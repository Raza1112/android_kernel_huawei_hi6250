/*
 * Hisilicon HHEE exception driver .
 *
 * Copyright (c) 2012-2013 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/hisi/rdr_pub.h>
#include <linux/hisi/util.h>
#include <linux/hisi/rdr_hisi_platform.h>
#include <linux/compiler.h>
#include <linux/interrupt.h>
#include <asm/compiler.h>
#include <linux/debugfs.h>
#include "hhee.h"

struct hisi_hhee_device {
	struct device *dev;
	struct semaphore panic_sem;
};

static struct hisi_hhee_device *hisi_hhee;

struct rdr_exception_info_s hhee_excetption_info[] = {
	{
		.e_modid            = MODID_AP_S_HHEE_PANIC,
		.e_modid_end        = MODID_AP_S_HHEE_PANIC,
		.e_process_priority = RDR_ERR,
		.e_reboot_priority  = RDR_REBOOT_NOW,
		.e_notify_core_mask = RDR_AP,
		.e_reset_core_mask  = RDR_AP,
		.e_from_core        = RDR_AP,
		.e_reentrant        = (u32)RDR_REENTRANT_DISALLOW,
		.e_exce_type        = AP_S_HHEE_PANIC,
		.e_upload_flag      = (u32)RDR_UPLOAD_YES,
		.e_from_module      = "RDR HHEE PANIC",
		.e_desc             = "RDR HHEE PANIC"
	}/*lint !e785*/
};

static irqreturn_t hhee_panic_handle(int irq, void *data)
{
	pr_crit("hhee panic handler in kernel.\n");
	up(&hisi_hhee->panic_sem);

	return IRQ_HANDLED;
}/*lint !e715*/

int hhee_panic_thread(void *arg)
{
	pr_info("hhee_panic_happen start\n");

	while (1) {
		down(&hisi_hhee->panic_sem);

		pr_err("hhee panic trigger system_error.\n");
		rdr_syserr_process_for_ap((u32)MODID_AP_S_HHEE_PANIC, 0ULL, 0ULL);
		break;
	}
	return 0;
}/*lint !e715*/

/*check hhee enable*/
static int check_hhee_enable(void)
{
	unsigned int hhee_dts_enable;
	int ret;
	struct device_node *np = of_find_compatible_node(NULL, NULL, "hisi,hisi-hhee");

	if (np && of_find_property(np, "hhee_enable", NULL)) {
		ret = of_property_read_u32(np, "hhee_enable", &hhee_dts_enable);
		if (ret) {
			pr_info("HHEE: failed to find node hhee_enable");
			return HHEE_DISABLE;
		}
		if (HHEE_ENABLE != hhee_dts_enable) {
			pr_info("HHEE: disable, hhee_dts_enable is %d\n", hhee_dts_enable);
			return HHEE_DISABLE;
		}
	} else {
		pr_info("HHEE: no hisi-hhee found\n");
		return HHEE_DISABLE;
	}

	return HHEE_ENABLE;
}

static int hisi_hhee_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret, irq;

	pr_info("hhee panic probe\n");

	if (HHEE_DISABLE == check_hhee_enable())
		return 0;

	hisi_hhee = devm_kzalloc(dev, sizeof(*hisi_hhee), GFP_KERNEL);
	if (!hisi_hhee)
		return -ENOMEM;

	hisi_hhee->dev = dev;

	/*rdr struct register*/
	ret = (s32)rdr_register_exception(&hhee_excetption_info[0]);
	if (!ret)
		pr_err("register hhee exception fail.\n");

	ret = hhee_logger_init();
	if (ret < 0) {
		ret = -EINVAL;
		goto err_free_hhee;
	}

#ifdef CONFIG_HISI_DEBUG_FS
	ret = hhee_init_debugfs();
	if (ret < 0) {
		ret = -EINVAL;
		goto err_free_hhee;
	}
#endif
	/*irq num get and register*/
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		pr_err("hhee: get irq failed\n");
		ret = -ENXIO;
		goto err_free_hhee;
	}

	ret = devm_request_irq(&pdev->dev, (unsigned int)irq,
						hhee_panic_handle, 0ul, "hisi-hhee", pdev);
	if (ret < 0) {
		pr_err("devm request irq failed\n");
		ret = -EINVAL;
		goto err_free_hhee;
	}

	/*init a thread to process hhee exception*/
	sema_init(&hisi_hhee->panic_sem, 0);

	if (!kthread_run(hhee_panic_thread, NULL, "hhee_exception")) {
		pr_err("create hhee_exception_thread faild.\n");
		ret = -EINVAL;
		goto err_free_hhee;
	}
	pr_info("hhee probe done\n");
	return 0;

err_free_hhee:
	devm_kfree(dev, hisi_hhee);
	return ret;
}

void hhee_version(void)
{
	hhee_fn_hvc((unsigned long)ARM_STD_HVC_VERSION, 0UL, 0UL, 0UL);
}

static int hisi_hhee_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HISI_DEBUG_FS
	hhee_cleanup_debugfs();
#endif
	return 0;
}/*lint !e715*/

/*lint -e785 -esym(785,*)*/
static const struct of_device_id hisi_hhee_of_match[] = {
	{.compatible = "hisi,hisi-hhee"},
	{},
};

static struct platform_driver hisi_hhee_driver = {
	.driver = {
		.owner = THIS_MODULE, /*lint !e64*/
		.name = "hisi-hhee",
		.of_match_table = of_match_ptr(hisi_hhee_of_match),
	},
	.probe = hisi_hhee_probe,
	.remove = hisi_hhee_remove,
};
/*lint -e785 +esym(785,*)*/

static int __init hisi_hhee_panic_init(void)
{
	int ret;

	pr_info("hhee panic init\n");
	ret = platform_driver_register(&hisi_hhee_driver);/*lint !e64*/
	if (ret)
		pr_err("register hhee driver fail\n");

	return ret;
}

static void __exit hisi_hhee_panic_exit(void)
{
	platform_driver_unregister(&hisi_hhee_driver);
}

/*lint -e528 -esym(528,*)*/
/*lint -e753 -esym(753,*)*/
module_init(hisi_hhee_panic_init);
module_exit(hisi_hhee_panic_exit);

MODULE_DESCRIPTION("hisi hhee exception driver");
MODULE_ALIAS("hisi hhee exception module");
MODULE_LICENSE("GPL");
/*lint -e528 +esym(528,*)*/
/*lint -e753 +esym(753,*)*/
