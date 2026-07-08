#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <linux/rfnm-shared.h>
#include <linux/rfnm-si5510.h>
#include <linux/printk.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#include "si5510_fotf.h"

typedef unsigned char       uint8_t;
typedef   signed char        int8_t;

uint32_t RFNM_SI5510_CMD_BUFFER_SIZE;

uint32_t rfnm_dcs_freq_hz;
static struct i2c_client *rfnm_si5510_client;

void rfnm_si5510_i2c_read(struct i2c_client *client, uint8_t * buf, int cnt) {

	uint8_t CTS[6] = {0xf0, 0x0f};
	i2c_master_send(client, CTS, 2);

	i2c_master_recv(client, buf, cnt);
}

void rfnm_si5510_i2c_write(struct i2c_client *client, uint8_t * buf, int cnt) {

	i2c_master_send(client, buf, cnt);
}

void rfnm_si5510_sio_test(struct i2c_client *client) {
 uint8_t sio_test_request[5] = { 0xF0, 0x0F, 0x01, 0xAB, 0xCD };

 rfnm_si5510_i2c_write(client, sio_test_request, 5);

 uint8_t i2c_read_buf[100];

 rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 4);

 while (i2c_read_buf[0] != 0x80) {
	 msleep(10);
	 rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 4);
 }

}

void rfnm_si5510_cts(struct i2c_client *client) {

	uint8_t i2c_read_buf[100];

	do {
		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 1);
		msleep(10);
		//printk("RFNM: %02x\n", i2c_read_buf[0]);
	} while (i2c_read_buf[0] != 0x80);
}

void rfnm_si5510_buffsize(struct i2c_client *client) {

	uint8_t i2c_read_buf[100];
	uint8_t sio_info_request[3] = { 0xF0, 0x0F, 0x02 };
	rfnm_si5510_i2c_write(client, sio_info_request, 3);

	do {
		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 5);
	} while(i2c_read_buf[0] != 0x80);

	RFNM_SI5510_CMD_BUFFER_SIZE = (i2c_read_buf[2] << 8) + i2c_read_buf[1];
	RFNM_SI5510_CMD_BUFFER_SIZE -= 10; // margin for command packets

	//printk("RFNM: Command buffer size from SIO_INFO: %d\n", RFNM_SI5510_CMD_BUFFER_SIZE);
	//return RFNM_SI5510_CMD_BUFFER_SIZE;
}


void rfnm_si5510_restart(struct i2c_client *client) {

	uint8_t i2c_read_buf[100];
	uint8_t restart_request[] = { 0xF0, 0x0F, 0xF0, 0x00 };
	rfnm_si5510_i2c_write(client, restart_request, 4);

	do {

		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 1);

		//printk("RFNM: RESTART returned %02x\n", i2c_read_buf[0]);

		msleep(10);
	} while(i2c_read_buf[0] != 0x80);

	//printk("RFNM: RESTART Sent.\n");
}

void rfnm_si5510_host_load(struct i2c_client *client, char * data, int datalen) {

	//RFNM_SI5510_CMD_BUFFER_SIZE -= 10; // margin for command packets
	uint8_t i2c_read_buf[100];
	int numberOfChunks = (datalen / RFNM_SI5510_CMD_BUFFER_SIZE) + 1;

	int chunkNum;

	uint8_t host_load_command_init[3] = { 0xF0, 0x0F, 0x05 };
	uint8_t * host_load_command;

	host_load_command = kmalloc(RFNM_SI5510_CMD_BUFFER_SIZE + 10, GFP_KERNEL);
	memcpy(host_load_command, &host_load_command_init[0], 3);

	for (chunkNum = 0; chunkNum < numberOfChunks; ++chunkNum) {
		int chunkSize = RFNM_SI5510_CMD_BUFFER_SIZE;
		if (chunkNum == numberOfChunks - 1) {
			chunkSize = datalen % RFNM_SI5510_CMD_BUFFER_SIZE;
		}

		memcpy(&host_load_command[3], &data[chunkNum * RFNM_SI5510_CMD_BUFFER_SIZE], chunkSize);
		rfnm_si5510_i2c_write(client, &host_load_command[0], chunkSize + 3);

		do {
			rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 1);
			//printk("RFNM: CHUNK %d status is %02x\n", chunkNum, i2c_read_buf[0]);
		} while(i2c_read_buf[0] != 0x80);
	}

	kfree(host_load_command);
}

extern void rfnm_wsled_send_chain(uint8_t);
extern void rfnm_wsled_set(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);

