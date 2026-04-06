// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include <tawk_drv_debug/debug.h>
#include "record.h"

#include "../../ipc_internal.h"

static DEFINE_MUTEX(ipc_debug_mutex);
static refcount_t ipc_debug_refcnt = REFCOUNT_INIT(0);

static struct dentry *ipc_debug_root;
DEFINE_XARRAY(ipc_debug_table);

DEFINE_XARRAY(ipc_debug_instances);

#define ROW_SIZE (124 + 1)
#define NUM_ROWS 1024
#define MAX_READ_SIZE (ROW_SIZE * NUM_ROWS)
#define TOTAL_TABLE_COLUMNS 6
#define MAX_COLUMN_SIZE 32

struct ipc_debug_buffer {
	char *buffer;
	size_t size;
};

static const char ipc_debug_field_names[TOTAL_TABLE_COLUMNS][MAX_COLUMN_SIZE] = {
	{ "Address" },
	{ "Access" },
	{ "Count" },
	{ "Min, ns (@Uptime, sec)" },
	{ "Max, ns (@Uptime, sec)" },
	{ "Avg, ns\n" },
};

static const int ipc_debug_field_offsets[TOTAL_TABLE_COLUMNS] = {
	0, 24, 32, 48, 80, 112,
};

static const int ipc_debug_field_sizes[TOTAL_TABLE_COLUMNS] = {
	24, 8, 16, 32, 32, 12,
};

static void ipc_debug_init_record(struct ipc_debug_record *record)
{
	ipc_debug_set_counter(record, 0);
	ipc_debug_set_min(record, UINT_MAX);
	spin_lock_init(&record->lock);
}

static void ipc_debug_update_record(struct ipc_debug_record *record,
				    struct timespec64 *start_ts,
				    struct timespec64 *end_ts)
{
	s64 time_delta;
	int count;

	time_delta = timespec64_to_ns(end_ts) - timespec64_to_ns(start_ts);
	spin_lock(&record->lock);
	count = ipc_debug_get_counter(record);

	ipc_debug_set_avg(record,
		(count * ipc_debug_get_avg(record) + time_delta) / (count + 1));

	if (time_delta < ipc_debug_get_min(record)) {
		ipc_debug_set_min(record, time_delta);
		ipc_debug_set_min_ts(record, end_ts->tv_sec);
	}

	if (time_delta > ipc_debug_get_max(record)) {
		ipc_debug_set_max(record, time_delta);
		ipc_debug_set_max_ts(record, end_ts->tv_sec);
	}

	ipc_debug_inc_counter(record);
	spin_unlock(&record->lock);
}

static struct ipc_debug_entry *ipc_debug_alloc_entry(void)
{
	struct ipc_debug_entry *entry;

