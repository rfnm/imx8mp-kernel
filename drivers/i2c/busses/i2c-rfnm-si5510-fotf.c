/* si5510_fotf.c — from-scratch Si5510 NB FOTF plan generator (no tables).
 *
 * Reverse-engineered CBPro FOTF3 planner math, byte-exact against all 199,100 corpus
 * plans (1000..200099 kHz, 1 kHz grid). Model, with f = OUT13 target in kHz and
 * NB = 5836800/f (Fvco = 11673.6 MHz, R13 = 2, OUT13 = Fvco/(NB*R13)):
 *
 *   m  = smallest integer >= ceil(50000/f) with NB/m non-integer, hunting upward while
 *        the fine word NB/m stays >= ~9; if it would drop below, fall back to m = ceil
 *        with an integer fine word (corpus: only f = 97280, 194560). m-1 is written to
 *        the coarse range registers 0xde98/0xde9c; the DSPLL fine word is NB/m.
 *   V  = 256*NB/m = 1494220800/(f*m), kept as an exact reduced fraction p/q.
 *   K  = ceil(f*m/2750)          (= ceil(intermediate F1 / 5.5 MHz, F1 = 2*f*m kHz)
 *   A  = floor(32768*(K*p + 113*q)/(7*q))    32-bit word at 0xd7f7 (the /7 and the
 *        RFPLL MR = 2084+4/7 share the same origin)
 *   num = p << k normalized into [2^37, 2^38), LE at 0xd89d.. (bytes 3,4 are repeated
 *        in the 0x0B burst payload); den = q << k, LE at 0xd8a5.. (max 30 bits).
 *
 * Template quirk (replicates the corpus batch structure, keyed on the 100 kHz run
 * [B, B+99], B = (f/100)*100): if the whole run is m==1 -> short plan (no 0xde9x);
 * if the run mixes m==1 with a hunted integer-NB point -> every plan long, the m==1
 * plans carry flag 0x20 at 0xde90 and coarse value 10; runs entirely m>=2 write flag 0
 * and the coarse group only when some plan in the run has m-1 != 10 (10 is the base
 * config's coarse value; pure m==11 runs omit the group).
 *
 * Single source shared between the RE repo (userspace tests) and the kernel driver
 * (drivers/i2c/busses/i2c-rfnm-si5510-fotf.c) — keep both copies identical.
 * Author: RFNM. */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#endif

#include "si5510_keystream.h"
#include "si5510_fotf.h"

#define FVCO2_KHZ   5836800u          /* Fvco/2 in kHz; NB = FVCO2/f */
#define VNUM        1494220800u       /* 256*FVCO2 = numerator of V*(f*m) */
#define FM_MAX      648533u           /* f*m above this => fine word < ~9, planner falls back */

static uint32_t fotf_crc32_mpeg2(const uint8_t *p, int n) {
	uint32_t c = 0xFFFFFFFFu;
	int i, k;
	for(i = 0; i < n; i++) {
		c ^= (uint32_t)p[i] << 24;
		for(k = 0; k < 8; k++) {
			c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
		}
	}
	return c;
}

