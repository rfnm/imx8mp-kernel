#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <linux/i2c.h>
#include <linux/printk.h>

#include <linux/crc32.h>
#include <linux/rfnm-shared.h>


typedef unsigned char       uint8_t;
typedef   signed char        int8_t;

/*
	EEPROM memory layout (256 bytes, 32 pages of 8)

	factory half, 0x00-0x7F (struct rfnm_eeprom_data at 0x00, written by rfnm_factory_use_only only):
	0x00: 4 bytes magic header 0x34 0x26 0x94 0x12
	0x0a: 6 byte mac address (motherboard only)
	0x10: 1 byte board id
	0x11: 1 byte board revision id
	0x12-0x16: free space
	0x17: 9 byte board serial number (8 + null)
	0x20: 4 byte crc32

	user half, 0x80-0xFF (struct rfnm_eeprom_user_config at 0x80, motherboard adapter 0 only,
	kernel-owned: userspace writes individual settings via rfnm_settings/<name> sysfs files and
	never raw bytes; eeprom writes are hard-bounded to this half so the factory data can never
	be touched from userspace):
	0x80: 4 bytes magic 'R' 'F' 'U' 'C', 1 byte version, 1 byte pad
	0x86: 118 byte bit-packed settings payload (map: rfnm_user_settings[] / rfnm_settings/map)
	0xFC: 4 byte crc32 over 0x80-0xFB
*/


// ONE combined write-address + repeated-start read for a whole block. The old shape
// (per-byte i2c_master_send + UNCHECKED i2c_master_recv, ~80 bus lock/unlock cycles
// per board-info read, the recv fired even after a NAKed send) was the defect #76
// amplifier: an empty DGB slot turned every sysfs read into an 80-transaction NAK
// storm against the controller's error paths.
static int rfnm_bootconfig_read_block(struct i2c_client *client, uint8_t addr, uint8_t *buf, uint16_t len) {

	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0, .len = 1, .buf = &addr },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if(ret < 0) {
		return ret;
	}
	return ret == 2 ? 0 : -EIO;
}

void rfnm_bootconfig_read_eeprom(struct i2c_client *client, uint8_t addr, uint8_t * buf) {

	rfnm_bootconfig_read_block(client, addr, buf, 1);
}

int rfnm_bootconfig_write_eeprom(struct i2c_client *client, uint8_t addr, uint8_t val) {

	uint8_t write[2];
	write[0] = addr;
	write[1] = val;

	int retry_id = 0;
	int ret;

retry:
	ret = i2c_master_send(client, write, 2);

	if(ret < 0) {
		if(retry_id++ < 30) {
			// ack-poll pacing: the EEPROM NAKs while its internal write cycle runs
			// (tWR <= 5 ms). The old bare goto hammered the bus at full rate.
			usleep_range(500, 1000);
			goto retry;
		} else {
			return -1;
		}
	}

	return 0;
}

int rfnm_load_board_info(struct device *dev, struct rfnm_eeprom_data *eeprom_data) {

	struct i2c_client *client = to_i2c_client(dev);

	memset(eeprom_data, 0, sizeof(*eeprom_data));

	if(rfnm_bootconfig_read_block(client, 0, (uint8_t*) eeprom_data, sizeof(*eeprom_data))) {
		return -1;
	}

	if(eeprom_data->crc != crc32(0x80000000, (uint8_t*) eeprom_data, sizeof(*eeprom_data) - 4)) {
		return -1;
	}

	return 0;
}


/*
	User settings framework. The kernel owns the whole eeprom user half: userspace only sees one
	validated sysfs file per setting (rfnm_settings/<name>), so it can never corrupt the block.
	Settings are bit-packed into rfnm_eeprom_user_config.payload at the fixed positions declared
	in rfnm_user_settings[] below. APPEND ONLY: never move or resize a shipped entry; new
	settings take new bits (small ints in payload bytes 0..7, strings byte-aligned from byte 8).
*/

