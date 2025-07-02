#ifndef INCLUDE_LINUX_RFNM_VSPA_H_
#define INCLUDE_LINUX_RFNM_VSPA_H_

#define RFNM_RX_BUF_CNT 14


typedef uint32_t vspa_complex_fixed16;

#define FFT_SIZE 512
#define RFNM_LA9310_DMA_RX_SIZE		(256)
#define LA_RX_BASE_BUFSIZE (4*RFNM_LA9310_DMA_RX_SIZE)
#define LA_RX_BASE_BUFSIZE_12 ((LA_RX_BASE_BUFSIZE * 3) / 4)

#define RFNM_PACKED_STRUCT( __Declaration__ ) __Declaration__ __attribute__((__packed__))

RFNM_PACKED_STRUCT(
	struct structTXIQCompParams {
		float dcoff [2];
		float IQImb_ftaps [4];
	}
); 

RFNM_PACKED_STRUCT(
	struct rfnm_bufdesc_tx {
		vspa_complex_fixed16 buf[RFNM_LA9310_DMA_RX_SIZE];
		vspa_complex_fixed16 pad[RFNM_LA9310_DMA_RX_SIZE];/*
		uint32_t dac_id;
		uint32_t phytimer;
		uint32_t cc;
		uint32_t axiq_done;
		uint32_t iqcomp_done;
		// (64 - (4 * 5)) / 4 = 22
		uint32_t pad_to_64[11];*/
	}
); 
/*
RFNM_PACKED_STRUCT(
	struct rfnm_bufdesc_rx {
		vspa_complex_fixed16 buf[RFNM_LA9310_DMA_RX_SIZE];
		uint32_t adc_id;
		uint32_t phytimer;
		uint32_t cc;
		uint32_t axiq_done;
		uint32_t iqcomp_done;
		uint32_t read;
		// (64 - (4 * 5)) / 4 = 22
		uint32_t pad_to_64[10];
		uint32_t pad_to_128[16];
	}
); 
*/
RFNM_PACKED_STRUCT(
	struct rfnm_bufdesc_rx_sub {
		uint32_t adc_id;
		uint32_t phytimer;
		uint32_t cc;
		uint32_t size;
	}
); 
#define RFNM_RX_BUF_OUT_SUB_CNT 12

RFNM_PACKED_STRUCT(
	struct rfnm_bufdesc_rx {
		struct rfnm_bufdesc_rx_sub bufdesc[RFNM_RX_BUF_OUT_SUB_CNT];
		uint32_t padding[16];
		//vspa_complex_fixed16 buf[960];
		uint8_t buf[960*4];
		// 976 to make this struct 4kB wide and align it
	}
); 

typedef enum
{
	ERROR_DMA_DDR_RD_UNDERRUN=0,	// 0x0
	ERROR_DMA_DDR_WR_UNDERRUN,		// 0x1
	ERROR_AXIQ_FIFO_TX_UNDERRUN,	// 0x2
	ERROR_AXIQ_FIFO_RX_OVERRUN,		// 0x3
	ERROR_AXIQ_DMA_TX_CMD_UNDERRUN, // 0x4
	ERROR_AXIQ_DMA_RX_CMD_UNDERRUN, // 0x5
	ERROR_DMA_CONFIG_ERROR,		// 0x6
	PASS_TX_CYCLE,				// 0x7
	PASS_RX_CYCLE,				// 0x8
	LA_ERROR_MAX,					// 0x9
} error_id_e;

RFNM_PACKED_STRUCT(
	struct rfnm_la9310_status {
		//uint16_t rx_buf[16]; //use 8 to keep align, rather than RFNM_RX_BUF_CNT
		uint32_t age;
		uint32_t tx_buf_id;
		uint32_t rx_buf_id;
		uint32_t g_errors[LA_ERROR_MAX];
		//uint32_t pad_to_64[4];
		//uint32_t pad_again[8]; //<-- only for M7
	}
); 






#define LA9310_IQFLOOD_PHYS_ADDR	0xC0000000
#define RFNM_IQFLOOD_MEMADDR 0x96400000
#define RFNM_IQFLOOD_USB_MEMADDR 0x9D400000
#define RFNM_IQFLOOD_LOCAL_RX_MEMADDR 0x82400000
#define RFNM_IQFLOOD_LOCAL_TX_MEMADDR 0x87400000
#define RFNM_IQFLOOD_MEMSIZE_SHIVA (0xD000000)
#define RFNM_IQFLOOD_MEMSIZE (0xD000000)

#define RFNM_IQFLOOD_LOCAL_TX_BUF_SIZE 800
#define RFNM_IQFLOOD_LOCAL_RX_BUF_SIZE 800

/* share eth and local buffers for now */

#define RFNM_IQFLOOD_ETH_RX_MEMADDR 0x82400000
#define RFNM_IQFLOOD_ETH_TX_MEMADDR 0x87400000

#define RFNM_IQFLOOD_ETH_TX_BUF_SIZE 1500
#define RFNM_IQFLOOD_ETH_RX_BUF_SIZE 1500


#define RFNM_IQFLOOD_STATUS_MEMADDR (RFNM_IQFLOOD_MEMADDR + 0)
#define RFNM_IQFLOOD_L1TRACE_MEMADDR (RFNM_IQFLOOD_MEMADDR + 0x10000)
#define RFNM_IQFLOOD_ADC_MEMADDR (RFNM_IQFLOOD_MEMADDR + 0x100000)
#define RFNM_IQFLOOD_DAC_MEMADDR (RFNM_IQFLOOD_MEMADDR + 0x1100000)
//#define RFNM_IQFLOOD_USB_MEMADDR (RFNM_IQFLOOD_MEMADDR + 0x2100000)

#endif