	entry = kzalloc(sizeof(struct ipc_debug_entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(ENOMEM);

	ipc_debug_init_record(&entry->read_record);
	ipc_debug_init_record(&entry->write_record);

	return entry;
}

static void ipc_debug_fini_entry(struct ipc_debug_entry *entry)
{
	kfree(entry);
}

static int ipc_debug_update_entry(struct xarray *table,
				  uintptr_t addr,
				  struct timespec64 *start_ts,
				  struct timespec64 *end_ts,
				  bool write)
{
	struct ipc_debug_record *record;
	struct ipc_debug_entry *entry;
	int rc;

retry_load:
	entry = xa_load(table, addr);
	if (unlikely(!entry)) {
		entry = ipc_debug_alloc_entry();

		rc = xa_insert(table, addr, (void *) entry, GFP_KERNEL);
		if (rc == -EBUSY) {
			/* Another thread created new entry first. Retry loading
			 * the entry.
			 */
			ipc_debug_fini_entry(entry);
			goto retry_load;
		} else if (rc) {
			/* Memory allocation failed - ENOMEM */
			ipc_debug_fini_entry(entry);
			return rc;
		}
	}

	if (write)
		record = &entry->write_record;
	else
		record = &entry->read_record;

	ipc_debug_update_record(record, start_ts, end_ts);
	return 0;
}

static void ipc_debug_finish_table(struct xarray *xarray)
{
	unsigned long idx;
	void *entry;

	xa_for_each(xarray, idx, entry) {
		xa_erase(xarray, idx);
		ipc_debug_fini_entry((struct ipc_debug_entry *) entry);
	}
}

uint32_t ipc_debug_sys_read32(uintptr_t a)
{
	struct timespec64 start_ts, end_ts;
	uint32_t val;

	ktime_get_ts64(&start_ts);
	val = *(volatile uint32_t *)(a);
	ktime_get_ts64(&end_ts);
	ipc_debug_update_entry(&ipc_debug_table, a, &start_ts, &end_ts, false);
	return val;
}

void ipc_debug_sys_write32(uint32_t v, uintptr_t a)
{
	struct timespec64 start_ts, end_ts;

	ktime_get_ts64(&start_ts);
	*(volatile uint32_t *)(a) = v;
	ktime_get_ts64(&end_ts);
	ipc_debug_update_entry(&ipc_debug_table, a, &start_ts, &end_ts, true);
}

uint64_t ipc_debug_sys_read64(uintptr_t a)
{
	struct timespec64 start_ts, end_ts;
	uint64_t val;

	ktime_get_ts64(&start_ts);
	val = *(volatile uint64_t *)(a);
	ktime_get_ts64(&end_ts);
	ipc_debug_update_entry(&ipc_debug_table, a, &start_ts, &end_ts, false);
	return val;
}

void ipc_debug_sys_write64(uint64_t v, uintptr_t a)
{
	struct timespec64 start_ts, end_ts;

	ktime_get_ts64(&start_ts);
	*(volatile uint64_t *)(a) = v;
	ktime_get_ts64(&end_ts);
	ipc_debug_update_entry(&ipc_debug_table, a, &start_ts, &end_ts, true);
}

static void ipc_debug_print_table_heading(char *buf)
{
	char field[MAX_COLUMN_SIZE], line[ROW_SIZE];

	memset(line, ' ', ROW_SIZE);
	for (int i = 0; i < TOTAL_TABLE_COLUMNS; i++) {
		snprintf(field, ipc_debug_field_sizes[i], "%s",
			 ipc_debug_field_names[i]);
		strncpy(line + ipc_debug_field_offsets[i], field, strlen(field));
	}

	line[ROW_SIZE - 1] = '\n';
	memcpy(buf, line, ROW_SIZE);
}

static void ipc_debug_print_table_line(char *buf, int *line_num,
				       uintptr_t addr, const char *access,
				       struct ipc_debug_record *record)
{
	char field[MAX_COLUMN_SIZE], line[ROW_SIZE];

	if (ipc_debug_get_counter(record) == 0)
		return;

	memset(line, ' ', ROW_SIZE);
	snprintf(field, ipc_debug_field_sizes[0], "%-lx", addr);
	strncpy(line, field, strlen(field));

	snprintf(field, ipc_debug_field_sizes[1], "%-s", access);
	strncpy(line + ipc_debug_field_offsets[1], field, strlen(field));

	snprintf(field, ipc_debug_field_sizes[2], "%-d",
		 ipc_debug_get_counter(record));
	strncpy(line + ipc_debug_field_offsets[2], field, strlen(field));

	snprintf(field, ipc_debug_field_sizes[3], "%-llu (@%llu)",
		 ipc_debug_get_min(record), ipc_debug_get_min_ts(record));
	strncpy(line + ipc_debug_field_offsets[3], field, strlen(field));

	snprintf(field, ipc_debug_field_sizes[4], "%-llu (@%llu)",
		 ipc_debug_get_max(record), ipc_debug_get_max_ts(record));
	strncpy(line + ipc_debug_field_offsets[4], field, strlen(field));

	snprintf(field, ipc_debug_field_sizes[5], "%-llu",
		 ipc_debug_get_avg(record));
	strncpy(line + ipc_debug_field_offsets[5], field, strlen(field));

	line[ROW_SIZE - 1] = '\n';
	memcpy(buf + *line_num * ROW_SIZE, line, ROW_SIZE);
	(*line_num)++;
}

static void ipc_debug_print_table_fini(char *buf, int line)
{
	buf[line * ROW_SIZE] = '\0';
}

static int ipc_debug_format_table(struct xarray *xarray, char *buf)
{
	unsigned long idx;
	int line_num = 1;
	void *entry;

	ipc_debug_print_table_heading(buf);

	xa_for_each(xarray, idx, entry) {
		if (entry) {
			if (line_num > MAX_READ_SIZE)
				return -EFAULT;

			ipc_debug_print_table_line(buf, &line_num, idx, "R",
				&((struct ipc_debug_entry *) entry)->read_record);

			if (line_num > MAX_READ_SIZE)
				return -EFAULT;

			ipc_debug_print_table_line(buf, &line_num, idx, "W",
				&((struct ipc_debug_entry *) entry)->write_record);
		}
	}

	ipc_debug_print_table_fini(buf, line_num);

	return line_num * ROW_SIZE;
}

static int ipc_debug_open(struct inode *inode, struct file *file)
{
	struct ipc_debug_buffer *debug_buffer;
	char *buffer;
	size_t size;

	debug_buffer = kzalloc(sizeof(struct ipc_debug_buffer), GFP_KERNEL);
	if (!debug_buffer)
		return -ENOMEM;

	buffer = kzalloc(MAX_READ_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	size = ipc_debug_format_table(&ipc_debug_table, buffer);
	if (size < 0)
		return size;

	debug_buffer->buffer = buffer;
	debug_buffer->size = size;

	file->private_data = debug_buffer;
	return nonseekable_open(inode, file);
}

static int ipc_debug_release(struct inode *inode, struct file *file)
{
	struct ipc_debug_buffer *debug_buffer;

	debug_buffer = file->private_data;

	kfree(debug_buffer->buffer);
	kfree(debug_buffer);
	return 0;
}

static ssize_t ipc_debug_read(struct file *file, char __user *buf,
			      size_t len, loff_t *offset)
{
	struct ipc_debug_buffer *debug_buffer;

	debug_buffer = file->private_data;
	return simple_read_from_buffer(buf, len, offset,
				       debug_buffer->buffer,
				       debug_buffer->size);
}

static const struct file_operations ipc_debug_file_ops = {
	.owner = THIS_MODULE,
	.open = ipc_debug_open,
	.release = ipc_debug_release,
	.read = ipc_debug_read,
};

static int ipc_debug_instance_init(struct ipc_context_s *ipc, struct dentry *root);
static void ipc_debug_instance_fini(struct ipc_context_s *ipc);

int ipc_debug_init(struct ipc_context_s *ipc)
{
	struct dentry *entry;
	int rc = 0;

	mutex_lock(&ipc_debug_mutex);
	if (ipc_debug_root) {
		rc = ipc_debug_instance_init(ipc, ipc_debug_root);
		if (!rc)
			refcount_inc(&ipc_debug_refcnt);

		mutex_unlock(&ipc_debug_mutex);
		return rc;
	}

	/* Create top-level directory */
	ipc_debug_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!ipc_debug_root) {
		pr_err("%s: failed to create root debugfs directory.\n",
		       KBUILD_MODNAME);
		rc = -ENOMEM;
		goto err_root;
	} else if (IS_ERR(ipc_debug_root)) {
		rc = PTR_ERR(ipc_debug_root);
		pr_err("%s: failed to create root debugfs directory. rc=%d\n",
			KBUILD_MODNAME, rc);
		goto err_root;
	}

	entry = debugfs_create_file("bar_access_stats", 0444, ipc_debug_root,
				    &ipc_debug_table, &ipc_debug_file_ops);
	if (!entry) {
		pr_err("%s: failed to create bar_access_stats debugfs file.\n",
		       KBUILD_MODNAME);
		rc = -ENOMEM;
		goto err_file;
	} else if (IS_ERR(entry)) {
		rc = PTR_ERR(entry);
		pr_err("%s: failed to create bar_access_stats debugfs file. rc=%d\n",
			KBUILD_MODNAME, rc);
		goto err_file;
	}

	refcount_set(&ipc_debug_refcnt, 1);

	rc = ipc_debug_instance_init(ipc, ipc_debug_root);
	if (rc)
		goto err_instance;

	mutex_unlock(&ipc_debug_mutex);
	return rc;

err_instance:
	refcount_set(&ipc_debug_refcnt, 0);

err_file:
	debugfs_remove(ipc_debug_root);

err_root:
	ipc_debug_root = NULL; /* unset because it might be IS_ERR */
	mutex_unlock(&ipc_debug_mutex);
	return rc;
}

void ipc_debug_fini(struct ipc_context_s *ipc)
{
	mutex_lock(&ipc_debug_mutex);

	ipc_debug_instance_fini(ipc);

	if (!refcount_dec_and_test(&ipc_debug_refcnt))
		goto out;

	debugfs_remove(ipc_debug_root);
	ipc_debug_root = NULL;
	ipc_debug_finish_table(&ipc_debug_table);

out:
	mutex_unlock(&ipc_debug_mutex);
}

struct dentry *ipc_debug_get_root(void)
{
	struct dentry *res;

	mutex_lock(&ipc_debug_mutex);

	res = ipc_debug_root;
	if (res)
		refcount_inc(&ipc_debug_refcnt);

	mutex_unlock(&ipc_debug_mutex);
	return res;
}

void ipc_debug_put_root(void)
{
	mutex_lock(&ipc_debug_mutex);

	if (!refcount_dec_and_test(&ipc_debug_refcnt))
		goto out;

	debugfs_remove(ipc_debug_root);
	ipc_debug_root = NULL;
	ipc_debug_finish_table(&ipc_debug_table);

out:
	mutex_unlock(&ipc_debug_mutex);
}

static int ipc_debug_instance_open(struct inode *inode, struct file *file)
{
	struct ipc_context_s *ipc;
	ipc_hardware_mailbox_ctx_t *mb;
	char *buffer, *cur, *end;

	ipc = inode->i_private;
	mb = &ipc->hw.mbox;

	buffer = kzalloc(MAX_READ_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	cur = buffer;
	end = buffer + MAX_READ_SIZE;

	ipc_lock(ipc);

#ifdef TAWK_IPC_PROFILE
	cur += snprintf(cur, end - cur, "mbox wq:\n");

	cur += snprintf(cur, end - cur, "  runtime:\n");
	cur += snprintf(cur, end - cur, "    min (us): %llu\n",
			mb->active_prof.min.delta / 1000);
	cur += snprintf(cur, end - cur, "    max (us): %llu\n",
			mb->active_prof.max.delta / 1000);
	cur += snprintf(cur, end - cur, "      at/uptime (sec): %llu\n",
			mb->active_prof.max.at / 1000 / 1000 / 1000);
	cur += snprintf(cur, end - cur, "      cpu: %i\n",
			mb->active_prof.max.cpu);
	cur += snprintf(cur, end - cur, "    mean (us): %llu\n",
			ipc_prof_mean(&mb->active_prof) / 1000);

	cur += snprintf(cur, end - cur, "  idle:\n");
	cur += snprintf(cur, end - cur, "    min (us): %llu\n",
			mb->idle_prof.min.delta / 1000);
	cur += snprintf(cur, end - cur, "    max (us): %llu\n",
			mb->idle_prof.max.delta / 1000);
	cur += snprintf(cur, end - cur, "      at/uptime (sec): %llu\n",
			mb->idle_prof.max.at / 1000 / 1000 / 1000);
	cur += snprintf(cur, end - cur, "    mean (us): %llu\n",
			ipc_prof_mean(&mb->idle_prof) / 1000);
#endif

	ipc_unlock(ipc);

	file->private_data = buffer;
	return nonseekable_open(inode, file);
}

static int ipc_debug_instance_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t ipc_debug_instance_read(struct file *file, char __user *buf,
				       size_t len, loff_t *offset)
{
	char *buffer = file->private_data;
	return simple_read_from_buffer(buf, len, offset,
				       buffer, strlen(buffer));
}

static const struct file_operations ipc_debug_instance_ops = {
	.owner = THIS_MODULE,
	.open = ipc_debug_instance_open,
	.release = ipc_debug_instance_release,
	.read = ipc_debug_instance_read,
};

static int ipc_debug_instance_init(struct ipc_context_s *ipc, struct dentry *root)
{
	char fname[128];
	struct dentry *leaf;
	int rc;

	snprintf(fname, sizeof(fname), "ipc_%p", ipc);

	leaf = debugfs_create_file(fname, 0444, root, ipc,
				   &ipc_debug_instance_ops);
	if (!leaf) {
		pr_err("%s: failed to create IPC debugfs file.\n",
		       KBUILD_MODNAME);
		return -1;
	} else if (IS_ERR(leaf)) {
		pr_err("%s: failed to create IPC debugfs file. rc=%ld\n",
			KBUILD_MODNAME, PTR_ERR(leaf));
		return -1;
	}

	rc = xa_insert(&ipc_debug_instances, (unsigned long)ipc, leaf, GFP_KERNEL);
	if (rc) {
		pr_err("%s: failed to register IPC debugfs file. rc=%d\n",
			KBUILD_MODNAME, rc);
		debugfs_remove(leaf);
	}

	return rc;
}

static void ipc_debug_instance_fini(struct ipc_context_s *ipc)
{
	struct dentry *leaf = xa_load(&ipc_debug_instances, (unsigned long)ipc);
	if (!leaf)
		return;

	xa_erase(&ipc_debug_instances, (unsigned long)ipc);
	debugfs_remove(leaf);
}