static const uint8_t rfnm_user_config_magic[4] = { 'R', 'F', 'U', 'C' };

struct rfnm_user_setting {
	const char *name;	// sysfs file name under rfnm_settings/
	uint16_t bit_off;	// position in payload[], lsb-first
	uint16_t bit_width;	// ints: 1..32; strings: bytes * 8, byte-aligned, last byte forced NUL
	uint8_t is_string;
	uint32_t def;		// int default when the eeprom block is missing/invalid
};

static const struct rfnm_user_setting rfnm_user_settings[RFNM_USER_SETTING_COUNT] = {
	[RFNM_USER_SETTING_DDNS_ENABLED]   = { "ddns_enabled",       0,       1, 0, 1 },	// default ON (2026-07-19): virgin boards self-register <serial>.rfnm.me; user 0 persists
	[RFNM_USER_SETTING_DDNS_PUBLIC]    = { "ddns_public",        1,       1, 0, 0 },
	[RFNM_USER_SETTING_LED_BRIGHTNESS] = { "led_brightness",     8,       3, 0, 7 },	// 0 = off .. 7 = max
	[RFNM_USER_SETTING_DDNS_NICKNAME]  = { "ddns_nickname",  8 * 8, 17 * 8, 1, 0 },
	// DCO warm-start cal (written by cellsyncd on stable TRACK, applied by si5510 at probe).
	// steps are stored biased (+100000) because this framework is unsigned; def = bias = 0 steps.
	[RFNM_USER_SETTING_DCO_CAL_VALID]  = { "dco_cal_valid",      2,       1, 0, 0 },
	[RFNM_USER_SETTING_DCO_CAL_STEPS]  = { "dco_cal_steps",     16,      18, 0, 100000 },
};

static DEFINE_MUTEX(rfnm_user_mtx);
static struct rfnm_bootconfig *rfnm_cfg;
static struct i2c_client *rfnm_user_client;					// adapter-0 client, set at probe
static struct rfnm_eeprom_user_config rfnm_user_block;				// runtime truth, always valid
static uint8_t rfnm_user_shadow[sizeof(struct rfnm_eeprom_user_config)];	// last known eeprom content

static uint32_t rfnm_user_bits_get(const uint8_t *payload, uint16_t off, uint16_t width) {

	uint32_t v = 0;
	int i;

	for(i = 0; i < width; i++) {
		v |= (uint32_t)((payload[(off + i) / 8] >> ((off + i) % 8)) & 1) << i;
	}

	return v;
}

static void rfnm_user_bits_set(uint8_t *payload, uint16_t off, uint16_t width, uint32_t val) {

	int i;

	for(i = 0; i < width; i++) {
		if((val >> i) & 1) {
			payload[(off + i) / 8] |= 1 << ((off + i) % 8);
		} else {
			payload[(off + i) / 8] &= ~(1 << ((off + i) % 8));
		}
	}
}

static int rfnm_user_config_valid(struct rfnm_eeprom_user_config *eeprom_data) {

	uint8_t *eeprom_data_ptr = (uint8_t*) eeprom_data;

	if(memcmp(eeprom_data->magic_header, rfnm_user_config_magic, 4)) {
		return 0;
	}

	if(eeprom_data->version != RFNM_USER_CONFIG_VERSION) {
		return 0;
	}

	if(eeprom_data->crc != crc32(0x80000000, eeprom_data_ptr, sizeof(*eeprom_data) - 4)) {
		return 0;
	}

	return 1;
}

static void rfnm_user_block_seal(struct rfnm_eeprom_user_config *b) {

	memcpy(b->magic_header, rfnm_user_config_magic, 4);
	b->version = RFNM_USER_CONFIG_VERSION;
	b->pad = 0;
	b->crc = crc32(0x80000000, (uint8_t*) b, sizeof(*b) - 4);
}

