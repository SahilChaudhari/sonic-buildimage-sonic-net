// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/module.h>
#include "ipc_main.h"
#ifdef TAWK_IPC_WITH_PCI
#include "ipc_pci.h"
#endif
#ifdef TAWK_IPC_WITH_PLATFORM
#include "ipc_platform.h"
#endif

MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_DESCRIPTION(TAWK_IPC_DESCRIPTION);
MODULE_VERSION(TAWK_IPC_VERSION);
MODULE_LICENSE("GPL");

/*
 * Minimum size of the common request buffer pool
 */
static unsigned int min_common_pool_size = 0;
module_param(min_common_pool_size, uint, 0644);
MODULE_PARM_DESC(min_common_pool_size,
		 "Minimum size of common request buffer pool");

static unsigned int link_down_mode = 1;
module_param(link_down_mode, int, 0444);
MODULE_PARM_DESC(link_down_mode,
		 "Handle link down event ("
		 "0=*all* users must close TAWK IPC files to ACK link down [strict, default], "
		 "1=Auto-ACK of link down, but users must reopen TAWK IPC files [less strict], "
		 "2=Auto-ACK of link down, and users do *not* need to reopen TAWK IPC files [dangerous])");


unsigned int tawk_drv_minimum_common_pool_size(void)
{
	return min_common_pool_size;
}

enum tawk_drv_link_down_mode tawk_drv_get_link_down_mode(void)
{
	switch (link_down_mode) {
	default:
	case 0:
		return TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN;
	case 1:
		return TAWK_DRV_LINK_DOWN_MODE_REOPEN;
	case 2:
		return TAWK_DRV_LINK_DOWN_MODE_IGNORE;
	}
}

static int __init tawk_drv_verify_params(void)
{
	int rc = 0;

	if (link_down_mode > TAWK_DRV_LINK_DOWN_MODE_MAX) {
		pr_err("link_down_mode (%u) is exceeding max (%u)\n",
			link_down_mode, TAWK_DRV_LINK_DOWN_MODE_MAX);
		rc = -ERANGE;
	}

	return rc;
}

static int __init tawk_drv_init(void)
{
	int rc = 0;

	pr_info("%s: %s, ver %s\n",
#if defined(TAWK_IPC_WITH_PCI)
		TAWK_DRIVER_NAME,
#elif defined(TAWK_IPC_WITH_PLATFORM)
		TAWK_DRV_MM_NAME,
#else
#error "Not yet handled"
#endif
		TAWK_IPC_DESCRIPTION, TAWK_IPC_VERSION);

	rc = tawk_drv_verify_params();
	if (rc != 0)
		goto err_params;

#ifdef TAWK_IPC_WITH_PCI
	rc = tawk_drv_pci_register_driver();
	if (rc != 0)
		goto err_pci_register_fail;
#endif
#ifdef TAWK_IPC_WITH_PLATFORM
	rc = tawk_drv_platform_register_driver();
	if (rc != 0)
		goto err_platform_register_fail;
#endif
	return 0;
#ifdef TAWK_IPC_WITH_PLATFORM
	tawk_drv_platform_unregister_driver();
err_platform_register_fail:
#endif
#ifdef TAWK_IPC_WITH_PCI
	tawk_drv_pci_unregister_driver();
err_pci_register_fail:
#endif
err_params:
	return rc;
}

static void __exit tawk_drv_exit(void)
{
#ifdef TAWK_IPC_WITH_PLATFORM
	tawk_drv_platform_unregister_driver();
#endif
#ifdef TAWK_IPC_WITH_PCI
	tawk_drv_pci_unregister_driver();
#endif
	pr_info("%s: Driver unloaded\n",
#if defined(TAWK_IPC_WITH_PCI)
		TAWK_DRIVER_NAME
#elif defined(TAWK_IPC_WITH_PLATFORM)
		TAWK_DRV_MM_NAME
#else
#error "Not yet handled"
#endif
	       );
}

module_init(tawk_drv_init);
module_exit(tawk_drv_exit);