static uint32_t fotf_gcd32(uint32_t a, uint32_t b) {
	while(b) {
		uint32_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

/* coarse stage: smallest m >= ceil(50000/f) with 5836800 % (f*m) != 0, giving up (and
 * falling back to the initial m, integer fine word) if f*m would exceed FM_MAX */
static uint32_t fotf_m_of(uint32_t f) {
	uint32_t m0 = (50000u + f - 1) / f;
	uint32_t m = m0;
	while(FVCO2_KHZ % (f * m) == 0) {
		m++;
		if(f * m > FM_MAX) {
			return m0;
		}
	}
	return m;
}

/* template shape of the 100 kHz corpus run containing f: run_kind 0 = short (all m==1),
 * 1 = flagged (m==1 plans get 0xde90 bit5 + coarse 10), 2 = all m>=2.
 * *coarse_present = whether the 0xde98/0xde9c group is written at all. */
static void fotf_run_info(uint32_t f, int *run_kind, int *coarse_present) {
	uint32_t B = (f / 100u) * 100u;
	uint32_t fp;
	int any1 = 0, any2 = 0, any_off10 = 0;
	for(fp = B; fp < B + 100u; fp++) {
		uint32_t m = fotf_m_of(fp);
		if(m == 1) {
			any1 = 1;
		} else {
			any2 = 1;
			if(m - 1 != 10) {
				any_off10 = 1;
			}
		}
	}
	if(!any2) {
		*run_kind = 0;
		*coarse_present = 0;
	} else if(any1) {
		*run_kind = 1;
		*coarse_present = 1;
	} else {
		*run_kind = 2;
		*coarse_present = any_off10;
	}
}

int si5510_fotf_supported(uint32_t khz) {
	return khz >= SI5510_FOTF_MIN_KHZ && khz <= SI5510_FOTF_MAX_KHZ;
}

static int fotf_emit_reg(uint8_t *r, uint32_t addr, uint8_t val, uint8_t mask) {
	r[0] = 0x0D;
	r[1] = (addr >> 8) & 0xFF;
	r[2] = addr & 0xFF;
	r[3] = val;
	r[4] = mask;
	return 5;
}

int si5510_fotf_gen(uint32_t khz, uint8_t *out, int cap) {
	uint32_t f = khz, m, fm, g, q, K, k;
	uint64_t p, A, num, den;
	uint8_t rec[160];
	int run_kind, coarse_present, n = 0, o = 0, i, nl;
	char name[16];
	uint16_t blk;
	uint32_t crc, B;

	if(!si5510_fotf_supported(khz) || cap < SI5510_FOTF_MAX_LEN) {
		return -1;
	}

	m = fotf_m_of(f);
	fm = f * m;
	g = fotf_gcd32(VNUM, fm);
	p = VNUM / g;                          /* V = p/q, reduced */
	q = fm / g;
	K = (fm + 2749u) / 2750u;
	A = (32768ull * (K * p + 113ull * q)) / (7ull * q);
	for(k = 0; (p >> k) != 0; k++) {       /* k = bit length of p ... */
	}
	k = 38 - k;                            /* ... normalize p into [2^37, 2^38) */
	num = p << k;
	den = (uint64_t)q << k;
	fotf_run_info(f, &run_kind, &coarse_present);

	n += fotf_emit_reg(rec + n, 0xc390, 0x00, 0x01);
	for(i = 0; i < 4; i++) {
		n += fotf_emit_reg(rec + n, 0xd7f7 + i, (A >> (8 * i)) & 0xFF, 0xFF);
	}
	for(i = 0; i < 6; i++) {
		n += fotf_emit_reg(rec + n, 0xd89d + i, (num >> (8 * i)) & 0xFF, 0xFF);
	}
	n += fotf_emit_reg(rec + n, 0xd8a3, (num >> 48) & 0x03, 0x03);
	n += fotf_emit_reg(rec + n, 0xd8a4, (num >> 56) & 0xFF, 0xFF);
	for(i = 0; i < 3; i++) {
		n += fotf_emit_reg(rec + n, 0xd8a5 + i, (den >> (8 * i)) & 0xFF, 0xFF);
	}
	n += fotf_emit_reg(rec + n, 0xd8a8, (den >> 24) & 0x3F, 0x3F);
	if(run_kind != 0) {
		uint8_t flag = (run_kind == 1 && m == 1) ? 0x20 : 0x00;
		n += fotf_emit_reg(rec + n, 0xde90, flag, 0x20);
		if(coarse_present) {
			uint32_t cv = (run_kind == 1 && m == 1) ? 10 : m - 1;
			uint32_t basea;
			for(basea = 0xde98; basea <= 0xde9c; basea += 4) {
				for(i = 0; i < 4; i++) {
					n += fotf_emit_reg(rec + n, basea + i, (cv >> (8 * i)) & 0xFF, 0xFF);
				}
			}
		}
	}
	rec[n++] = 0x0B;
	rec[n++] = 0x00;
	rec[n++] = 0x08;
	rec[n++] = 0x00;
	rec[n++] = 0x49;
	rec[n++] = 0x01;
	rec[n++] = 0x00;
	rec[n++] = 0x00;
	rec[n++] = 0x00;
	rec[n++] = (num >> 24) & 0xFF;
	rec[n++] = (num >> 32) & 0xFF;
	rec[n++] = 0x00;
	rec[n++] = 0x00;
	rec[n++] = 0x00;
	n += fotf_emit_reg(rec + n, 0xc312, 0x01, 0x01);
	n += fotf_emit_reg(rec + n, 0xc390, 0x01, 0x01);

	/* wrap: name + marker + scrambled datablock + clear EOT + clear CRC (big-endian) */
	B = (f / 100u) * 100u;
	nl = snprintf(name, sizeof(name), "NB Plan%u", f - B + 1);
	memcpy(out + o, name, nl);
	o += nl;
	out[o++] = 0x1B;
	out[o++] = 0x0A;
	out[o++] = 0x1B;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	blk = n + 1 + 4;
	crc = fotf_crc32_mpeg2(rec, n);
	out[o] = 0x02;
	out[o + 1] = (blk >> 8) & 0xFF;
	out[o + 2] = blk & 0xFF;
	memcpy(out + o + 3, rec, n);
	for(i = 0; i < n + 3 && i < (int)sizeof(si5510_keystream); i++) {
		out[o + i] ^= si5510_keystream[i];
	}
	o += n + 3;
	out[o++] = 0x04;
	out[o++] = (crc >> 24) & 0xFF;
	out[o++] = (crc >> 16) & 0xFF;
	out[o++] = (crc >> 8) & 0xFF;
	out[o++] = crc & 0xFF;
	return o;
}

/* =====================================================================================
 * NA plan generator — OUT9 programmable bench output. Byte-exact against the rev4 sweep
 * corpus (8,400 plans, r/si5510/gen_na_rev4/<MHz>/). Full model: docs/NA-FOTF-MODEL.md.
 * Shares the wrapper (name + marker tt=0x0A + scramble + EOT + CRC32-MPEG2) with NB.
 * NA sits on the shared NA DSPLL: OUT9 = Fvco/(NA*R9), NA fractional + R9 integer.
 * Needs 64-bit (unsigned __int128 for the blk3 phase-adjust); Lime target is arm64.
 * Author: RFNM. ==================================================================== */
#define NA_FVCO     11673600u         /* Fvco in kHz */
#define NA_VNUM     2988441600u       /* 256*Fvco; V = VNUM/FnA */
#define NA_C4FVCO   46694400u         /* 4*Fvco; drvcfg tick base */

static const uint8_t na_lutx[12] = {0x84,0x85,0x85,0x86,0x80,0x81,0x81,0x82,0x82,0x83,0x83,0x84};
static const uint8_t na_luty[12] = {0x80,0x80,0x81,0x81,0x7c,0x7c,0x7d,0x7d,0x7e,0x7e,0x7f,0x7f};
static const uint16_t na_drv_addr[8] = {0xd03e,0xd042,0xd046,0xd048,0xd04a,0xd04e,0xd054,0xd05c};

/* R9 = 2*ceil(50000/f), then hunt +2 while NA would be integer, falling back if the fine
 * word V would drop below 2304 (=9*256) — corpus-bounded, mirrors NB's FM_MAX. */
static uint32_t na_r9_of(uint32_t f) {
	uint32_t r9 = 2u * ((50000u + f - 1u) / f);
	uint32_t r0 = r9;
	while(NA_FVCO % (f * r9) == 0u) {
		r9 += 2u;
		if((uint64_t)2304u * f * r9 > NA_VNUM) {
			return r0;
		}
	}
	return r9;
}

/* drvcfg X/Y: periodic LUT-12 on the branch tick t=floor(4Fvco/FnA - 10/33),
 * u=floor(4Fvco/FnA - 27/154); Y has a one-slot sliver where the X grid crossed but Y
 * has not (delta_X > delta_Y). */
static void na_drvcfg(uint32_t fna, uint8_t *X, uint8_t *Y) {
	uint32_t t = (uint32_t)(((uint64_t)33u * NA_C4FVCO - (uint64_t)10u * fna) / ((uint64_t)33u * fna));
	uint32_t u = (uint32_t)(((uint64_t)154u * NA_C4FVCO - (uint64_t)27u * fna) / ((uint64_t)154u * fna));
	*X = na_lutx[t % 12u];
	*Y = (u % 12u == 4u && t < u) ? 0x82u : na_luty[u % 12u];
}

/* Kernel-safe unsigned 128/128 divide (quotient + remainder). The kernel links no libgcc,
 * so __int128 `/` and `%` (which lower to __udivti3/__umodti3) are unavailable. Do binary
 * long division using only constant shift-by-1 (variable __int128 shifts would emit
 * __ashlti3/__lshrti3, also absent). blk3's true quotient is 16-bit, so 128 cheap iterations. */
static void na_udivmod128(unsigned __int128 n, unsigned __int128 d,
		unsigned __int128 *quo, unsigned __int128 *rem) {
	unsigned __int128 q = 0, r = 0;
	int i;
	for(i = 0; i < 128; i++) {
		r = (r << 1) | (unsigned __int128)(n >> 127);
		n <<= 1;
		q <<= 1;
		if(r >= d) {
			r -= d;
			q |= 1u;
		}
	}
	*quo = q;
	*rem = r;
}

/* blk3 = firmware var PLLR1A_SYNC_VDIV_PHADJ (timestamp phase-adjust), exact rational:
 *   TU = 23907532800000/7 ; 1.486 = 743/500 (exact)
 *   frac = TU * [ (FnA_kHz mod mden)*5e11 + (743*FnA_Hz + 5e11)*mden^2 ]
 *        / ( mden^2 * 5e11 * FnA_Hz * 7 )          where mden = 400/gcd(FnA_kHz,400)
 *   blk3 = round_half_to_even(frac)
 * Derivation/validation: docs/calc_sync_reset_virtual_div_phadj.dis.txt, scripts/na_tail_solve.py. */
static uint16_t na_blk3(uint32_t fna_khz) {
	uint64_t fna_hz = (uint64_t)fna_khz * 1000u;
	uint32_t g = fotf_gcd32(fna_khz, 400u);
	uint32_t mden = 400u / g;
	uint32_t rem = (fna_khz / g) % mden;
	uint64_t mden2 = (uint64_t)mden * mden;
	const uint64_t TUn = 23907532800000ull;
	const uint64_t E11 = 500000000000ull;               /* 5e11 = 500*1e9 */
	unsigned __int128 N = (unsigned __int128)TUn *
		((unsigned __int128)rem * E11 +
		 (unsigned __int128)(743ull * fna_hz + E11) * mden2);
	unsigned __int128 D = (unsigned __int128)E11 * mden2;
	D = D * fna_hz;
	D = D * 7u;
	unsigned __int128 q, r;
	na_udivmod128(N, D, &q, &r);
	if(2 * r > D || (2 * r == D && (q & 1))) {
		q += 1;
	}
	return (uint16_t)q;
}

int si5510_fotf_na_supported(uint32_t khz) {
	return khz >= SI5510_FOTF_NA_MIN_KHZ && khz <= SI5510_FOTF_NA_MAX_KHZ;
}

/* per-plan fine word / A / R9 for one target */
struct na_plan {
	uint32_t r9, fna;
	uint64_t num, den, A;
	uint8_t X, Y;
};

static void na_compute(uint32_t f, struct na_plan *pl) {
	uint32_t r9 = na_r9_of(f), fna = f * r9, k;
	uint32_t g = fotf_gcd32(NA_VNUM, fna);
	uint64_t p = NA_VNUM / g, q = fna / g, K;
	for(k = 0; (p >> k) != 0; k++) {
	}
	k = 38 - k;
	K = (fna + 5499u) / 5500u;
	pl->r9 = r9;
	pl->fna = fna;
	pl->num = p << k;
	pl->den = q << k;
	pl->A = (32768ull * (K * p + 113ull * q)) / (7ull * q);
	na_drvcfg(fna, &pl->X, &pl->Y);
}

int si5510_fotf_gen_na(uint32_t khz, uint8_t *out, int cap) {
	uint32_t f = khz, B, x, i, o = 0;
	uint8_t rec[288];
	int n = 0, nl;
	int any_r9_2 = 0, all_r9_10 = 1, blk2_present = 0, x_present = 0, y_present = 0;
	struct na_plan pl;
	char name[16];
	uint16_t blk;
	uint32_t crc;

	if(!si5510_fotf_na_supported(khz) || cap < SI5510_FOTF_NA_MAX_LEN) {
		return -1;
	}

	/* batch [B, B+99] union flags (same 100-plan run definition as NB) */
	B = (f / 100u) * 100u;
	for(x = B; x < B + 100u; x++) {
		uint32_t r9 = na_r9_of(x), fna = x * r9;
		uint8_t X, Y;
		na_drvcfg(fna, &X, &Y);
		if(r9 == 2u) {
			any_r9_2 = 1;
		}
		if(r9 != 10u) {
			all_r9_10 = 0;
		}
		if(fna / 400u != 250u) {
			blk2_present = 1;
		}
		if(X != 0x83u) {
			x_present = 1;
		}
		if(Y != 0x7fu) {
			y_present = 1;
		}
	}

	na_compute(f, &pl);

	n += fotf_emit_reg(rec + n, 0xc384, 0x00, 0x01);
	for(i = 0; i < 8; i++) {
		uint16_t a = na_drv_addr[i];
		int isY = (a == 0xd04a || a == 0xd04e);
		if((isY && y_present) || (!isY && x_present)) {
			n += fotf_emit_reg(rec + n, a, isY ? pl.Y : pl.X, 0xFF);
			n += fotf_emit_reg(rec + n, a + 1, 0x01, 0x01);
		}
	}
	for(i = 0; i < 4; i++) {
		n += fotf_emit_reg(rec + n, 0xd7eb + i, (pl.A >> (8 * i)) & 0xFF, 0xFF);
	}
	for(i = 0; i < 6; i++) {
		n += fotf_emit_reg(rec + n, 0xd87f + i, (pl.num >> (8 * i)) & 0xFF, 0xFF);
	}
	n += fotf_emit_reg(rec + n, 0xd885, (pl.num >> 48) & 0x03, 0x03);
	n += fotf_emit_reg(rec + n, 0xd886, (pl.num >> 56) & 0xFF, 0xFF);
	for(i = 0; i < 3; i++) {
		n += fotf_emit_reg(rec + n, 0xd887 + i, (pl.den >> (8 * i)) & 0xFF, 0xFF);
	}
	n += fotf_emit_reg(rec + n, 0xd88a, (pl.den >> 24) & 0x3F, 0x3F);
	if(any_r9_2) {
		n += fotf_emit_reg(rec + n, 0xde50, (pl.r9 == 2u) ? 0x20 : 0x00, 0x20);
	}
	if(!all_r9_10) {
		uint32_t rv = (pl.r9 == 2u) ? 10u : (pl.r9 / 2u - 1u);
		uint32_t base;
		for(base = 0xde58; base <= 0xde5c; base += 4u) {
			for(i = 0; i < 4; i++) {
				n += fotf_emit_reg(rec + n, base + i, (rv >> (8 * i)) & 0xFF, 0xFF);
			}
		}
	}
	/* blk1: num bytes 3,4 */
	rec[n++] = 0x0B; rec[n++] = 0x00; rec[n++] = 0x08; rec[n++] = 0x00; rec[n++] = 0x39;
	rec[n++] = 0x01; rec[n++] = 0x00; rec[n++] = 0x00; rec[n++] = 0x00;
	rec[n++] = (pl.num >> 24) & 0xFF;
	rec[n++] = (pl.num >> 32) & 0xFF;
	rec[n++] = 0x00; rec[n++] = 0x00; rec[n++] = 0x00;
	/* blk2: floor(FnA/400) BE16 */
	if(blk2_present) {
		uint32_t v = pl.fna / 400u;
		rec[n++] = 0x0B; rec[n++] = 0x00; rec[n++] = 0x04; rec[n++] = 0x00; rec[n++] = 0x00;
		rec[n++] = 0x01; rec[n++] = 0x00; rec[n++] = 0x00;
		rec[n++] = (v >> 8) & 0xFF; rec[n++] = v & 0xFF;
	}
	/* blk3: PLLR1A_SYNC_VDIV_PHADJ BE16 */
	{
		uint16_t v = na_blk3(pl.fna);
		rec[n++] = 0x0B; rec[n++] = 0x00; rec[n++] = 0x08; rec[n++] = 0x00; rec[n++] = 0x26;
		rec[n++] = 0x01; rec[n++] = 0x00; rec[n++] = 0x00; rec[n++] = 0x00;
		rec[n++] = 0x00; rec[n++] = 0x00; rec[n++] = 0x00;
		rec[n++] = (v >> 8) & 0xFF; rec[n++] = v & 0xFF;
	}
	n += fotf_emit_reg(rec + n, 0xc2fe, 0x01, 0x01);
	n += fotf_emit_reg(rec + n, 0xc384, 0x01, 0x01);

	/* wrap: name + marker(tt=0x0A) + scrambled datablock + clear EOT + clear CRC */
	nl = snprintf(name, sizeof(name), "NA Plan%u", f - B + 1);
	memcpy(out + o, name, nl);
	o += nl;
	out[o++] = 0x1B;
	out[o++] = 0x0A;
	out[o++] = 0x1B;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	out[o++] = 0x1A;
	blk = n + 1 + 4;
	crc = fotf_crc32_mpeg2(rec, n);
	out[o] = 0x02;
	out[o + 1] = (blk >> 8) & 0xFF;
	out[o + 2] = blk & 0xFF;
	memcpy(out + o + 3, rec, n);
	for(i = 0; i < (uint32_t)(n + 3) && i < (uint32_t)sizeof(si5510_keystream); i++) {
		out[o + i] ^= si5510_keystream[i];
	}
	o += n + 3;
	out[o++] = 0x04;
	out[o++] = (crc >> 24) & 0xFF;
	out[o++] = (crc >> 16) & 0xFF;
	out[o++] = (crc >> 8) & 0xFF;
	out[o++] = crc & 0xFF;
	return o;
}