static void rfnm_user_block_defaults(struct rfnm_eeprom_user_config *b) {

	int i;

	memset(b, 0, sizeof(*b));

	for(i = 0; i < RFNM_USER_SETTING_COUNT; i++) {
		if(!rfnm_user_settings[i].is_string && rfnm_user_settings[i].def) {
			rfnm_user_bits_set(b->payload, rfnm_user_settings[i].bit_off, rfnm_user_settings[i].bit_width, rfnm_user_settings[i].def);
		}
	}

	rfnm_user_block_seal(b);
}

// write rfnm_user_block to the eeprom (changed bytes only, offsets 128..255 by construction so
// the factory half is unreachable), read back and verify, mirror into bootconfig. rfnm_user_mtx held.
static int rfnm_user_block_commit(void) {

	const uint8_t *p = (const uint8_t*) &rfnm_user_block;
	int i, ret, dirty = 0;

	for(i = 0; i < sizeof(rfnm_user_block); i++) {
		if(rfnm_user_shadow[i] == p[i]) {
			continue;
		}
		dirty = 1;
		ret = rfnm_bootconfig_write_eeprom(rfnm_user_client, 128 + i, p[i]);
		if(ret < 0) {
			printk("RFNM: user config eeprom write failed at byte %d\n", i);
			break;
		}
	}

	if(dirty) {
		msleep(10);	// let the last byte's internal write cycle (tWR) finish before reading back
	}

	// full read-back keeps the shadow honest even after a failed/partial write
	if(rfnm_bootconfig_read_block(rfnm_user_client, 128, rfnm_user_shadow, sizeof(rfnm_user_block))) {
		printk("RFNM: user config eeprom read-back failed\n");
		return -EIO;
	}

	if(memcmp(rfnm_user_shadow, p, sizeof(rfnm_user_block))) {
		printk("RFNM: user config eeprom verify failed\n");
		return -EIO;
	}

	if(rfnm_cfg) {
		memcpy(&rfnm_cfg->user_eeprom, &rfnm_user_block, sizeof(rfnm_user_block));
	}

	return 0;
}

int rfnm_user_setting_get(enum rfnm_user_setting_id id, uint32_t *val) {

	if(id >= RFNM_USER_SETTING_COUNT || rfnm_user_settings[id].is_string) {
		return -EINVAL;
	}

	if(!rfnm_user_client) {
		return -EAGAIN;
	}

	mutex_lock(&rfnm_user_mtx);
	*val = rfnm_user_bits_get(rfnm_user_block.payload, rfnm_user_settings[id].bit_off, rfnm_user_settings[id].bit_width);
	mutex_unlock(&rfnm_user_mtx);

	return 0;
}
EXPORT_SYMBOL(rfnm_user_setting_get);

int rfnm_user_setting_get_str(enum rfnm_user_setting_id id, char *buf, size_t buflen) {

	const struct rfnm_user_setting *s;
	size_t len;

	if(id >= RFNM_USER_SETTING_COUNT || !rfnm_user_settings[id].is_string) {
		return -EINVAL;
	}

	if(!rfnm_user_client) {
		return -EAGAIN;
	}

	s = &rfnm_user_settings[id];
	len = s->bit_width / 8;
	if(buflen < len) {
		return -EINVAL;
	}

	mutex_lock(&rfnm_user_mtx);
	memcpy(buf, &rfnm_user_block.payload[s->bit_off / 8], len);
	mutex_unlock(&rfnm_user_mtx);
	buf[len - 1] = 0;

	return 0;
}
EXPORT_SYMBOL(rfnm_user_setting_get_str);

struct rfnm_user_attr {
	struct device_attribute dattr;
	enum rfnm_user_setting_id id;
};

