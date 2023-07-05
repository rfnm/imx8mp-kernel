#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_device.h>


static int rfnm_wsled_probe(struct platform_device *pdev)
{
	void __iomem *io = ioremap(0x30220000, SZ_4K);
	volatile unsigned int *addr;

	addr = (volatile unsigned int *) io;

	uint32_t initial = *addr;
	uint32_t cond_high = initial | 0x80;
	uint32_t cond_low = initial & ~(0x80);

	printk("RFNM WSLED driver");

	// 1000 = 1.06us
	// 200 = 160ns

	int z;

	*addr = cond_low; for(z = 0; z < 250000; z++) asm volatile ("nop");


	uint32_t send[4] = {0xaaaaaaaa, 0xff0000, 0x00ff00, 0xdeadbeef};
	uint8_t current_bit = 0;
	uint8_t current_led = 0;

	uint8_t bit = (send[current_led] & (1 << current_bit)) >> current_bit;

	for(current_led = 0; current_led < 4; current_led++) {
		for(current_bit = 0; current_bit < 24; current_bit++) {

			if(current_led != 0) {
				*addr = cond_high; // reset condition while prefetching the loop
			}

			bit = (send[current_led] & (1 << current_bit)) >> current_bit;

			// total bit length = 1.25us +- 600ns
			// send one  -> high for 800 +- 150 ns; then low for 450 +- 150ns
			// send zero -> high for 400 +- 150 ns; then low for 850 +- 150ns


			if(bit) {
				for(z = 0; z < 715; z++) asm volatile ("nop");
			} else {
				for(z = 0; z < 350; z++) asm volatile ("nop");
			}

			*addr = cond_low;

			if(bit) {
				for(z = 0; z < 370; z++) asm volatile ("nop");
			} else {
				for(z = 0; z < 750; z++) asm volatile ("nop");
			}

		}
	}

	*addr = cond_low;

	return 0;
}

static const struct of_device_id rfnm_wsled_match_table[] = {
	{ .compatible = "rfnm,wsled", },
	{}
};

MODULE_DEVICE_TABLE(of, rfnm_wsled_match_table);

static struct platform_driver rfnm_wsled_driver = {
	.probe		= rfnm_wsled_probe,
	.driver		= {
		.name		= "rfnm-wsled-led",
		.of_match_table = rfnm_wsled_match_table,
	},
};

module_platform_driver(rfnm_wsled_driver);
