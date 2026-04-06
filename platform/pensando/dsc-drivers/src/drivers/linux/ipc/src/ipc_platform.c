// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/of_reserved_mem.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <tawk/device.h>
#include <tawk/ipc.h>
#include <tawk/platform.h>
#include "ipc_platform.h"
#include "ipc_main.h"
#include "ipc_device.h"


static const struct of_device_id tawk_drv_platform_id_table[] = {
	{ .compatible = "amd,tawk-ipc" },
	{ },
};

static int tawk_drv_platform_map_resource(struct tawk_drv_data *tawk_data,
					  int idx, const char *format,
					  mm_reg_t *result)
{
	struct platform_device *pfdev = tawk_data->u.platform.platform_dev;
	struct tawk_drv_resource *tawk_res;
	struct resource *res;
	void __iomem *v_addr;

	int res_idx = tawk_data->u.platform.n_resources;
	WARN_ON(res_idx >= NUM_PLATFORM_RESOURCES);
	if (res_idx >= NUM_PLATFORM_RESOURCES)
		return -EINVAL;

	tawk_res = &tawk_data->u.platform.platform_resources[res_idx];

	res = platform_get_resource(pfdev, IORESOURCE_MEM, idx);
	if (!res) {
		dev_err(&pfdev->dev, "Cannot find resource %d\n", idx);

		return -EINVAL;
	}

	v_addr = ioremap(res->start, res->end - res->start);
	if (IS_ERR(v_addr)) {
		dev_err(&pfdev->dev, "Cannot memory-map %s err=%pe\n", format, v_addr);
		return PTR_ERR(v_addr);
	}
	*result = (mm_reg_t) v_addr;

	tawk_res->phys_addr = res->start;
	tawk_res->vaddr = v_addr;
	tawk_res->len = res->end - res->start;
	tawk_data->u.platform.n_resources++;
	return 0;
}

static void tawk_drv_platform_unmap_resources(struct tawk_drv_data *tawk_data);

static int tawk_drv_platform_map_resources(struct tawk_drv_data *tawk_data,
					   mm_reg_t *db_addr,
					   mm_reg_t *rst_sigs_addr,
					   mm_reg_t *mbox_ctl_addr,
					   mm_reg_t *mbox_buffer_addr)
{
	int rc = 0;

	if (db_addr) {
		rc = tawk_drv_platform_map_resource(tawk_data,
						    TAWK_DRV_DB_REGION, "db_addr",
						    db_addr);
		if (rc)
			goto err_unmap;
	}

	rc = tawk_drv_platform_map_resource(tawk_data,
					    TAWK_DRV_RST_SIGS_REGION, "rst_sigs_addr",
					    rst_sigs_addr);
	if (rc)
		goto err_unmap;

	rc = tawk_drv_platform_map_resource(tawk_data,
					    TAWK_DRV_MBOX_CTL_REGION, "mbox_ctl_addr",
					    mbox_ctl_addr);
	if (rc)
		goto err_unmap;

	rc = tawk_drv_platform_map_resource(tawk_data,
					    TAWK_DRV_MBOX_BUFFER_REGION, "mbox_buffer_addr",
					    mbox_buffer_addr);
	if (rc)
		goto err_unmap;

	return 0;

err_unmap:
        tawk_drv_platform_unmap_resources(tawk_data);
        return rc;
}

static void tawk_drv_platform_unmap_resources(struct tawk_drv_data *tawk_data)
{
	struct tawk_drv_resource *tawk_res = tawk_data->u.platform.platform_resources;

	while (tawk_data->u.platform.n_resources > 0) {
		int res_idx = tawk_data->u.platform.n_resources;
		iounmap(tawk_res[res_idx].vaddr);
		tawk_data->u.platform.n_resources--;
	}
}

static int tawk_drv_platform_get_properties(struct device_node *np,
					    struct tawk_drv_platform_properties *props)
{
	uint32_t temp[3];
	int rc;

	rc = of_property_read_u32(np, "peer-id", &props->peer_id);
	if (rc)
		return rc;

