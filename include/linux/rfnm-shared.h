#ifndef INCLUDE_LINUX_RFNM_SHARED_H_
#define INCLUDE_LINUX_RFNM_SHARED_H_

#define RFNM_DAUGHTERBOARD_BREAKOUT (1)
#define RFNM_DAUGHTERBOARD_GRANITA (2)
#define RFNM_DAUGHTERBOARD_LIME (3)

#define RFNM_SLOT_PRIMARY (0)
#define RFNM_SLOT_SECONDARY (1)

#define RFNM_DAUGHTERBOARD_PRESENT (0x10)
#define RFNM_DAUGHTERBOARD_NOT_FOUND (0x20)
#define RFNM_DAUGHTERBOARD_NOT_CHECKED_YET (0xff)

#define RFNM_BOOTCONFIG_PHYADDR (0xA3400000)

#define RFNM_PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))



struct rfnm_dgb_tx_ch {
	int freq;
	int freq_max;
	int freq_min;
	int power;
	int dac_id;
	int ch_id;
	
	void * ch_get;
	void * ch_set;
};

struct rfnm_dgb_rx_ch {
	int freq;
	int freq_max;
	int freq_min;
	int gain;
	int adc_id;
	int ch_id;
	
	void * ch_get;
	void * ch_set;
};

struct rfnm_dgb {
	struct rfnm_dgb_rx_ch rx_ch[4];
	struct rfnm_dgb_tx_ch tx_ch[4];
	int rx_ch_cnt;
	int tx_ch_cnt;
	uint8_t board_id;
	uint8_t board_revision_id;
	uint8_t serial_number[9];
	struct rfnm_dgb_dt *rfnm_dgb_dt;
};


struct __attribute__((__packed__)) rfnm_eeprom_data {
	uint8_t magic_header[4];
	uint8_t pad1[12];
	uint8_t board_id;
	uint8_t board_revision_id;
	uint8_t pad2[5];
	uint8_t serial_number[9];
	uint32_t crc;
};

// 0xff initial status is only guaranteed by uboot mod in the first 4kB
struct __attribute__((__packed__)) rfnm_bootconfig {
	uint8_t daughterboard_present[2];
  	uint8_t pcie_clock_ready;
	uint8_t usb_pd_negotiation_in_progress;
	struct rfnm_eeprom_data motherboard_eeprom;
	struct rfnm_eeprom_data daughterboard_eeprom[2];
};



















//#define RFNM_MAX_TRX_CH_CNT (8)


enum rfnm_ch_data_type {
	RFNM_CH_DATA_TYPE_COMPLEX,
	RFNM_CH_DATA_TYPE_REAL
};

enum rfnm_agc_type {
    RFNM_AGC_OFF,
    RFNM_AGC_DEFAULT
};

enum rfnm_bias_tee {
    RFNM_BIAS_TEE_OFF,
    RFNM_BIAS_TEE_ON
};

enum rfnm_rf_path {
	RFNM_PATH_SMA_A,
	RFNM_PATH_SMA_B,
	RFNM_PATH_SMA_C,
	RFNM_PATH_SMA_D,
	RFNM_PATH_SMA_E,
	RFNM_PATH_SMA_F,
	RFNM_PATH_SMA_G,
	RFNM_PATH_SMA_H,
    RFNM_PATH_EMBED_ANT,
    RFNM_PATH_LOOPBACK,
	RFNM_PATH_NULL
};



RFNM_PACKED_STRUCT(
    struct rfnm_api_tx_ch {
		int8_t abs_id;
		int8_t dgb_id;
        int64_t freq_min;
        int64_t freq_max;
        int64_t freq_cur;
        int16_t samp_freq_div_m;
        int16_t samp_freq_div_n;
        int8_t avail;
        int8_t active;
        int8_t power;
		enum rfnm_bias_tee bias_tee;
		enum rfnm_rf_path path_active;
		enum rfnm_rf_path path_possible[10];		
		enum rfnm_ch_data_type data_type;
    }
);

RFNM_PACKED_STRUCT(
    struct rfnm_api_rx_ch {
		int8_t abs_id;
		int8_t dgb_id;
        int64_t freq_min;
        int64_t freq_max;
        int64_t freq_cur;
        int16_t samp_freq_div_m;
        int16_t samp_freq_div_n;
        int8_t avail;
        int8_t active;
        int8_t gain;
        enum rfnm_agc_type agc;
		enum rfnm_bias_tee bias_tee;
        enum rfnm_rf_path path_active;
		enum rfnm_rf_path path_possible[10];		
		enum rfnm_ch_data_type data_type;
    }
);

