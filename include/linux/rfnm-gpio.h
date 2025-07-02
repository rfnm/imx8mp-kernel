#ifndef INCLUDE_LINUX_RFNM_GPIO_H_
#define INCLUDE_LINUX_RFNM_GPIO_H_


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

#define RFNM_DGB_GPIO4_22 ((4 << R_DBG_S_PRI_BANK) | (22 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (22 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_23 ((4 << R_DBG_S_PRI_BANK) | (23 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (23 << R_DBG_S_SEC_NUM))
#define RFNM_DGB_GPIO4_31 ((4 << R_DBG_S_PRI_BANK) | (31 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_SEC_BANK) | (31 << R_DBG_S_SEC_NUM))

#define RFNM_GPIO4_26 ((4 << R_DBG_S_PRI_BANK) | (26 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_PRI_BANK) | (26 << R_DBG_S_PRI_NUM))
#define RFNM_GPIO4_30 ((4 << R_DBG_S_PRI_BANK) | (30 << R_DBG_S_PRI_NUM) | (4 << R_DBG_S_PRI_BANK) | (30 << R_DBG_S_PRI_NUM))

// bit order is inverted in LA?
#define RFNM_DGB_LA_FE_CLK ((6 << R_DBG_S_PRI_BANK) | (24 << R_DBG_S_PRI_NUM) | (6 << R_DBG_S_SEC_BANK) | (23 << R_DBG_S_SEC_NUM))

#define RFNM_GPIO4_RB_3V3 ((1 << R_DBG_S_PRI_BANK) | (6 << R_DBG_S_PRI_NUM) | (1 << R_DBG_S_SEC_BANK) | (11 << R_DBG_S_SEC_NUM))
#define RFNM_GPIO4_RB_1V8_SHARED ((1 << R_DBG_S_PRI_BANK) | (5 << R_DBG_S_PRI_NUM) | (1 << R_DBG_S_SEC_BANK) | (5 << R_DBG_S_SEC_NUM))


void rfnm_gpio_set(uint8_t dgb_id, uint32_t gpio_map_id);
void rfnm_gpio_clear(uint8_t dgb_id, uint32_t gpio_map_id);
void rfnm_gpio_output(uint8_t dgb_id, uint32_t gpio_map_id);


#endif