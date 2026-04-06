// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "ipc_pci.h"
#include "ipc_main.h"
#include "ipc_device.h"
#include <tawk/device.h>
#include <tawk/ipc.h>
#include <tawk/platform.h>

static const struct pci_device_id tawk_drv_pci_id_table[] = {
	{ PCI_VDEVICE(PENSANDO, PCI_DEVICE_ID_PENSANDO_TAWK_PF) },
	{ },
};

static void tawk_drv_pci_unmap_bar(struct tawk_drv_data *tawk_data)
{
	struct pci_dev *pci_dev = tawk_data->u.pci.pci_dev;
	struct tawk_drv_bar *bar = &tawk_data->u.pci.bar;

	iounmap(bar->base_addr);
	pci_release_region(pci_dev, TAWK_PF_BAR);
	bar->len = 0;
	bar->phys_addr = 0;
}

static int tawk_drv_pci_map_bar(struct tawk_drv_data *tawk_data,
				struct pci_dev *pci_dev)
{
	struct tawk_drv_bar *bar;
	int rc;

	bar = &tawk_data->u.pci.bar;
	bar->len = pci_resource_len(pci_dev, TAWK_PF_BAR);
	bar->phys_addr = pci_resource_start(pci_dev, TAWK_PF_BAR);
	if (!bar->phys_addr)
		return -ENODEV;

	rc = pci_request_region(pci_dev, TAWK_PF_BAR, TAWK_DRIVER_NAME);
	if (rc) {
		pci_err(pci_dev, "request for memory BAR[%d] failed. rc=%d\n",
			TAWK_PF_BAR, rc);
		goto err_clear_bar_addr;
	}

	bar->base_addr = pci_iomap(pci_dev, TAWK_PF_BAR, bar->len);
	if (!bar->base_addr) {
		pci_err(pci_dev, "Could not map memory BAR[%d]", TAWK_PF_BAR);
		rc = -ENOMEM;
		goto err_pci_release_region;
	}

	pci_info(pci_dev, "Memory BAR%d at %016llx+%lx (virtual %p)\n", TAWK_PF_BAR,
		 (unsigned long long)bar->phys_addr, bar->len, bar->base_addr);
	return 0;

err_pci_release_region:
	pci_release_region(pci_dev, TAWK_PF_BAR);
err_clear_bar_addr:
	bar->phys_addr = 0;
	return rc;
}

static irqreturn_t tawk_drv_rstsigs_irq(int irq, void *user_data)
{
	struct tawk_drv_data *tawk_data = user_data;
	ipc_device_rst_notify_interrupt(tawk_data->ipc_stack);
	return IRQ_HANDLED;
}

static irqreturn_t tawk_drv_mbox_irq(int irq, void *user_data)
{
	struct tawk_drv_data *tawk_data = user_data;
	ipc_device_mbox_notify_interrupt(tawk_data->ipc_stack);
	return IRQ_HANDLED;
}

enum {
	TAWK_IPC_RSTSIGS_IRQ = 0,
	TAWK_IPC_MBOX_IRQ,
	TAWK_IPC_NR_IRQ_VECS,
};

/* A callback from the TAWK IPC library to initialise interrupt handling
 * with the Linux kernel. On success, returns zero. On error, it reverts
 * any changes made and returns a negative number.
 *
 * The initialised interrupts must be masked/disabled.
 */