static void rfnm_si5510_boot(struct i2c_client *client) {

	uint8_t i2c_read_buf[100];
	uint8_t boot_request[] = { 0xF0, 0x0F, 0x07 };
	rfnm_si5510_i2c_write(client, boot_request, 3);

	do {
		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 1);
		//printk("RFNM: BOOT returned %02x\n", i2c_read_buf[0]);
		msleep(10);
	} while (i2c_read_buf[0] != 0x80);

	//printk("RFNM: BOOT Sent.\n");
}


uint8_t rfnm_si5510_reference_status(struct i2c_client *client) {
	uint8_t i2c_read_buf[100] = {0};

	uint8_t reference_status_request[] = { 0xF0, 0x0F, 0x16 };
	rfnm_si5510_i2c_write(client, reference_status_request, 3);

	do {
		if (i2c_read_buf[0] == 0x90) {
			printk("RFNM: FWERR triggered. See text under Common Errors.\n");
			while (1) {}
		}
		msleep(10);
		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 5);
	} while(i2c_read_buf[0] != 0x80);

	// return true if reference PLL is locked, otherwise return false.
	return (i2c_read_buf[0] == 0x80 && i2c_read_buf[1] == 0x00 && i2c_read_buf[2] == 0 && i2c_read_buf[3] == 0 && i2c_read_buf[4] == 0);
}

static int rfnm_si5510_plan_board_id(int board_id) {
	// MT3812 (Yucca, id 5) needs the same clocks as Granita, so reuse the Granita frequency
	// plans for it. Plan-selection cheat ONLY - the bootconfig ids stay untouched so driver
	// binding and everything else still sees the real board id.
	if(board_id == RFNM_DAUGHTERBOARD_YUCCA) {
		return RFNM_DAUGHTERBOARD_GRANITA;
	}
	return board_id;
}

int can_use_si5510_config(struct rfnm_bootconfig *cfg, int daughterboard_1, int daughterboard_2) {
	int id0 = rfnm_si5510_plan_board_id(cfg->daughterboard_eeprom[0].board_id);
	int id1 = rfnm_si5510_plan_board_id(cfg->daughterboard_eeprom[1].board_id);

	if(daughterboard_1 == daughterboard_2) {
		// relax conditions: only need to match one daughterboard
		// (to account for missing or unsupported daughterboards)
		if(
			(id0 == daughterboard_1 && id1 == daughterboard_1) ||

			(id0 == daughterboard_1 && (
				cfg->daughterboard_present[1] == RFNM_DAUGHTERBOARD_NOT_FOUND || id1 == RFNM_DAUGHTERBOARD_BREAKOUT
			)) ||
			(id1 == daughterboard_1 && (
				cfg->daughterboard_present[0] == RFNM_DAUGHTERBOARD_NOT_FOUND || id0 == RFNM_DAUGHTERBOARD_BREAKOUT
			))

		) {
			//printk("RFNM: board ids %d %d present %d %d", cfg->daughterboard_eeprom[0].board_id, cfg->daughterboard_eeprom[1].board_id, cfg->daughterboard_present[0], cfg->daughterboard_present[1]);
			return 1;
		}
	} else {
		if(id0 == daughterboard_1 && id1 == daughterboard_2) {
			return 1;
		}
	}

	return 0;
}

void rfnm_si5510_set_output_status(struct i2c_client *client, int output_id, int enable_disable) {
	uint8_t i2c_read_buf[100];
	uint8_t send_output_status_request[] = { 0xF0, 0x0F, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint32_t output_req = output_req = 1 << output_id;

	memcpy(&send_output_status_request[3], &output_req, 4);

	if(enable_disable) {
		send_output_status_request[7] = 1;
		printk("RFNM: Enabling clock output %d\n", output_id);
	} else {
		printk("RFNM: Disabling clock output %d\n", output_id);
	}

	rfnm_si5510_i2c_write(client, send_output_status_request, 8);

	do {
		if (i2c_read_buf[0] == 0x90) {
			printk("RFNM: FWERR triggered. See text under Common Errors.\n");
			while (1) {}
		}
		msleep(10);
		rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 5);
	} while(i2c_read_buf[0] != 0x80);
}

EXPORT_SYMBOL(rfnm_si5510_set_output_status);



static ssize_t rfnm_ext_ref_out_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);

	if(buf[1] != 'n' && buf[1] != 'f' && buf[1] != '0' && buf[1] != '1') {
		printk("RFNM: Si5510: Valid inputs are either 'on' or 'off'");
		return -EINVAL;
	}

	if(buf[1] == 'n' || buf[1] == '1') {
		printk("RFNM: Si5510: enabling ext reference output @ 10 MHz\n");
		rfnm_si5510_set_output_status(client, 9, 1);
	} else {
		printk("RFNM: Si5510: disabling external reference output\n");
		rfnm_si5510_set_output_status(client, 9, 0);
	}

	return count;
}

