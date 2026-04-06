// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <tawk_drv_debug/debug.h>

#include "ipc_debug.h"
#include "ipc_device.h"

#define MAX_READ_SIZE 4096

static int ipc_debug_client_open(struct inode *inode, struct file *file)
{
	struct tawk_drv_client *client;
	char *buffer;

	client = inode->i_private;

	buffer = kzalloc(MAX_READ_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	snprintf(buffer, MAX_READ_SIZE, "req_free:%d req_in_use:%d read_q:%d "
		 "link_is_up:%s link_down_count:%d req_total:%d resp_total:%d\n",
		 tawk_drv_client_req_free_count(client),
		 tawk_drv_client_req_in_use_count(client),
		 tawk_drv_client_read_q_count(client),
		 tawk_drv_client_link_is_up(client) ? "yes" : "no",
		 tawk_drv_client_link_down_count(client),
		 tawk_drv_client_handle_req_count(client),
		 tawk_drv_client_handle_resp_count(client));

	file->private_data = buffer;
	return nonseekable_open(inode, file);
}

static int ipc_debug_client_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t ipc_debug_client_read(struct file *file, char __user *buf,
				     size_t len, loff_t *offset)
{
	char *buffer = file->private_data;
	return simple_read_from_buffer(buf, len, offset,
				       buffer, strlen(buffer));
}

static const struct file_operations ipc_debug_client_ops = {
	.owner = THIS_MODULE,
	.open = ipc_debug_client_open,
	.release = ipc_debug_client_release,
	.read = ipc_debug_client_read,
};

struct dentry *
ipc_debug_client_init(struct tawk_drv_client *client)
{
	char fname[128];

	struct dentry *dentry;
	struct dentry *ipc_debug_root;

	ipc_debug_root = ipc_debug_get_root();
	if (!ipc_debug_root) {
		dentry = ERR_PTR(-ENOENT);
		goto out;
	}

	snprintf(fname, sizeof(fname), "client_%s_%p",
		 client->tawk_data->dev_name, client);

	dentry = debugfs_create_file(fname, 0444, ipc_debug_root, client,
				    &ipc_debug_client_ops);
	if (!dentry) {
		pr_err("%s: failed to create client debugfs file.\n",
		       KBUILD_MODNAME);
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	} else if (IS_ERR(dentry)) {
		pr_err("%s: failed to create client debugfs file. rc=%ld\n",
			KBUILD_MODNAME, PTR_ERR(dentry));
		goto out;
	}

out:
	return dentry;
}

void ipc_debug_client_fini(struct dentry *dentry)
{
	debugfs_remove(dentry);
	ipc_debug_put_root();
}