static int tawk_drv_init_interrupts(void *fn_ctx)
{
	struct pci_dev *pci_dev = fn_ctx;
	struct tawk_drv_data *tawk_data = pci_get_drvdata(pci_dev);
	int rc;

	rc = pci_alloc_irq_vectors(pci_dev, TAWK_IPC_NR_IRQ_VECS,
			TAWK_IPC_NR_IRQ_VECS, PCI_IRQ_MSIX);

	/* The above function returns the number of allocated vectors.
	 * Hence, when the minimum and the maximum are the same, we
	 * expect only one positive number on success. Otherwise, it
	 * must be a negative error code.
	 */
	if (rc != TAWK_IPC_NR_IRQ_VECS) {
		pci_err(pci_dev, "unable to alloc IRQ vectors. rc=%d\n", rc);
		goto err;
	}

	/* Clear in case it is set to prevent spurious interrupts
	 * immediately after request_irq() because it also enables
	 * interrupts handling.
	 */
	pci_clear_master(pci_dev);

	rc = request_irq(pci_irq_vector(pci_dev, TAWK_IPC_RSTSIGS_IRQ),
			tawk_drv_rstsigs_irq, 0, "tawk_rstsigs", tawk_data);
	if (rc != 0) {
		pci_err(pci_dev, "unable to request IRQ for rstsigs. rc=%d\n",
				rc);
		goto err_free_irq_vectors;
	}

	rc = request_irq(pci_irq_vector(pci_dev, TAWK_IPC_MBOX_IRQ),
			tawk_drv_mbox_irq, 0, "tawk_mbox", tawk_data);
	if (rc != 0) {
		pci_err(pci_dev, "unable to request IRQ for mbox. rc=%d\n",
				rc);
		goto err_free_rst_irq;
	}

	/* Explicitly mask/disable interrupts until explicitly enabled */
	disable_irq(pci_irq_vector(pci_dev, TAWK_IPC_RSTSIGS_IRQ));
	disable_irq(pci_irq_vector(pci_dev, TAWK_IPC_MBOX_IRQ));

	/* Now it is safe to enable bus mastering */
	pci_set_master(pci_dev);

	return 0;

err_free_rst_irq:
	free_irq(pci_irq_vector(pci_dev, TAWK_IPC_RSTSIGS_IRQ), tawk_data);
err_free_irq_vectors:
	pci_free_irq_vectors(pci_dev);
err:
	/* Note, we must return a positive error code */
	return -rc;
}

static void tawk_drv_enable_interrupts(struct pci_dev *pci_dev)
{
	enable_irq(pci_irq_vector(pci_dev, TAWK_IPC_RSTSIGS_IRQ));
	enable_irq(pci_irq_vector(pci_dev, TAWK_IPC_MBOX_IRQ));
}

/* A callback from the TAWK IPC library to deinitialise Linux interrupt
 * handling and undo the effects achieved with tawk_drv_init_interrupts()
 * and tawk_drv_enable_interrupts().
 */
static void tawk_drv_fini_interrupts(void *fn_ctx)
{
	struct pci_dev *pci_dev = fn_ctx;
	struct tawk_drv_data *tawk_data = pci_get_drvdata(pci_dev);

	pci_clear_master(pci_dev);

	free_irq(pci_irq_vector(pci_dev, TAWK_IPC_MBOX_IRQ), tawk_data);
	free_irq(pci_irq_vector(pci_dev, TAWK_IPC_RSTSIGS_IRQ), tawk_data);

	pci_free_irq_vectors(pci_dev);
}

static int tawk_drv_pci_probe(struct pci_dev *pci_dev,
			      const struct pci_device_id *id)
{
	struct tawk_drv_data *tawk_data;
	struct tawk_drv_bar *tawk_bar;
	bool interrupts = true;
	int rc;

	tawk_data = kzalloc(sizeof(*tawk_data), GFP_KERNEL);
	if (!tawk_data)
		return -ENOMEM;

	tawk_data->dev = &pci_dev->dev;
	tawk_data->u.pci.pci_dev = pci_dev;
	pci_set_drvdata(pci_dev, tawk_data);

	rc = pci_enable_device(pci_dev);
	if (rc) {
		pci_err(pci_dev, "failed to enable PCI device. rc=%d\n", rc);
		goto err_free_drv_data;
	}

	rc = tawk_drv_pci_map_bar(tawk_data, pci_dev);
	if (rc) {
		pci_err(pci_dev, "failed to map BAR. rc=%d\n", rc);
		goto err_disable_pci_dev;
	}

	tawk_bar = &tawk_data->u.pci.bar;

	tawk_data->ipc_stack = ipc_alloc_init(pcie_host,
		(uintptr_t) tawk_bar->base_addr,
#if IPC_WITH_POLLING
		true,
#endif
		&interrupts, TAWK_IPC_RSTSIGS_IRQ, TAWK_IPC_MBOX_IRQ,
		tawk_drv_init_interrupts, tawk_drv_fini_interrupts,
		pci_dev, &pci_dev->dev);
	if (IS_ERR(tawk_data->ipc_stack)) {
		rc = PTR_ERR(tawk_data->ipc_stack);
		goto err_unmap_pci_bar;
	}

	rc = tawk_drv_device_init(tawk_data);
	if (rc != 0)
		goto err_free_ipc_stack;

	/* If this NIC supports interrupts, they must be configured now,
	 * but we still need to enable/unmask them ourselves here.
	 */
	if (interrupts)
		tawk_drv_enable_interrupts(pci_dev);

	/* Initially IPC is locked */
	ipc_unlock(tawk_data->ipc_stack);

	pci_info(pci_dev, "Device probed (interrupts %s)\n",
			interrupts ? "enabled" : "disabled");
	return 0;

err_free_ipc_stack:
	ipc_fini_free(tawk_data->ipc_stack);
err_unmap_pci_bar:
	tawk_drv_pci_unmap_bar(tawk_data);
err_disable_pci_dev:
	pci_disable_device(pci_dev);
err_free_drv_data:
	pci_set_drvdata(pci_dev, NULL);
	kfree(tawk_data);
	return rc;
}