static DEVICE_ATTR_WO(rfnm_ext_ref_out);


static int rfnm_si5510_wait_clock_stable(struct i2c_client *client) {
	int retries;

	if(!client) {
		return -ENODEV;
	}

	for(retries = 0; retries < 1000; retries++) {
		if(rfnm_si5510_reference_status(client)) {
			return 0;
		}
		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

int rfnm_si5510_dcs_freq_supported(uint64_t freq) {
	// plans are generated on an integer-kHz grid; a non-multiple-of-1000 target would silently
	// program the floor kHz below (freq / 1000 truncates) - exact or refuse (issue #11)
	if(freq % 1000) {
		return 0;
	}

	return si5510_fotf_supported(freq / 1000);
}
EXPORT_SYMBOL(rfnm_si5510_dcs_freq_supported);

static int rfnm_si5510_set_dcs_freq_work(struct i2c_client *client, uint64_t freq) {

	uint8_t fotf_nb[SI5510_FOTF_MAX_LEN];
	int fotf_nb_size;

	if(!client) {
		return -ENODEV;
	}

	if(freq % 1000) {
		printk("RFNM: Si5510: unsupported LA9310 DCS frequency %llu Hz (not on the 1 kHz grid)\n", (unsigned long long)freq);
		return -EINVAL;
	}

	/* the NB FOTF plan is computed from scratch (RE'd CBPro planner math, byte-exact against the
	 * full 199,100-plan corpus) - no plan table in the kernel anymore */
	fotf_nb_size = si5510_fotf_gen(freq / 1000, fotf_nb, sizeof(fotf_nb));
	if(fotf_nb_size <= 0) {
		printk("RFNM: Si5510: unsupported LA9310 DCS frequency %llu Hz\n", (unsigned long long)freq);
		return -EINVAL;
	}

	printk("RFNM: Si5510: setting LA9310 to %llu kHz, plan size %d\n", (unsigned long long)(freq / 1000), fotf_nb_size);

	rfnm_si5510_host_load(client, fotf_nb, fotf_nb_size);
	return 0;
}

int rfnm_si5510_set_dcs_freq(struct i2c_client *client, uint64_t freq) {
	int ret;

	if(!client) {
		return -ENODEV;
	}

	if(rfnm_dcs_freq_hz == freq) {
		ret = rfnm_si5510_wait_clock_stable(client);
		if(ret) {
			// issue #11: the cache says this frequency is already programmed, but the clock is not
			// stable - stop vouching for the hardware state so the next request reprograms instead
			// of skip-trusting a broken clock
			rfnm_dcs_freq_hz = 0;
		}
		return ret;
	}

	ret = rfnm_si5510_set_dcs_freq_work(client, freq);
	if(ret) {
		return ret;
	}
	ret = rfnm_si5510_wait_clock_stable(client);
	if(ret) {
		printk("RFNM: Si5510: timed out waiting for stable clock at %llu Hz\n", (unsigned long long)freq);
		// issue #11: the FOTF for the new frequency IS loaded - the hardware is no longer at the old
		// frequency and not confirmed at the new one. Leaving the old value cached made later requests
		// for that frequency skip reprogramming entirely (silent wrong-clock delivery, wedged until
		// reboot). Mark the state unknown instead: 0 never matches, so the next request reprograms.
		rfnm_dcs_freq_hz = 0;
		return ret;
	}

	rfnm_dcs_freq_hz = freq;
	return 0;
}

EXPORT_SYMBOL(rfnm_si5510_set_dcs_freq);

void rfnm_si5510_set_dco(struct i2c_client *client, int32_t val) {
	uint8_t i2c_read_buf[32];
	uint8_t send_dco_request[] = { 0xF0, 0x0F, 0x24, 0x01, 0x00, 0x00, 0x00, 0x00 };
	uint8_t i;

	for(i = 0; i < 4; i++)
		send_dco_request[4 + i] = (val >> (i << 3)) & 0xFF;

	rfnm_si5510_i2c_write(client, send_dco_request, 8);
	rfnm_si5510_i2c_read(client, &i2c_read_buf[0], 2);

	if(((i2c_read_buf[0] & 0xf0 ) == 0x80) && ((i2c_read_buf[1] & 0x01 ) == 0x00)) {
		printk("RFNM: Si5510 DCO set to %d steps (0.1 ppb/step, absolute)\n", val);
	}
	else {
		printk("RFNM: failed to set Si5510 DCO\n");
	}
}

EXPORT_SYMBOL(rfnm_si5510_set_dco);


uint32_t rfnm_si5510_get_dcs_freq(struct i2c_client *client) {
	return rfnm_dcs_freq_hz;
}

EXPORT_SYMBOL(rfnm_si5510_get_dcs_freq);




static ssize_t rfnm_set_dcs_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);
	
	uint32_t reqfreq;

    if (kstrtou32(&buf[0], 10, &reqfreq) != 0) {
		return -EINVAL;
	}

	if(reqfreq < 1000 || reqfreq > 2000000) {
		printk("RFNM: Invalid DCS frequency requested, valid range is [1 - 200] MHz, expressed in KHz. Eg, 122880 for 122.88 MHz\n");
		return -EINVAL;
	}

	return rfnm_si5510_set_dcs_freq(client, reqfreq * 1000) ? -EINVAL : count;

}