RFNM_PACKED_STRUCT(
	struct rfnm_dev_hwinfo_bit {
		uint8_t board_id;
		uint8_t board_revision_id;
		uint8_t serial_number[9];
		char user_readable_name[30];
		uint8_t mac_addr[6];
		uint8_t tx_ch_cnt;
		uint8_t rx_ch_cnt;
		int16_t temperature;
	}
);

/*RFNM_PACKED_STRUCT(
	struct rfnm_dev_hwinfo_clockgen {
		uint32_t dcs_clk;
	}
)*/

RFNM_PACKED_STRUCT(
	struct rfnm_dev_hwinfo {
		struct rfnm_dev_hwinfo_bit motherboard;
		struct rfnm_dev_hwinfo_bit daughterboard[2];
	//	struct rfnm_dev_hwinfo_clockgen clock;
	}
);

RFNM_PACKED_STRUCT(
	struct rfnm_dev_tx_ch_list {
		int cnt;
		struct rfnm_api_tx_ch tx_ch[8];
	}
);

RFNM_PACKED_STRUCT(
	struct rfnm_dev_rx_ch_list {
		int cnt;
		struct rfnm_api_rx_ch rx_ch[8];
	}
);

enum rfnm_control_ep {
    RFNM_GET_DEV_HWINFO = 0xf00,
    RFNM_GET_TX_CH_LIST,
	RFNM_SET_TX_CH_LIST,
	RFNM_GET_RX_CH_LIST,
	RFNM_SET_RX_CH_LIST
};



void rfnm_dgb_reg_rx_ch(int dgb_slot, struct rfnm_dgb_rx_ch * rx_ch);
void rfnm_dgb_reg_tx_ch(int dgb_slot, struct rfnm_dgb_tx_ch * tx_ch);
void rfnm_dgb_reg(struct rfnm_dgb_dt *dgb_dt, int dgb_slot, int board_id, int board_revision_id, uint8_t serial_number[9]);
void rfnm_dgb_unreg(int dgb_slot);


void rfnm_populate_dev_hwinfo(struct rfnm_dev_hwinfo *r_hwinfo);


struct rfnm_dgb_dt {
	struct device dev;
	void *priv;
	int daughterboard_id;
	void *priv_drv;
};
















#define R_DBG_S_PRI_BANK 0
#define R_DBG_S_PRI_NUM 8
#define R_DBG_S_SEC_BANK 16
#define R_DBG_S_SEC_NUM 24

#define RFNM_DGB_GPIO4_0 ((4 << R_DBG_S_PRI_BANK) | (0 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (10 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_1 ((4 << R_DBG_S_PRI_BANK) | (1 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (11 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_2 ((4 << R_DBG_S_PRI_BANK) | (2 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (12 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_3 ((4 << R_DBG_S_PRI_BANK) | (3 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (13 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_4 ((4 << R_DBG_S_PRI_BANK) | (4 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (14 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_5 ((4 << R_DBG_S_PRI_BANK) | (5 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (15 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_6 ((4 << R_DBG_S_PRI_BANK) | (6 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (16 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_7 ((4 << R_DBG_S_PRI_BANK) | (7 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (17 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO5_16 ((5 << R_DBG_S_PRI_BANK) | (16 << R_DBG_S_PRI_NUM) | (5 << R_DBG_S_SEC_BANK) | (17 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO3_21 ((3 << R_DBG_S_PRI_BANK) | (21 << R_DBG_S_PRI_NUM) | (5 << R_DBG_S_SEC_BANK) | (2 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_8 ((4 << R_DBG_S_PRI_BANK) | (8 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (18 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_9 ((4 << R_DBG_S_PRI_BANK) | (9 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (19 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO2_3 ((2 << R_DBG_S_PRI_BANK) | (3 << R_DBG_S_PRI_NUM) | (2 << R_DBG_S_SEC_BANK) | (1 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO2_2 ((2 << R_DBG_S_PRI_BANK) | (2 << R_DBG_S_PRI_NUM) | (2 << R_DBG_S_SEC_BANK) | (0 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO3_22 ((3 << R_DBG_S_PRI_BANK) | (22 << R_DBG_S_PRI_NUM) | (3 << R_DBG_S_SEC_BANK) | (24 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO3_23 ((3 << R_DBG_S_PRI_BANK) | (23 << R_DBG_S_PRI_NUM) | (3 << R_DBG_S_SEC_BANK) | (25 << R_DBG_S_SEC_NUM))


void rfnm_gpio_set(uint8_t dgb_id, uint32_t gpio_map_id);
void rfnm_gpio_clear(uint8_t dgb_id, uint32_t gpio_map_id);
void rfnm_gpio_output(uint8_t dgb_id, uint32_t gpio_map_id);


#endif