static void tawk_drv_pci_link_down(ipc_context_t *ipc,
				   ipc_link_event_handle_t handle,
				   void *cb_ctx)
{
	struct completion *link_down_completion = (struct completion *)cb_ctx;
	complete(link_down_completion);
}

/* Bring the IPC datalink down (irreversibly).
 *
 * How is this made? We register a new link-down event handler where we
 * will not respond with an ACK, i.e. ipc_link_down_event_ack(), which
 * will prevent IPC from progressing to the link recovery.
 *
 * Then, we request the IPC link reset with ipc_request_reset() and wait
 * until it happens.
 *
 * Upon completion, we reacquire the IPC lock and this guarantees that all
 * IPC users are aware of the link-down.
 *
 * WARNING Use only in the d-tor workflows.
 */
static void tawk_drv_set_link_down(struct tawk_drv_data *tawk_data)
{
	ipc_context_t *ipc = tawk_data->ipc_stack;
	ipc_link_event_cb_t link_cb = (ipc_link_event_cb_t) {
		.link_up_cb   = NULL,
		.link_down_cb = tawk_drv_pci_link_down,
		.cb_ctx = &tawk_data->link_down_completion,
	};

	WARN_ON_ONCE(!ipc_link_is_up(ipc));

	init_completion(&tawk_data->link_down_completion);

	ipc_register_link_event_handler(ipc, &link_cb);
	ipc_request_reset(ipc);
	ipc_unlock(ipc);

	wait_for_completion(&tawk_data->link_down_completion);

	ipc_lock(ipc);

	WARN_ON_ONCE(ipc_link_is_up(ipc));

	/* We must remove the callback data from the linked list
	 * because it resides on the stack and might corrupt it
	 * otherwise.
	 */
	ipc_deregister_link_event_handler(ipc, &link_cb);
}

static void tawk_drv_pci_remove(struct pci_dev *pci_dev)
{
	struct tawk_drv_data *tawk_data = pci_get_drvdata(pci_dev);
	int rc;

	ipc_lock(tawk_data->ipc_stack);
	if (ipc_link_is_up(tawk_data->ipc_stack))
		tawk_drv_set_link_down(tawk_data);

	/* "Invalidate" an IPC instance and unlock the IPC lock. */
	ipc_device_quarantine(tawk_data->ipc_stack);

	tawk_drv_pci_unmap_bar(tawk_data);
	pci_disable_device(pci_dev);
	pci_set_drvdata(pci_dev, NULL);
	tawk_data->dev = NULL;
	tawk_data->u.pci.pci_dev = NULL;
	tawk_drv_device_fini(tawk_data);
	rc = tawk_drv_data_put(tawk_data);
	pci_info(pci_dev, "Device removed%s\n",
		 rc ? " (outstanding clients)" : "");
}

static struct pci_driver tawk_drv_pci_driver = {
	.name = TAWK_DRIVER_NAME,
	.id_table = tawk_drv_pci_id_table,
	.probe = tawk_drv_pci_probe,
	.remove = tawk_drv_pci_remove,
};

int tawk_drv_pci_register_driver(void)
{
	return pci_register_driver(&tawk_drv_pci_driver);
}

void tawk_drv_pci_unregister_driver(void)
{
	pci_unregister_driver(&tawk_drv_pci_driver);
}

MODULE_DEVICE_TABLE(pci, tawk_drv_pci_id_table);