static DEVICE_ATTR_WO(rfnm_set_dcs_freq);



static ssize_t rfnm_set_dco_ppb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);

	int32_t ppb;

	if (kstrtos32(&buf[0], 10, &ppb) != 0) {
		return -EINVAL;
	}

	if(ppb < -10000 || ppb > 10000) {
		printk("RFNM: Invalid DCO offset %d ppb, configured MR DCO range is +-10000 ppb (+-10 ppm)\n", ppb);
		return -EINVAL;
	}

	// The 0x24 wire unit is DCO STEPS, one MR step = 0.100000039438 ppb (a base-config plan
	// property - see r/si5510/RE/SI5510-HANDOFF.md 3.1). steps = ppb * 10 is the correctly
	// rounded conversion over the whole +-10000 ppb range (max error 0.04 step). This
	// attribute used to pass the raw value through (i.e. it took steps and overstated the
	// pull ~10x); raw steps now live in rfnm_set_dco_steps below.
	rfnm_si5510_set_dco(client, ppb * 10);

	return count;
}

static DEVICE_ATTR_WO(rfnm_set_dco_ppb);


static ssize_t rfnm_set_dco_steps_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);

	int32_t steps;

	if (kstrtos32(&buf[0], 10, &steps) != 0) {
		return -EINVAL;
	}

	if(steps < -100000 || steps > 100000) {
		printk("RFNM: Invalid DCO offset %d steps, configured MR DCO range is +-100000 steps (+-10 ppm)\n", steps);
		return -EINVAL;
	}

	// raw absolute NUM_STEPS for opcode 0x24 - full 0.1 ppb resolution (cellsyncd's servo
	// writes this one; rfnm_set_dco_ppb above is the human-facing integer-ppb knob)
	rfnm_si5510_set_dco(client, steps);

	return count;
}

static DEVICE_ATTR_WO(rfnm_set_dco_steps);



/* --- siggen: programmable bench output on OUT9 (NA divider) --------------------------
 * OUT9 = Fvco/(NA*R9). Retuning HOST_LOADs an NA FOTF plan computed from scratch by
 * si5510_fotf_gen_na() (RE'd CBPro planner math, byte-exact vs the corpus) - no plan
 * table - and NEVER resets the LA9310: OUT9 is a bench clock, isolated from the NB/OUT13
 * radio path. rfnm_ext_ref_out stays as the 10 MHz on/off compat alias.
 *
 * SAFETY: an NA retune moves the shared NA fractional divider. It is only safe once the
 * base project has evicted the PCIe refclk (OUT4/OUT5) off NA onto Q dividers (the rev4
 * base; see docs/OUT9-SIGGEN-PLAN.md). Against a base that still carries PCIe on NA this
 * would move the PCIe clock. Retuning is an explicit sysfs action, gated on that base. */
static uint32_t rfnm_siggen_freq_khz;

static int rfnm_si5510_set_siggen_freq(struct i2c_client *client, uint32_t khz) {

	uint8_t fotf_na[SI5510_FOTF_NA_MAX_LEN];
	int fotf_na_size;

	if(!client) {
		return -ENODEV;
	}

	if(khz == 0) {
		// 0 = turn the output off
		rfnm_si5510_set_output_status(client, 9, 0);
		rfnm_siggen_freq_khz = 0;
		return 0;
	}

	if(!si5510_fotf_na_supported(khz)) {
		printk("RFNM: Si5510: unsupported siggen frequency %u kHz (valid range [1 - 200] MHz in kHz)\n", khz);
		return -EINVAL;
	}

	fotf_na_size = si5510_fotf_gen_na(khz, fotf_na, sizeof(fotf_na));
	if(fotf_na_size <= 0) {
		printk("RFNM: Si5510: failed to generate siggen plan for %u kHz\n", khz);
		return -EINVAL;
	}

	printk("RFNM: Si5510: retuning siggen (OUT9/NA) to %u kHz, plan size %d\n", khz, fotf_na_size);

	rfnm_si5510_host_load(client, fotf_na, fotf_na_size);
	rfnm_si5510_set_output_status(client, 9, 1);

	rfnm_siggen_freq_khz = khz;
	return 0;
}