static ssize_t rfnm_user_setting_show(struct device *dev, struct device_attribute *attr, char *buf) {

	struct rfnm_user_attr *ua = container_of(attr, struct rfnm_user_attr, dattr);
	const struct rfnm_user_setting *s = &rfnm_user_settings[ua->id];
	ssize_t ret;

	mutex_lock(&rfnm_user_mtx);
	if(s->is_string) {
		char tmp[64];
		size_t len = min_t(size_t, s->bit_width / 8, sizeof(tmp));
		memcpy(tmp, &rfnm_user_block.payload[s->bit_off / 8], len);
		tmp[len - 1] = 0;
		ret = snprintf(buf, PAGE_SIZE, "%s\n", tmp);
	} else {
		ret = snprintf(buf, PAGE_SIZE, "%u\n", rfnm_user_bits_get(rfnm_user_block.payload, s->bit_off, s->bit_width));
	}
	mutex_unlock(&rfnm_user_mtx);

	return ret;
}

static ssize_t rfnm_user_setting_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct rfnm_user_attr *ua = container_of(attr, struct rfnm_user_attr, dattr);
	const struct rfnm_user_setting *s = &rfnm_user_settings[ua->id];
	int ret;

	if(s->is_string) {
		size_t maxlen = s->bit_width / 8 - 1;	// last byte is the forced NUL
		size_t len = count, i;
		char tmp[64];

		while(len && (buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
			len--;
		}
		if(len > maxlen || len >= sizeof(tmp)) {
			return -EINVAL;
		}
		for(i = 0; i < len; i++) {
			if(buf[i] <= ' ' || buf[i] > '~') {	// printable, no whitespace
				return -EINVAL;
			}
		}
		memset(tmp, 0, sizeof(tmp));
		memcpy(tmp, buf, len);

		mutex_lock(&rfnm_user_mtx);
		memset(&rfnm_user_block.payload[s->bit_off / 8], 0, s->bit_width / 8);
		memcpy(&rfnm_user_block.payload[s->bit_off / 8], tmp, len);
	} else {
		uint32_t val;

		if(kstrtou32(buf, 0, &val)) {
			return -EINVAL;
		}
		if(s->bit_width < 32 && val >= (1U << s->bit_width)) {
			return -EINVAL;	// e.g. led_brightness is 3 bits: 0..7
		}

		mutex_lock(&rfnm_user_mtx);
		rfnm_user_bits_set(rfnm_user_block.payload, s->bit_off, s->bit_width, val);
	}

	rfnm_user_block_seal(&rfnm_user_block);
	ret = rfnm_user_block_commit();
	mutex_unlock(&rfnm_user_mtx);

	return ret ? ret : count;
}

// bit -> name map, so userspace/tooling can discover the layout without reading driver source
static ssize_t map_show(struct device *dev, struct device_attribute *attr, char *buf) {

	ssize_t n = 0;
	int i;

	for(i = 0; i < RFNM_USER_SETTING_COUNT; i++) {
		n += snprintf(buf + n, PAGE_SIZE - n, "%s bit %u width %u %s default %u\n",
			rfnm_user_settings[i].name, rfnm_user_settings[i].bit_off, rfnm_user_settings[i].bit_width,
			rfnm_user_settings[i].is_string ? "string" : "int", rfnm_user_settings[i].def);
	}

	return n;
}
static DEVICE_ATTR_RO(map);

static ssize_t raw_show(struct device *dev, struct device_attribute *attr, char *buf) {

	mutex_lock(&rfnm_user_mtx);
	bin2hex(buf, (uint8_t*) &rfnm_user_block, sizeof(rfnm_user_block));
	mutex_unlock(&rfnm_user_mtx);
	buf[2 * sizeof(rfnm_user_block)] = '\n';

	return 2 * sizeof(rfnm_user_block) + 1;
}
static DEVICE_ATTR_RO(raw);

static struct rfnm_user_attr rfnm_user_attrs[RFNM_USER_SETTING_COUNT];
static struct attribute *rfnm_user_attr_ptrs[RFNM_USER_SETTING_COUNT + 3];	// + map + raw + NULL

