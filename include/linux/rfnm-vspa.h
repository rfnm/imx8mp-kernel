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
		vspa_complex_fixed16 buf[FFT_SIZE];
		uint32_t dac_id;
		uint32_t phytimer;
		uint32_t cc;
		uint32_t axiq_done;
		uint32_t iqcomp_done;
		// (64 - (4 * 5)) / 4 = 22
		uint32_t pad_to_64[11];
	}
); 

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
#endif