static ssize_t rfnm_set_siggen_freq_show(struct device *dev, struct device_attribute *attr, char *buf) {

	return sysfs_emit(buf, "%u\n", rfnm_siggen_freq_khz);
}

static ssize_t rfnm_set_siggen_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);
	uint32_t khz;

	if(kstrtou32(&buf[0], 10, &khz) != 0) {
		return -EINVAL;
	}

	return rfnm_si5510_set_siggen_freq(client, khz) ? -EINVAL : count;
}

static DEVICE_ATTR_RW(rfnm_set_siggen_freq);





static struct gpio_desc *si5510_rst_gpio;
static struct gpio_desc *la9310_trst_gpio;
static struct gpio_desc *la9310_hrst_gpio;
static struct gpio_desc *la9310_bootstrap_en_gpio;

static struct gpio_desc *power_en_09_gpio;
static struct gpio_desc *la9310_power_en_gpio;
static DEFINE_MUTEX(rfnm_la9310_reset_lock);

static void rfnm_si5510_put_la9310_gpios(void) {
	if(!IS_ERR_OR_NULL(power_en_09_gpio)) {
		gpiod_put(power_en_09_gpio);
	}
	if(!IS_ERR_OR_NULL(la9310_trst_gpio)) {
		gpiod_put(la9310_trst_gpio);
	}
	if(!IS_ERR_OR_NULL(la9310_hrst_gpio)) {
		gpiod_put(la9310_hrst_gpio);
	}
	if(!IS_ERR_OR_NULL(la9310_bootstrap_en_gpio)) {
		gpiod_put(la9310_bootstrap_en_gpio);
	}
	if(!IS_ERR_OR_NULL(la9310_power_en_gpio)) {
		gpiod_put(la9310_power_en_gpio);
	}

	power_en_09_gpio = NULL;
	la9310_trst_gpio = NULL;
	la9310_hrst_gpio = NULL;
	la9310_bootstrap_en_gpio = NULL;
	la9310_power_en_gpio = NULL;
}

static int rfnm_si5510_get_la9310_gpios(struct device *dev) {
	int error;

	if(!IS_ERR_OR_NULL(la9310_trst_gpio) && !IS_ERR_OR_NULL(la9310_hrst_gpio) && !IS_ERR_OR_NULL(la9310_bootstrap_en_gpio) &&
			!IS_ERR_OR_NULL(power_en_09_gpio) && !IS_ERR_OR_NULL(la9310_power_en_gpio)) {
		return 0;
	}

	la9310_trst_gpio = gpiod_get(dev, "la9310-trst", GPIOD_OUT_LOW);
	if (IS_ERR(la9310_trst_gpio)) {
		error = PTR_ERR(la9310_trst_gpio);
		printk("RFNM: Failed to get la9310-trst gpio: %d\n", error);
		goto err;
	}

	la9310_hrst_gpio = gpiod_get(dev, "la9310-hrst", GPIOD_OUT_LOW);
	if (IS_ERR(la9310_hrst_gpio)) {
		error = PTR_ERR(la9310_hrst_gpio);
		printk("RFNM: Failed to get la9310-hrst gpio: %d\n", error);
		goto err;
	}

	la9310_bootstrap_en_gpio = gpiod_get(dev, "la9310-bootstrap-en", GPIOD_OUT_HIGH);
	if (IS_ERR(la9310_bootstrap_en_gpio)) {
		error = PTR_ERR(la9310_bootstrap_en_gpio);
		printk("RFNM: Failed to get la9310-bootstrap-en gpio: %d\n", error);
		goto err;
	}

	power_en_09_gpio = gpiod_get(dev, "09v-power-en", GPIOD_OUT_LOW);
	if (IS_ERR(power_en_09_gpio)) {
		error = PTR_ERR(power_en_09_gpio);
		printk("RFNM: Failed to get 09v-power-en gpio: %d\n", error);
		goto err;
	}

	la9310_power_en_gpio = gpiod_get(dev, "la9310-power-en", GPIOD_OUT_LOW);
	if (IS_ERR(la9310_power_en_gpio)) {
		error = PTR_ERR(la9310_power_en_gpio);
		printk("RFNM: Failed to get la9310-power-en gpio: %d\n", error);
		goto err;
	}

	return 0;

err:
	rfnm_si5510_put_la9310_gpios();
	return error;
}