static const struct attribute_group rfnm_user_group = {
	.name = "rfnm_settings",
	.attrs = rfnm_user_attr_ptrs,
};

// read the eeprom user half at probe: valid block -> runtime truth, anything else -> defaults
static void rfnm_user_config_load(struct i2c_client *client) {

	char nick[17];

	if(rfnm_bootconfig_read_block(client, 128, rfnm_user_shadow, sizeof(rfnm_user_shadow))) {
		memset(rfnm_user_shadow, 0xff, sizeof(rfnm_user_shadow));	// unreadable = invalid block -> defaults
	}

	memcpy(&rfnm_user_block, rfnm_user_shadow, sizeof(rfnm_user_block));

	if(rfnm_user_config_valid(&rfnm_user_block)) {
		rfnm_user_client = client;
		rfnm_user_setting_get_str(RFNM_USER_SETTING_DDNS_NICKNAME, nick, sizeof(nick));
		printk("RFNM: User config loaded, nickname '%s'\n", nick);
	} else {
		rfnm_user_block_defaults(&rfnm_user_block);
		rfnm_user_client = client;
		printk("RFNM: User config not present, using defaults\n");
	}

	if(rfnm_cfg) {
		memcpy(&rfnm_cfg->user_eeprom, &rfnm_user_block, sizeof(rfnm_user_block));
	}
}


// Serve the PROBE-TIME identity, never the wire. Defect #76: the DGB buses are
// i2c ONLY during the boot quiet-window - once the radio stack runs, the M7 owns
// the I2C3_SDA ball as its GPT3 clock input (pin_mux.c in the M7 fw; board design),
// so a live read on that bus is electrically meaningless and killed the reader
// (external abort/stuck-busy -> reader dies holding the adapter rt_mutex -> board
// wedge; full receipts in the ledger). Identity cannot change after boot anyway.
static ssize_t rfnm_show_board_info_show(struct device *dev, struct device_attribute *attr, char *buf) {

	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_adapter *adapter = client->adapter;
	int adapter_nr = i2c_adapter_id(adapter);
	struct rfnm_eeprom_data eeprom_data;

	if(!rfnm_cfg) {
		return snprintf(buf, PAGE_SIZE, "Failed to read eeprom\n");
	}
	if(adapter_nr) {
		if(rfnm_cfg->daughterboard_present[adapter_nr - 1] != RFNM_DAUGHTERBOARD_PRESENT) {
			return snprintf(buf, PAGE_SIZE, "Failed to read eeprom\n");
		}
		memcpy(&eeprom_data, &rfnm_cfg->daughterboard_eeprom[adapter_nr - 1], sizeof(eeprom_data));
	} else {
		memcpy(&eeprom_data, &rfnm_cfg->motherboard_eeprom, sizeof(eeprom_data));
	}
	{
		if(adapter_nr) {
			return snprintf(buf, PAGE_SIZE, "board id %d revision %d serial %s\n", eeprom_data.board_id, eeprom_data.board_revision_id, eeprom_data.serial_number);
		} else {
			return snprintf(buf, PAGE_SIZE, "board id %d revision %d serial %s mac-addr %02x:%02x:%02x:%02x:%02x:%02x\n", 
						eeprom_data.board_id, eeprom_data.board_revision_id, eeprom_data.serial_number,
						eeprom_data.mac_addr[0], eeprom_data.mac_addr[1], eeprom_data.mac_addr[2], eeprom_data.mac_addr[3], eeprom_data.mac_addr[4], eeprom_data.mac_addr[5]);
		}

		
	}
}

static DEVICE_ATTR_RO(rfnm_show_board_info);