	rc = of_property_read_u32_array(np, "rst-db", temp, 2);
	if (rc) {
		props->rst_db_exists = false;
	} else {
		props->rst_db_exists = true;
		props->rst_db_offset = temp[0];
		props->rst_db_data = temp[1];
	}

	rc = of_property_read_u32_array(np, "mbox-db", temp, 2);
	if (rc) {
		props->mbox_db_exists = false;
	} else {
		props->mbox_db_exists = true;
		props->mbox_db_offset = temp[0];
		props->mbox_db_data = temp[1];
	}

	rc = of_property_read_u32_array(np, "rst-sigs", temp, 2);
	if (rc)
		return rc;

	props->rst_sig_out_offset = temp[0];
	props->rst_sig_in_offset = temp[1];

	rc = of_property_read_u32(np, "nmboxes", &props->nmboxes);
	if (rc)
		return rc;

	rc = of_property_read_u32_array(np, "mbox-ctl", temp, 2);
	if (rc)
		return rc;

	props->mbox_ctl_offset = temp[0];
	props->mbox_ctl_stride = temp[1];

	rc = of_property_read_u32_array(np, "mbox-buf", temp, 3);
	if (rc)
		return rc;

	props->mbox_buffer_offset = temp[0];
	props->mbox_buffer_stride = temp[1];
	props->mbox_buffer_size = temp[2];

	return 0;
}

