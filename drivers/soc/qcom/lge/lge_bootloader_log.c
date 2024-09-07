/*
 * drivers/soc/qcom/lge/lge_bootloader_log.c
 *
 * Copyright (C) 2012 LGE, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/io.h>
#include <asm/setup.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <soc/qcom/lge/lge_handle_panic.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>

#define LOGBUF_SIZE   0x20000
#define LOGBUF_SIG    0x6c6f6762

struct log_buffer {
	uint32_t    sig;
	uint32_t    size;
	uint32_t    end;
	uint32_t    start;
};


struct bootlog_platform_data {
	unsigned long paddr;
	unsigned long size;
};


static struct of_device_id bootlog_of_match[] = {
	{.compatible = "bootlog", },
	{ },
};

static int bootlog_probe(struct platform_device *pdev)
{
	struct log_buffer *bootlog_buf;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct resource r={0,};
	phys_addr_t mem_phys;
	void *mem_va;
	size_t mem_size;
	char *buffer, *token;
	int ret;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (node == NULL) {
		pr_err("%s: of_find_node_by_path failed\n", __func__);
		return -EINVAL;
	}
	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;
	mem_phys = r.start;
	mem_size = resource_size(&r);
	mem_va = memremap(r.start, mem_size, MEMREMAP_WB);

	if (!mem_va) {
		dev_err(dev, "unable to map memory region: %pa+%zx\n",
			&r.start, mem_size);
		return -ENOMEM;
	}

	bootlog_buf = (struct log_buffer *)mem_va;

	if (bootlog_buf->sig != LOGBUF_SIG) {
		pr_info("bootlog_buf->sig is not valid (%x)\n",
				bootlog_buf->sig);
		return -EINVAL;
	}

	pr_info("%s: addr(0x%lx) start(0x%x) end(0x%x) size(0x%x)\n", __func__, mem_phys, bootlog_buf->start, bootlog_buf->end, bootlog_buf->size);
	pr_info("-------------------------------------------------\n");
	pr_info("below logs are got from bootloader\n");
	pr_info("-------------------------------------------------\n");

	buffer = (char *)&bootlog_buf->start;
	buffer[mem_size - sizeof(struct log_buffer) - 1] = '\0';

	while (1) {
		token = strsep(&buffer, "\n");
		if (!token) {
			pr_info("%s: token %p\n", __func__, token);
			break;
		}
		pr_info("%s\n", token);
	}

	pr_info("-------------------------------------------------\n");

	memunmap(mem_va);
	if(!lge_get_download_mode()){
		unsigned long pfn_start, pfn_end, pfn_idx;
		pr_info("reserved-memory free[@0x%lx+@0x%lx)\n",mem_phys, mem_size);
		memblock_free(mem_phys, mem_size);

		pfn_start = mem_phys >> PAGE_SHIFT;
		pfn_end = (mem_phys + mem_size) >> PAGE_SHIFT;
		for (pfn_idx = pfn_start; pfn_idx < pfn_end; pfn_idx++)
			free_reserved_page(pfn_to_page(pfn_idx));
	}
	return 0;
}

static struct platform_driver bootlog_driver = {
	.probe		= bootlog_probe,
	.remove		= __exit_p(bootlog_remove),
	.driver		= {
		.name	= "bootlog",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(bootlog_of_match),
	},
};

static int __init lge_bootlog_init(void)
{
	return platform_driver_register(&bootlog_driver);
}

static void __exit lge_bootlog_exit(void)
{
	platform_driver_unregister(&bootlog_driver);
}

module_init(lge_bootlog_init);
module_exit(lge_bootlog_exit);

MODULE_DESCRIPTION("LGE bootloader log driver");
MODULE_AUTHOR("SungEun Kim <cleaneye.kim@lge.com>");
MODULE_LICENSE("GPL");