static ssize_t rfnm_factory_use_only_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {

	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_adapter *adapter = client->adapter;
	int adapter_nr = i2c_adapter_id(adapter);

	//01-02-abcdefgh
	//01-02-abcdefgh-00:00:00:00:00:00

	if(adapter_nr) {
		if(count != 15) {
			//printk("RFNM: Invalid length at %d\n", count);
			return -EINVAL;
		}
	} else {
		if(count != (15 + 18)) {
			return -EINVAL;
		}

		if(buf[14] != '-' || buf[17] != ':' || buf[20] != ':' || buf[23] != ':' || buf[26] != ':' || buf[29] != ':') {
			return -EINVAL;
		}
	}
	
	if(buf[2] != '-' || buf[5] != '-') {
		//printk("RFNM: invalid chars in string %c %c\n", &buf[2], &buf[5]);
		return -EINVAL;
	}

	const char __user tmpstr[20];
	uint32_t tmpval;
	struct rfnm_eeprom_data eeprom_data;
	int i, ret;

	memset((uint8_t*) &eeprom_data, 0, sizeof(eeprom_data));

	eeprom_data.magic_header[0] = 0x34;
	eeprom_data.magic_header[1] = 0x26;
	eeprom_data.magic_header[2] = 0x94;
	eeprom_data.magic_header[3] = 0x12;

	memset(&tmpstr, 0, 10);
	memcpy(&tmpstr, &buf[0], 2);

    if (kstrtou32(&tmpstr[0], 10, &tmpval) != 0) {
		//printk("RFNM: invalid digit at pos 1");
		return -EINVAL;
	}

	eeprom_data.board_id = tmpval;

	memset(&tmpstr, 0, 10);
	memcpy(&tmpstr, &buf[3], 2);

    if (kstrtou32(&tmpstr[0], 10, &tmpval) != 0) {
		//printk("RFNM: invalid digit at pos 2");
		return -EINVAL;
	}

	eeprom_data.board_revision_id = tmpval;

	memcpy(&eeprom_data.serial_number, &buf[6], 8);
	eeprom_data.serial_number[8] = 0;

	if(!adapter_nr) {
		memset(&tmpstr, 0, 20);
		memcpy(&tmpstr, &buf[15], 17);
		sscanf(&tmpstr[0], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
				&eeprom_data.mac_addr[0], &eeprom_data.mac_addr[1], &eeprom_data.mac_addr[2], 
				&eeprom_data.mac_addr[3], &eeprom_data.mac_addr[4], &eeprom_data.mac_addr[5]);
	}

	//printk("RFNM: board id %d revision %d serial %s\n", eeprom_data.board_id, eeprom_data.board_revision_id, eeprom_data.serial_number);

	uint8_t *eeprom_data_ptr;
	eeprom_data_ptr = (uint8_t*) &eeprom_data;

	eeprom_data.crc = crc32(0x80000000, eeprom_data_ptr, sizeof(eeprom_data) - 4);
	
	for(i = 0; i < sizeof(eeprom_data); i++) {
		//printk("RFNM: %d %02x\n", i, eeprom_data_ptr[i]);
		ret = rfnm_bootconfig_write_eeprom(client, i, *(eeprom_data_ptr + i));
		if(ret < 0) {
			printk("RFNM: Write to daughterboard failed");
			return -ENODEV;
		}
	}

	return count;
}

static DEVICE_ATTR_WO(rfnm_factory_use_only);