static int rfnm_si5510_reset_la9310(uint64_t dcs_freq) {
	int error;

	mutex_lock(&rfnm_la9310_reset_lock);

	if(!dcs_freq) {
		error = -EINVAL;
		goto out;
	}

	if(IS_ERR_OR_NULL(la9310_trst_gpio) || IS_ERR_OR_NULL(la9310_hrst_gpio) || IS_ERR_OR_NULL(la9310_bootstrap_en_gpio) ||
			IS_ERR_OR_NULL(power_en_09_gpio) || IS_ERR_OR_NULL(la9310_power_en_gpio)) {
		error = -ENODEV;
		goto out;
	}

	if(!rfnm_si5510_dcs_freq_supported(dcs_freq)) {
		// issue #11: refuse BEFORE the GPIO teardown below - discovering an unsupported frequency
		// after the LA9310 is already held in reset leaves the board dead with its PCIe endpoint gone
		printk("RFNM: refusing LA9310 reset: DCS %llu Hz is not synthesizable\n", (unsigned long long)dcs_freq);
		error = -EINVAL;
		goto out;
	}

	gpiod_set_value_cansleep(la9310_hrst_gpio, 0);
	gpiod_set_value_cansleep(la9310_trst_gpio, 0);

	gpiod_set_value_cansleep(la9310_bootstrap_en_gpio, 0);

	gpiod_set_value_cansleep(power_en_09_gpio, 1);
	gpiod_set_value_cansleep(la9310_power_en_gpio, 1);

	usleep_range(1000, 2000);

	error = rfnm_si5510_set_dcs_freq(rfnm_si5510_client, dcs_freq);
	if(error) {
		printk("RFNM: Failed to set LA9310 DCS clock during reset: %d\n", error);
		goto out;
	}

	gpiod_set_value_cansleep(la9310_trst_gpio, 1);
	gpiod_set_value_cansleep(la9310_hrst_gpio, 1);

	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(la9310_bootstrap_en_gpio, 1);
	printk("RFNM: Performed LA9310 reset\n");

	error = 0;

out:
	mutex_unlock(&rfnm_la9310_reset_lock);
	return error;
}

int rfnm_board_reset_la9310(uint64_t dcs_freq) {
	return rfnm_si5510_reset_la9310(dcs_freq);
}
EXPORT_SYMBOL_GPL(rfnm_board_reset_la9310);

static ssize_t rfnm_reset_la9310_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	int error;

	if(buf[0] != '1' && buf[0] != 'r' && buf[0] != 'b') {
		printk("RFNM: Valid rfnm_reset_la9310 inputs are '1', 'reset', or 'boot'\n");
		return -EINVAL;
	}

	error = rfnm_board_reset_la9310(rfnm_dcs_freq_hz);
	if(error) {
		return error;
	}

	return count;
}

static DEVICE_ATTR_WO(rfnm_reset_la9310);

/*
uint32_t rfnm_si5510_plan_map[RFNM_NUM_DCS_FREQ][3] = {
	{300, 38, 38912000},{285, 40, 40960000},{256, 45, 45600000},
	{250, 46, 46694400},{240, 48, 48640000},{228, 51, 51200000},
	{200, 58, 58368000},{192, 60, 60800000},{190, 61, 61440000},
	{160, 72, 72960000},{152, 76, 76800000},{150, 77, 77824000},
	{128, 91, 91200000},{125, 93, 93388800},{120, 97, 97280000},
	{114, 102, 102400000},{100, 116, 116736000},{96, 121, 121600000},
	{95, 122, 122880000},{80, 145, 145920000},{76, 153, 153600000},
	{75, 155, 155648000},{64, 182, 182400000},{60, 194, 194560000},
	{57, 204, 204800000} };

EXPORT_SYMBOL(rfnm_si5510_plan_map);

void rfnm_si5510_load_from_map(struct i2c_client *client, int offset, int map, int RFNM_SI5510_CMD_BUFFER_SIZE) {
	map--;
	offset *= 4;
	rfnm_si5510_host_load(client, rfnm_q_plan_map[offset+map], rfnm_q_plan_map_sizes[offset+map], RFNM_SI5510_CMD_BUFFER_SIZE);
}
*/