static int tawk_drv_platform_probe(struct platform_device *pfdev)
{
#define MM_REG_ERR_VAL ((uintptr_t)ERR_PTR(EFAULT))
	/* Region base addresses */
	mm_reg_t db_addr = MM_REG_ERR_VAL;
	mm_reg_t rst_sigs_addr = MM_REG_ERR_VAL;
	mm_reg_t mbox_ctl_addr = MM_REG_ERR_VAL;
	mm_reg_t mbox_buffer_addr = MM_REG_ERR_VAL;

	/* Arguments for allocating Tawk IPC stack */
	mm_reg_t rst_sigs_out_addr, rst_sigs_in_addr, rst_db_addr,
		 mbox_control_base, mbox_buffer_base, mbox_db_addr;
	uint32_t rst_db_val, mbox_count, mbox_control_stride_log2,
		 mbox_buffer_stride_log2, mbox_db_val,
		 mbox_buffer_size;

	struct tawk_drv_platform_properties tawk_props;
	struct tawk_drv_data *tawk_data;
	struct device *dev = &pfdev->dev;
	struct device_node *np;
	int rc;

	tawk_data = kzalloc(sizeof(*tawk_data), GFP_KERNEL);
	if (!tawk_data)
		return -ENOMEM;

	tawk_data->dev = &pfdev->dev;
	tawk_data->u.platform.platform_dev = pfdev;
	platform_set_drvdata(pfdev, tawk_data);

	np = dev->of_node;
	if (!np) {
		dev_err(dev, "Can't find device tree node\n");
		rc = -EINVAL;
		goto err_free_drv_data;
	}


	rc = tawk_drv_platform_get_properties(np, &tawk_props);
	if (rc) {
		dev_err(dev, "Failed to get properties rc=%d\n", rc);
		goto err_free_drv_data;
	}

	rc = tawk_drv_platform_map_resources(tawk_data,
					     (tawk_props.rst_db_exists || tawk_props.mbox_db_exists) ? &db_addr : NULL,
					     &rst_sigs_addr,
					     &mbox_ctl_addr,
					     &mbox_buffer_addr);
	if (rc) {
		dev_err(dev, "Failed to map resources rc=%d\n", rc);
		goto err_free_drv_data;
	}

	BUG_ON(rst_sigs_addr == MM_REG_ERR_VAL);
	rst_sigs_out_addr = rst_sigs_addr + tawk_props.rst_sig_out_offset;
	rst_sigs_in_addr = rst_sigs_addr + tawk_props.rst_sig_in_offset;

	if (tawk_props.rst_db_exists) {
		BUG_ON(rst_sigs_addr == MM_REG_ERR_VAL);
		rst_db_addr = db_addr + tawk_props.rst_db_offset;
		rst_db_val = tawk_props.rst_db_data;
	} else {
		rst_db_addr = 0xbaadbaadbaadbaad;
		rst_db_val = 0xbaadbaad;
	}

	mbox_count = tawk_props.nmboxes;

	BUG_ON(mbox_ctl_addr == MM_REG_ERR_VAL);
	mbox_control_base = mbox_ctl_addr + tawk_props.mbox_ctl_offset;
	mbox_control_stride_log2 = tawk_props.mbox_ctl_stride;

	BUG_ON(mbox_buffer_addr == MM_REG_ERR_VAL);
	mbox_buffer_base = mbox_buffer_addr + tawk_props.mbox_buffer_offset;
	mbox_buffer_stride_log2 = tawk_props.mbox_buffer_stride;
	mbox_buffer_size = tawk_props.mbox_buffer_size;

	if (tawk_props.mbox_db_exists) {
		BUG_ON(rst_sigs_addr == MM_REG_ERR_VAL);
		mbox_db_addr = db_addr + tawk_props.mbox_db_offset;
		mbox_db_val = tawk_props.mbox_db_data;
	} else {
		mbox_db_addr = 0xbaadbaadbaadbaad;
		mbox_db_val = 0xbaadbaad;
	}

	tawk_data->ipc_stack =
		ipc_alloc_init(mm_host, tawk_props.peer_id, /*poll = */ true,
			       rst_sigs_out_addr,
			       rst_sigs_in_addr,
			       tawk_props.rst_db_exists,
			       rst_db_addr, rst_db_val,
			       mbox_count,
			       mbox_control_base, mbox_control_stride_log2,
			       mbox_buffer_base, mbox_buffer_stride_log2,
			       mbox_buffer_size,
			       tawk_props.mbox_db_exists,
			       mbox_db_addr, mbox_db_val,
			       dev);

	if (IS_ERR(tawk_data->ipc_stack)) {
		rc = PTR_ERR(tawk_data->ipc_stack);
		dev_err(dev, "TAWK stack initialization failed rc=%d\n", rc);
		goto err_unmap_resources;
	}

	rc = tawk_drv_device_init(tawk_data);
	if (rc)
		goto err_free_ipc_stack;

	/* Initially IPC is locked */
	ipc_unlock(tawk_data->ipc_stack);

	dev_info(dev, "TAWK Platform IPC driver probed\n");
	return 0;

err_free_ipc_stack:
	ipc_fini_free(tawk_data->ipc_stack);
err_unmap_resources:
	tawk_drv_platform_unmap_resources(tawk_data);
err_free_drv_data:
	platform_set_drvdata(pfdev, NULL);
	kfree(tawk_data);
	return rc;
}

#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
static void tawk_drv_platform_remove(struct platform_device *pfdev)
#else
static int tawk_drv_platform_remove(struct platform_device *pfdev)
#endif
{
	struct tawk_drv_data *tawk_data = platform_get_drvdata(pfdev);
	struct device *dev = &pfdev->dev;

	ipc_lock(tawk_data->ipc_stack);
	tawk_drv_device_fini(tawk_data);
	ipc_fini_free(tawk_data->ipc_stack);
	tawk_drv_platform_unmap_resources(tawk_data);
	platform_set_drvdata(pfdev, NULL);
	kfree(tawk_data);
	dev_info(dev, "TAWK Platform IPC driver removed\n");
#if (KERNEL_VERSION(5, 18, 0) > LINUX_VERSION_CODE)
	return 0;
#endif
}

static struct platform_driver tawk_drv_platform_driver = {
	.driver = {
		.name = TAWK_DRV_MM_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tawk_drv_platform_id_table,
	},
	.probe = tawk_drv_platform_probe,
	.remove = tawk_drv_platform_remove,
};

int tawk_drv_platform_register_driver(void)
{
	return platform_driver_register(&tawk_drv_platform_driver);
}

void tawk_drv_platform_unregister_driver(void)
{
	platform_driver_unregister(&tawk_drv_platform_driver);
}