static int rfnm_bootconfig_probe(struct i2c_client *client) {

	struct i2c_adapter *adapter = client->adapter;
	
	struct rfnm_bootconfig *cfg;
	struct rfnm_eeprom_data *eeprom_data;
	cfg = memremap(RFNM_BOOTCONFIG_PHYADDR, SZ_4M, MEMREMAP_WB);

	if(adapter->nr == 0) {
		rfnm_cfg = cfg;

		eeprom_data = &cfg->motherboard_eeprom;
		if(!rfnm_load_board_info(&client->dev, eeprom_data)) {
			printk("RFNM: Motherboard id %d revision %d serial %s mac-addr %02x:%02x:%02x:%02x:%02x:%02x\n",
			eeprom_data->board_id, eeprom_data->board_revision_id, eeprom_data->serial_number,
			eeprom_data->mac_addr[0], eeprom_data->mac_addr[1], eeprom_data->mac_addr[2], eeprom_data->mac_addr[3], eeprom_data->mac_addr[4], eeprom_data->mac_addr[5]);
		}

		rfnm_user_config_load(client);
	} else {
		eeprom_data = &cfg->daughterboard_eeprom[adapter->nr - 1];
		if(!rfnm_load_board_info(&client->dev, eeprom_data)) {
			printk("RFNM: Daughterboard detected on slot %d, board id %d revision %d serial %s\n", adapter->nr, eeprom_data->board_id, eeprom_data->board_revision_id, eeprom_data->serial_number);
			cfg->daughterboard_present[adapter->nr - 1] = RFNM_DAUGHTERBOARD_PRESENT;
		} else {
			cfg->daughterboard_present[adapter->nr - 1] = RFNM_DAUGHTERBOARD_NOT_FOUND;
		}
	}

	

	int err;

	err = device_create_file(&client->dev, &dev_attr_rfnm_show_board_info);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_show_board_info");
	}

	err = device_create_file(&client->dev, &dev_attr_rfnm_factory_use_only);
	if (err < 0) {
		printk("RFNM: failed to create device file for rfnm_factory_use_only");
	}

	if(adapter->nr == 0) {
		int i;

		for(i = 0; i < RFNM_USER_SETTING_COUNT; i++) {
			sysfs_attr_init(&rfnm_user_attrs[i].dattr.attr);
			rfnm_user_attrs[i].dattr.attr.name = rfnm_user_settings[i].name;
			rfnm_user_attrs[i].dattr.attr.mode = 0644;
			rfnm_user_attrs[i].dattr.show = rfnm_user_setting_show;
			rfnm_user_attrs[i].dattr.store = rfnm_user_setting_store;
			rfnm_user_attrs[i].id = i;
			rfnm_user_attr_ptrs[i] = &rfnm_user_attrs[i].dattr.attr;
		}
		rfnm_user_attr_ptrs[RFNM_USER_SETTING_COUNT] = &dev_attr_map.attr;
		rfnm_user_attr_ptrs[RFNM_USER_SETTING_COUNT + 1] = &dev_attr_raw.attr;
		rfnm_user_attr_ptrs[RFNM_USER_SETTING_COUNT + 2] = NULL;

		err = sysfs_create_group(&client->dev.kobj, &rfnm_user_group);
		if (err < 0) {
			printk("RFNM: failed to create rfnm_settings sysfs group");
		}
	}

	

	return 0;
}

static void rfnm_bootconfig_remove(struct i2c_client *client) {
	struct i2c_adapter *adapter = client->adapter;

	device_remove_file(&client->dev, &(dev_attr_rfnm_show_board_info));
	device_remove_file(&client->dev, &(dev_attr_rfnm_factory_use_only));

	if(adapter->nr == 0) {
		sysfs_remove_group(&client->dev.kobj, &rfnm_user_group);
		rfnm_user_client = NULL;
	}
}







static const struct of_device_id rfnm_bootconfig_match_table[] = {
	{ .compatible = "rfnm,bootconfig", },
	{}
};
MODULE_DEVICE_TABLE(of, rfnm_bootconfig_match_table);

static const struct i2c_device_id rfnm_bootconfig_id_table[] = {
	{ "rfnm_bootconfig", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rfnm_bootconfig_id_table);

static struct i2c_driver rfnm_bootconfig_driver = {
	.driver	= {
		.name	= "rfnm_bootconfig",
		.of_match_table = rfnm_bootconfig_match_table,
	},
	.remove     = rfnm_bootconfig_remove,
	.probe	= rfnm_bootconfig_probe,
	.id_table	= rfnm_bootconfig_id_table,
};
module_i2c_driver(rfnm_bootconfig_driver);
MODULE_LICENSE("GPL");