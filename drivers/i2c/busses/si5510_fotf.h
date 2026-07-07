/* si5510_fotf.h — from-scratch Si5510 NB FOTF plan generator (no tables).
 * Computes the full boot file for any OUT13 target on the 1 kHz grid, byte-exact
 * with the CBPro corpus (verified on all 199,100 files, 1000..200099 kHz).
 * Author: RFNM. */
#ifndef SI5510_FOTF_H
#define SI5510_FOTF_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define SI5510_FOTF_MIN_KHZ 1000u
#define SI5510_FOTF_MAX_KHZ 200099u
#define SI5510_FOTF_MAX_LEN 192      /* worst case boot file is 179 bytes */

/* 1 if khz is on the supported grid (integer kHz, 1000..200099). */
int si5510_fotf_supported(uint32_t khz);

/* Generate the complete NB FOTF boot file (name + marker + scrambled datablock +
 * EOT + CRC) for OUT13 = khz. Returns total length, or -1 if khz unsupported or
 * cap < SI5510_FOTF_MAX_LEN. */
int si5510_fotf_gen(uint32_t khz, uint8_t *out, int cap);

/* --- NA plan generator: OUT9 programmable bench output (see docs/NA-FOTF-MODEL.md).
 * Byte-exact vs the rev4 sweep corpus (8,400 plans). Requires 64-bit (uses __int128
 * for the blk3 phase-adjust); the Lime i.MX8MP target is arm64. --- */
#define SI5510_FOTF_NA_MIN_KHZ 1000u
#define SI5510_FOTF_NA_MAX_KHZ 200099u
#define SI5510_FOTF_NA_MAX_LEN 320   /* worst-case NA boot file is ~281 bytes */

/* 1 if khz is a supported OUT9 target (integer kHz, 1000..200099). */
int si5510_fotf_na_supported(uint32_t khz);

/* Generate the complete NA FOTF boot file for OUT9 = khz. Returns total length, or -1
 * if khz unsupported or cap < SI5510_FOTF_NA_MAX_LEN. */
int si5510_fotf_gen_na(uint32_t khz, uint8_t *out, int cap);

#endif
