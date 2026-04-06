// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_MAIN_H_
#define _IPC_MAIN_H_
#include <tawk/ipc.h>
#include <linux/refcount.h>

#define TAWK_IPC_DESCRIPTION "TAWK IPC Driver"
#define TAWK_IPC_VERSION drv_ver

#ifdef TAWK_IPC_WITH_PLATFORM
#define NUM_PLATFORM_RESOURCES 4
#endif

#define TAWK_DEVICE_NAME "tawkipcdev"
/* Maximum length of device name (tawkipcdevXXX) */
#define TAWK_DEVICE_NAME_LEN (10+3+1)
#define TAWK_DEVICE_MAX_ID 999

#ifdef TAWK_IPC_WITH_PCI
/**
 * struct tawk_drv_bar
 * @phys_addr: BAR physical address
 * @base_addr: BAR virtual mapping
 * @len: BAR length
 */
struct tawk_drv_bar {
	phys_addr_t phys_addr;
	void __iomem *base_addr;
	unsigned long len;
};
#endif

#ifdef TAWK_IPC_WITH_PLATFORM
/**
 * struct tawk_drv_resource
 * @phys_addr: Resource physical address
 * @vaddr: Resource virtual mapping
 * @len: Resource len
 */
struct tawk_drv_resource {
	phys_addr_t phys_addr;
	void __iomem *vaddr;
	unsigned long len;
};
#endif

/**
 * struct tawk_drv_data
 * @pci_dev: The PCI device
 * @tawk_drv_bar: BAR owned by PCI device
 * @platform_dev: The platform device
 * @platform_resources: Platform device memory mappings
 * @mdev: IPC character device
 * @misc_workqueue: Deferring miscdevice lifecycle management
 * @misc_init_work: Register miscdevice
 * @misc_fini_work: Deregister miscdevice
 * @ipc_stack: Instance of the core IPC stack
 * @channel_lock: Serialize request pool size changes
 * @req_handlers: List of registered request handlers
 * @req_handlers_lock: Lock protecting request handlers
 * @link_cb: Link up/down callbacks
 * @dev_id: IPC character device ID
 * @dev_name: IPC character device name
 * @req_limit: Max number of in-flight outgoing requests
 * @rsp_limit: Max number of in-flight in-coming requests
 * @refs: TAWK data reference counter
 * @link_down_completion: Wait for IPC link-down in d-tor
 */
struct tawk_drv_data {
	struct device *dev;
	union {
#ifdef TAWK_IPC_WITH_PCI
		struct {
			struct pci_dev *pci_dev;
			struct tawk_drv_bar bar;
		} pci;
#endif
#ifdef TAWK_IPC_WITH_PLATFORM
		struct {
			struct platform_device *platform_dev;
			struct tawk_drv_resource platform_resources[NUM_PLATFORM_RESOURCES];
			int n_resources;
		} platform;
#endif
	} u;
	struct miscdevice *mdev;
	struct workqueue_struct *misc_workqueue;
	struct work_struct misc_init_work;
	struct work_struct misc_fini_work;

	ipc_context_t *ipc_stack;
	struct mutex channel_lock;
	struct list_head req_handlers;
	struct mutex req_handlers_lock;
	ipc_link_event_cb_t link_cb;
	int dev_id;
	char dev_name[TAWK_DEVICE_NAME_LEN];
	size_t req_limit;
	size_t rsp_limit;

	refcount_t refs;
	struct completion link_down_completion;
};

unsigned int tawk_drv_minimum_common_pool_size(void);

/* The link down event acknowledgement tactics */
enum tawk_drv_link_down_mode {
	TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN = 0,
	TAWK_DRV_LINK_DOWN_MODE_REOPEN = 1,
	TAWK_DRV_LINK_DOWN_MODE_IGNORE = 2,

	TAWK_DRV_LINK_DOWN_MODE_MAX = TAWK_DRV_LINK_DOWN_MODE_IGNORE,
};

enum tawk_drv_link_down_mode tawk_drv_get_link_down_mode(void);

#endif /* _IPC_MAIN_H_ */