static int rfnm_si5510_probe(struct i2c_client *client) {

	struct rfnm_bootconfig *cfg;

	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	rfnm_dcs_freq_hz = 122880000;
	rfnm_si5510_client = client;

	if(device_property_read_bool(&client->dev, "rfnm,skip-5510-init-quirk")) {
		cfg->pcie_clock_ready = 1;
		printk("RFNM: skip-5510-init-quirk\n");
		return 0;
	}
	// when rebooted without hard power reset, this memory section doesn't get inited to 0xff...
	// move memory reset to uboot?

	// remove pd negotiation workaround: it gets stuck sometimes (non-PD connected, times out)

	if(	//cfg->usb_pd_negotiation_in_progress == 1 || 
		cfg->daughterboard_present[0] == RFNM_DAUGHTERBOARD_NOT_CHECKED_YET || 
		cfg->daughterboard_present[1] == RFNM_DAUGHTERBOARD_NOT_CHECKED_YET) {
		printk("RFNM: Deferring Si5510 probe...\n");
		memunmap(cfg);
		return -EPROBE_DEFER;
	}

#if 0
	s64  uptime_ms;
    uptime_ms = ktime_to_ms(ktime_get_boottime());

	if(uptime_ms < 1000) {
		// complete hack: most PD devices are going to keep probing between 0.7-1 second, so do not start there...
		printk("RFNM: Deferring Si5510 probe...\n");
		return -EPROBE_DEFER;
	}
#endif
	printk("RFNM: Starting up Si5510...\n");

	si5510_rst_gpio = devm_gpiod_get(&client->dev, "si5510-rst", GPIOD_OUT_LOW);
	int error;

	if (IS_ERR(si5510_rst_gpio)) {
		error = PTR_ERR(si5510_rst_gpio);
		printk("RFNM: Failed to get enable gpio: %d\n", error);
		return error;
	}

	gpiod_set_value_cansleep(si5510_rst_gpio, 0);
	msleep(10);
	gpiod_set_value_cansleep(si5510_rst_gpio, 1);

	rfnm_si5510_cts(client);

	rfnm_si5510_sio_test(client);

	/*int RFNM_SI5510_CMD_BUFFER_SIZE = */ rfnm_si5510_buffsize(client);

	rfnm_si5510_restart(client);

	rfnm_si5510_host_load(client, prod_fw_boot_bin, prod_fw_boot_bin_len);
	// fw >= 1.4 requires the ROM patch alongside the firmware (Skyworks export README;
	// load order between the boot files does not matter, only that all load before BOOT)
	rfnm_si5510_host_load(client, patch_rom_boot_bin, patch_rom_boot_bin_len);
	rfnm_si5510_host_load(client, Base_Plan_boot_bin, Base_Plan_boot_bin_len);

	rfnm_si5510_boot(client);

	/*int dcs_map_offset = -1;
repeat_search:
	for(i = 0; i < RFNM_NUM_DCS_FREQ; i++) {
		if(rfnm_si5510_plan_map[i][1] == cfg->user_eeprom.dcs_clk_tmp) {
			dcs_map_offset = i;
			printk("RFNM: DCS clock is 11673.6 MHz / %d\n", rfnm_si5510_plan_map[i][0]);
			break;
		}
	}

	if(dcs_map_offset < 0) {
		printk("RFNM: DCS clock not set in eeprom, defaulting to 122...\n");
		cfg->user_eeprom.dcs_clk_tmp = 122;
		goto repeat_search;
	}*/

	
	if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_YUCCA, RFNM_DAUGHTERBOARD_YUCCA)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 1);
		rfnm_si5510_host_load(client, Q_Plan1_boot_bin, Q_Plan1_boot_bin_len);
		printk("RFNM: Selected plan 1 RFNM_DAUGHTERBOARD_YUCCA, RFNM_DAUGHTERBOARD_YUCCA\n");

		// this yucca plan does not take all dgb combinations into account! 



	} else if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_GRANITA, RFNM_DAUGHTERBOARD_GRANITA)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 1);
		rfnm_si5510_host_load(client, Q_Plan1_boot_bin, Q_Plan1_boot_bin_len);
		printk("RFNM: Selected plan 1 RFNM_DAUGHTERBOARD_GRANITA, RFNM_DAUGHTERBOARD_GRANITA\n");
	} else if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_LIME, RFNM_DAUGHTERBOARD_LIME)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 2);
		rfnm_si5510_host_load(client, Q_Plan2_boot_bin, Q_Plan2_boot_bin_len);
		printk("RFNM: Selected plan 2 RFNM_DAUGHTERBOARD_LIME, RFNM_DAUGHTERBOARD_LIME\n");
	} else if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_GRANITA, RFNM_DAUGHTERBOARD_LIME)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 3);
		rfnm_si5510_host_load(client, Q_Plan3_boot_bin, Q_Plan3_boot_bin_len);
		printk("RFNM: Selected plan 3 RFNM_DAUGHTERBOARD_GRANITA, RFNM_DAUGHTERBOARD_LIME\n");
	} else if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_LIME, RFNM_DAUGHTERBOARD_GRANITA)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 4);
		rfnm_si5510_host_load(client, Q_Plan4_boot_bin, Q_Plan4_boot_bin_len);
		printk("RFNM: Selected plan 4 RFNM_DAUGHTERBOARD_LIME, RFNM_DAUGHTERBOARD_GRANITA\n");
	} else if(can_use_si5510_config(cfg, RFNM_DAUGHTERBOARD_BREAKOUT, RFNM_DAUGHTERBOARD_BREAKOUT)) {
		//rfnm_si5510_load_from_map(client, dcs_map_offset, 1);
		rfnm_si5510_host_load(client, Q_Plan1_boot_bin, Q_Plan1_boot_bin_len);
		printk("RFNM: Breakout board detected: Selected plan 1 RFNM_DAUGHTERBOARD_GRANITA, RFNM_DAUGHTERBOARD_GRANITA\n");
	} else {
		printk("RFNM: Couldn't find Si5510 config to work with the installed daughterboards\n");
	}

	if(cfg->daughterboard_present[0] == RFNM_DAUGHTERBOARD_PRESENT && cfg->daughterboard_eeprom[0].board_id != RFNM_DAUGHTERBOARD_BREAKOUT) {
		rfnm_si5510_set_output_status(client, 15, 1);
		if(cfg->daughterboard_eeprom[0].board_id != RFNM_DAUGHTERBOARD_LIME) {
			rfnm_si5510_set_output_status(client, 11, 1);
		}
		printk("RFNM: Enabling clocks for RBA\n");
	}

	if(cfg->daughterboard_present[1] == RFNM_DAUGHTERBOARD_PRESENT && cfg->daughterboard_eeprom[1].board_id != RFNM_DAUGHTERBOARD_BREAKOUT) {
		rfnm_si5510_set_output_status(client, 2, 1);
		if(cfg->daughterboard_eeprom[1].board_id != RFNM_DAUGHTERBOARD_LIME) {
			rfnm_si5510_set_output_status(client, 0, 1);
		}
		printk("RFNM: Enabling clocks for RBB\n");
	}


	// enable output 6 by default (GPT1 timer)
	rfnm_si5510_set_output_status(client, 6, 1);
	// enable output 8 by default (GPT3 timer)
	rfnm_si5510_set_output_status(client, 8, 1);
	


	printk("RFNM: Waiting for reference clock to lock...\n");

	while(!rfnm_si5510_reference_status(client)) {
		msleep(10);
	}

	printk("RFNM: Si5510 is ready and providing a PCIe clock!\n");

	cfg->pcie_clock_ready = 1;

	error = rfnm_si5510_get_la9310_gpios(&client->dev);
	if(error) {
		return error;
	}

	error = rfnm_si5510_reset_la9310(rfnm_dcs_freq_hz);
	if(error) {
		return error;
	}

	// cannot load wsled because it's not init'd yet... not sure why the order changed
	//rfnm_wsled_set(0, 0, 0, 0, 0xff);
	//rfnm_wsled_send_chain(0);

	int err;

	err = device_create_file(&client->dev, &dev_attr_rfnm_ext_ref_out);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_ext_ref_out");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_set_dcs_freq);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_set_dcs_freq");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_set_dco_ppb);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_set_dco_ppb");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_set_dco_steps);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_set_dco_steps");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_reset_la9310);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_reset_la9310");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_set_siggen_freq);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_set_siggen_freq");
	}
	//device_remove_file(&client->dev, &(dev_attr_rfnm_show_board_info));

	return 0;

}



static const struct of_device_id rfnm_si5510_match_table[] = {
	{ .compatible = "rfnm,si5510", },
	{}
};
MODULE_DEVICE_TABLE(of, rfnm_si5510_match_table);

static const struct i2c_device_id rfnm_si5510_id_table[] = {
	{ "rfnm_si5510", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rfnm_si5510_id_table);

static struct i2c_driver rfnm_si5510_driver = {
	.driver	= {
		.name	= "rfnm_si5510",
		.of_match_table = rfnm_si5510_match_table,
	},
	.probe	= rfnm_si5510_probe,
	.id_table	= rfnm_si5510_id_table,
};
//module_i2c_driver(rfnm_si5510_driver);


static int __init rfnm_si5510_init(void)
{
    return i2c_add_driver(&rfnm_si5510_driver);
}
late_initcall(rfnm_si5510_init);

MODULE_LICENSE("GPL");
