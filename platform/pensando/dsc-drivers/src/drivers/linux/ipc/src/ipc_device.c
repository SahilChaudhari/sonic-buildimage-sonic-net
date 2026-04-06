// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <tawk/device.h>
#include <tawk/ipc.h>
#include <tawk/platform.h>
#include <tawk_ipc_drv/ioctl.h>
#include "ipc_device.h"
#include "ipc_req_handle.h"
#include "ipc_channel.h"

#ifdef TAWK_DRV_DEBUG
#include <tawk_ipc_drv/ioctl_debug.h>
#include "ipc_debug.h"
#endif

DEFINE_IDA(tawk_drv_device_id);

static int tawk_drv_device_id_alloc(void)
{
	int id;

	id = ida_alloc(&tawk_drv_device_id, GFP_KERNEL);
	if (id > TAWK_DEVICE_MAX_ID)
		return -EAGAIN;

	return id;
}

static void tawk_drv_device_id_free(int id)
{
	ida_free(&tawk_drv_device_id, id);
}

static void tawk_drv_req_table_free(struct list_head *table)
{
	struct tawk_drv_req_state *curr, *nxt;

	list_for_each_entry_safe(curr, nxt, table, req_table_elem) {
		list_del(&curr->req_table_elem);
		kfree(curr);
	}
}

static enum tawk_drv_link_down_mode tawk_drv_client_link_down_mode(struct tawk_drv_client *client)
{
	enum tawk_drv_link_down_mode ret;

	spin_lock(&client->link_down_mode_lock);
	ret = client->link_down_mode;
	spin_unlock(&client->link_down_mode_lock);

	return ret;
}

static void tawk_drv_req_table_fini(struct tawk_drv_client *client)
{
	tawk_drv_req_table_free(&client->req_free);
}

static void tawk_drv_dealloc_req_state(struct tawk_drv_client *client,
				       struct tawk_drv_req_state *req_state);

/* Flush the req_in_use list.
 *
 * This requires us to wait for the TAWK IPC library to invoke
 * the asynchronous response handler tawk_drv_handle_resp().
 *
 * Upon completion, we empty the req_in_use list and return all
 * corresponding ipc_buffer_t instances to the TAWK IPC library.
 */
static void tawk_drv_req_table_finish_responses(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	struct tawk_drv_req_state *read_item;

	/* Each instance of tawk_drv_req_state on the req_in_use list
	 * must eventually end up on the read_q list. Hence, if we
	 * consume all read_q items, we clear up req_in_use.
	 */
	spin_lock(&client->req_wait_q.lock);
	while (!tawk_drv_req_table_empty(client)) {
		spin_unlock(&client->req_wait_q.lock);

		/* Get one tawk_drv_req_state.
		 *
		 * We cannot use tawk_drv_device_read_ready() because
		 * we want to proceed even if the link is down.
		 */
		wait_event(client->read_wait_q, !list_empty(&client->read_q));

		spin_lock(&client->read_wait_q.lock);

		/* Protect ourselves against spurious wakeups. */
		if (list_empty(&client->read_q)) {
			spin_unlock(&client->read_wait_q.lock);
			spin_lock(&client->req_wait_q.lock);
			continue;
		}
		read_item = list_entry((&client->read_q)->next,
				struct tawk_drv_req_state, list_elem);
		list_del(&read_item->list_elem);
		spin_unlock(&client->read_wait_q.lock);

		/* Release tawk_drv_req_state if it is a response */
		if (read_item->state == TAWK_DRV_RSP || read_item->state == TAWK_DRV_TP_ERR) {
			ipc_lock(ipc);
			ipc_response_free(ipc, read_item->buffer);
			ipc_unlock(ipc);
		}

		/* Remove from the req_in_use list */
		spin_lock(&client->req_wait_q.lock);
		tawk_drv_dealloc_req_state(client, read_item);
	}

	spin_unlock(&client->req_wait_q.lock);
}

static struct tawk_drv_req_state *tawk_drv_get_rsp_state(struct tawk_drv_client *client,
							 tawk_drv_hdr_tag_t tag);

static void tawk_drv_ipc_lock_offline(struct tawk_drv_client *client);

static void tawk_drv_rsp_table_finish_responses(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;

	mutex_lock(&client->rsp_table_lock);

	/* Check the entire response table (we act as a target). */
	for (int i = 0; i < tawk_data->rsp_limit; i++) {
		struct tawk_drv_req_state *rsp;

		rsp = tawk_drv_get_rsp_state(client, i);
		if (!rsp)
			continue;

		/* We found an unhandled request that we must complete with
		 * the core IPC library. To do so gracefully, we must wait for
		 * the link to go down and respond with the fake/synthesised
		 * response.
		 *
		 * The link will go down due to the transport timeout on the
		 * remote/initiator side. Then, it won't come up again, even
		 * in the "invincible" mode 2, because the core IPC library
		 * still expects us to return the IPC buffer with the response.
		 *
		 * The response can be anything because it won't be delivered
		 * to the initiator. Still, we must make the ipc_response_send()
		 * call for the graceful resource release.
		 */

		tawk_drv_ipc_lock_offline(client);

		ipc_request_make_response(ipc, rsp->buffer);
		ipc_response_send(ipc, rsp->buffer);

		ipc_unlock(ipc);
	}

	mutex_unlock(&client->rsp_table_lock);
}

bool tawk_drv_req_table_empty(struct tawk_drv_client *client)
{
	WARN_ON(!spin_is_locked(&client->req_wait_q.lock));

	return list_empty(&client->req_in_use);
}

static int tawk_drv_req_table_init_size(struct tawk_drv_client *client,
					size_t table_size)
{
	struct tawk_drv_req_state *req_state;

	INIT_LIST_HEAD(&client->req_free);
	INIT_LIST_HEAD(&client->req_in_use);

	/* Populate table_size entries in req_free list */
	for (int i = 0; i < table_size; i++) {
		req_state = kzalloc(sizeof(*req_state), GFP_KERNEL);
		if (!req_state)
			goto err_free_req_table;

		req_state->state = TAWK_DRV_FREE;
		list_add_tail(&req_state->req_table_elem, &client->req_free);
	}
	return 0;

err_free_req_table:
	tawk_drv_req_table_free(&client->req_free);
	return -ENOMEM;
}

static int tawk_drv_req_table_init(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;

	return tawk_drv_req_table_init_size(client, tawk_data->req_limit);
}

int tawk_drv_req_table_resize(struct tawk_drv_client *client,
			      size_t size)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;

	WARN_ON(!spin_is_locked(&client->req_wait_q.lock));
	WARN_ON(!tawk_drv_req_table_empty(client));

	if (size > tawk_data->req_limit)
		return -EINVAL;

	tawk_drv_req_table_fini(client);
	tawk_drv_req_table_init_size(client, size);

	return 0;
}

static void tawk_drv_rsp_table_fini(struct tawk_drv_client *client)
{
	kfree(client->rsp_table);
}

static int tawk_drv_rsp_table_init(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;

	client->rsp_table = kcalloc(tawk_data->rsp_limit,
				    sizeof(struct tawk_drv_req_state),
				    GFP_KERNEL);
	if (!client->rsp_table)
		return -ENOMEM;

	for (int i = 0; i < tawk_data->rsp_limit; i++)
		client->rsp_table[i].user_tag = i;

	return 0;
}

static int tawk_drv_buffer_try_alloc_pool(struct tawk_drv_client *client,
					  ipc_buffer_t **buffer)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	ipc_buffer_t *allocated_buffer;

	ipc_assert_locked(ipc);

	allocated_buffer = ipc_request_pool_try_alloc(ipc, client->req_pool);
	if (!allocated_buffer)
		return -EAGAIN;

	*buffer = allocated_buffer;
	return 0;
}

static int tawk_drv_buffer_try_alloc(struct tawk_drv_client *client,
				     ipc_buffer_t **buffer)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	ipc_buffer_t *allocated_buffer;

	ipc_assert_locked(ipc);

	allocated_buffer = ipc_request_try_alloc(ipc);
	if (!allocated_buffer)
		return -EAGAIN;

	*buffer = allocated_buffer;
	return 0;
}

static int tawk_drv_buffer_alloc_blocking(struct tawk_drv_client *client,
					  ipc_buffer_t **buffer)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	ipc_assert_locked(ipc);

	return ipc_request_alloc(ipc, buffer);
}

static int tawk_drv_alloc_buffer(struct tawk_drv_client *client,
				 ipc_buffer_t **buffer)
{
	if (client->req_pool)
		return tawk_drv_buffer_try_alloc_pool(client, buffer);
	else if (client->non_blocking)
		return tawk_drv_buffer_try_alloc(client, buffer);
	else
		return tawk_drv_buffer_alloc_blocking(client, buffer);
}

/* The lower-level TAWK IPC library calls tawk_drv_client_link_down()
 * when the userland opens a file descriptor, but then the datalink
 * goes down.
 *
 * This device driver has a choice whether to acknowledge it immediately
 * with ipc_link_down_event_ack() or defer until the userland closes
 * its file descriptor. The tawk_drv_client_link_down_mode() selects
 * the mode how to handle it.
 *
 * Locking: TAWK IPC library holds the IPC lock.
 */
static void tawk_drv_client_link_down(ipc_context_t *ipc,
				      ipc_link_event_handle_t handle,
				      void *cb_ctx)
{
	struct tawk_drv_client *client = (struct tawk_drv_client *) cb_ctx;

	++client->link_down_count;

	/* Set client into link-down state */
	spin_lock(&client->link_wait_q.lock);
	client->link_is_up = false;
	client->link_down_event = handle;
	spin_unlock(&client->link_wait_q.lock);

	/* This might wake up an uninterruptible
	 * tawk_drv_rsp_table_finish_responses() */
	wake_up(&client->link_wait_q);

	/* Not waking up the link_wait_q because the awaiting threads,
	 * who can handle the link down/up events, cannot act when the
	 * datalink is not up.
	 */

	switch (tawk_drv_client_link_down_mode(client)) {
	/* Disable further send requests via the write() syscall and defer
	 * the datalink down acknowledgement to the TAWK IPC library until
	 * the userland closes its file descriptor.
	 */
	default:
	case TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN:
		/* Deferred ACK */
		wake_up_interruptible_all(&client->read_wait_q);
		break;

	/* The same as above, but acknowledge the datalink down event
	 * immediately. The datalink might go up again, but it does not
	 * apply to this file descriptor.
	 */
	case TAWK_DRV_LINK_DOWN_MODE_REOPEN:
		ipc_link_down_event_ack(ipc, handle);
		wake_up_interruptible_all(&client->read_wait_q);

		/* Additionally, tell the TAWK IPC library that we are
		 * no longer willing to receive new requests when the
		 * datalink goes up again.
		 */
		tawk_drv_req_handler_free_client_handlers(client);
		break;

	/* Disable further send requests until the datalink goes up again.
	 * Then carry on as usual as if the datalink has never gone down.
	 */
	case TAWK_DRV_LINK_DOWN_MODE_IGNORE:
		ipc_link_down_event_ack(ipc, handle);
		/* Not waking up the read_wait_q because the awaiting threads
		 * cannot act on that in this mode.
		 */
		break;
	}
}

static void tawk_drv_client_link_up(ipc_context_t *ipc,
				    ipc_link_event_handle_t handle,
				    void *cb_ctx)
{
	struct tawk_drv_client *client = (struct tawk_drv_client *) cb_ctx;

	/* If we have not acknowledged the datalink down event,
	 * it is incorrect for the TAWK IPC library to notify us
	 * about the datalink up event.
	 */
	BUG_ON(tawk_drv_client_link_down_mode(client) == TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN);

	ipc_link_up_event_ack(ipc, handle);

	/* If we are running in one of the strict modes, the link up event
	 * will not make the file descriptor functional by unsetting the
	 * link_down_event.
	 */
	if (tawk_drv_client_link_down_mode(client) != TAWK_DRV_LINK_DOWN_MODE_IGNORE)
		return;

	spin_lock(&client->link_wait_q.lock);
	client->link_is_up = true;
	spin_unlock(&client->link_wait_q.lock);

	wake_up_interruptible_all(&client->link_wait_q);
}

static int tawk_drv_device_open(struct inode *inode, struct file *file)
{
	struct tawk_drv_client *client;
	int rc;

	/* IDEA: Get tawk_drv_data from file private_data and replace file
	 * private_data with the fd specific state i.e. tawk_drv_client.
	 */
	struct miscdevice *mdev = file->private_data;
	struct tawk_drv_data *tawk_data = dev_get_drvdata(mdev->parent);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	INIT_LIST_HEAD(&client->read_q);
	init_waitqueue_head(&client->read_wait_q);
	init_waitqueue_head(&client->req_wait_q);
	init_waitqueue_head(&client->link_wait_q);
	mutex_init(&client->rsp_table_lock);

	client->tawk_data = tawk_data;

	rc = tawk_drv_req_table_init(client);
	if (rc)
		goto err_free_client;

	rc = tawk_drv_rsp_table_init(client);
	if (rc)
		goto err_free_req_table;

	client->non_blocking = file->f_flags & O_NONBLOCK;

	file->private_data = client;

	client->link_cb = (ipc_link_event_cb_t) {
		.link_up_cb = tawk_drv_client_link_up,
		.link_down_cb = tawk_drv_client_link_down,
		.cb_ctx = client,
	};

	spin_lock_init(&client->link_down_mode_lock);
	client->link_down_mode = tawk_drv_get_link_down_mode();

	ipc_lock(tawk_data->ipc_stack);
	client->link_is_up = ipc_link_is_up(tawk_data->ipc_stack);

	/* We might be racing with the link down event handling. In this case,
	 * if we carry on with this file descriptor, we should expect the link
	 * up event, which we disallow in the "strict" link handling mode. The
	 * easiest is to fail open() and let the user retry later.
	 */
	if (!client->link_is_up && client->link_down_mode == TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN) {
		rc = -ENOLINK;
		goto err_free_rsp_table;
	}

	ipc_register_link_event_handler(tawk_data->ipc_stack,
					&client->link_cb);
	ipc_unlock(tawk_data->ipc_stack);

#ifdef TAWK_DRV_DEBUG
	atomic_set(&client->handle_req_total, 0);
	atomic_set(&client->handle_resp_total, 0);

	/* A failure to create debugfs file is non-fatal */
	client->debug_dentry = ipc_debug_client_init(client);
	if (IS_ERR(client->debug_dentry)) {
		dev_warn(tawk_data->dev, "Cannot create debugfs entry (rc=%ld)",
			 PTR_ERR(client->debug_dentry));
		client->debug_dentry = NULL;
	}
#endif

	refcount_inc(&tawk_data->refs);

	return 0;

err_free_rsp_table:
	ipc_unlock(tawk_data->ipc_stack);
	tawk_drv_rsp_table_fini(client);

err_free_req_table:
	tawk_drv_req_table_fini(client);
err_free_client:
	mutex_destroy(&client->rsp_table_lock);
	kfree(client);
	return rc;
}

static int tawk_drv_device_release(struct inode *inode, struct file *file)
{
	struct tawk_drv_client *client = file->private_data;
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

#ifdef TAWK_DRV_DEBUG
	if (client->debug_dentry)
		ipc_debug_client_fini(client->debug_dentry);
#endif

	/* Unregister EPs to stop receiving incoming requests */
	tawk_drv_req_handler_free_client_handlers(client);

	tawk_drv_channel_fini(client);

	/* Wait for the responses for the pending outgoing requests
	 * and synthesise responses for the incoming requests */
	tawk_drv_req_table_finish_responses(client);
	tawk_drv_rsp_table_finish_responses(client);
	tawk_drv_req_table_fini(client);
	tawk_drv_rsp_table_fini(client);

	/* Possibly acknowledge the link down event (which we did not want
	 * to acknowledge until the user has closed its file descriptor)
	 * and unregister the client link callbacks */
	ipc_lock(ipc);

	ipc_deregister_link_event_handler(ipc, &client->link_cb);

	if (tawk_drv_client_link_down_mode(client) == TAWK_DRV_LINK_DOWN_MODE_ALL_REOPEN &&
	    !client->link_is_up)
		ipc_link_down_event_ack(ipc, client->link_down_event);

	ipc_unlock(ipc);

	mutex_destroy(&client->rsp_table_lock);
	tawk_drv_data_put(client->tawk_data);
	kfree(client);

	return 0;
}

static struct tawk_drv_req_state *tawk_drv_alloc_req_state(struct tawk_drv_client *client)
{
	struct tawk_drv_req_state *ini_req_state;

	WARN_ON(!spin_is_locked(&client->req_wait_q.lock));

	if (list_empty(&client->req_free))
		return NULL;

	ini_req_state = list_entry((&client->req_free)->next,
				    struct tawk_drv_req_state,
				    req_table_elem);

	ini_req_state->state = TAWK_DRV_REQ;

	list_del(&ini_req_state->req_table_elem);
	list_add_tail(&ini_req_state->req_table_elem, &client->req_in_use);
	return ini_req_state;
}

static void tawk_drv_dealloc_req_state(struct tawk_drv_client *client,
				       struct tawk_drv_req_state *req_state)
{
	WARN_ON(!spin_is_locked(&client->req_wait_q.lock));

	req_state->state = TAWK_DRV_FREE;

	list_del(&req_state->req_table_elem);
	list_add_tail(&req_state->req_table_elem, &client->req_free);
}

static struct tawk_drv_req_state *tawk_drv_alloc_rsp_state(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;

	WARN_ON(!mutex_is_locked(&client->rsp_table_lock));

	for (int i = 0; i < tawk_data->rsp_limit; i++) {
		if (client->rsp_table[i].state == TAWK_DRV_FREE)
			return &client->rsp_table[i];
	}
	return NULL;
}

/* Return one pending request to be handled by our client
 * or NULL if there is no such request for the given tag.
 */
static struct tawk_drv_req_state *tawk_drv_get_rsp_state(struct tawk_drv_client *client,
							 tawk_drv_hdr_tag_t tag)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;

	WARN_ON(!mutex_is_locked(&client->rsp_table_lock));

	if (tag >= tawk_data->rsp_limit)
		return NULL;

	if (client->rsp_table[tag].state != TAWK_DRV_REQ)
		return NULL;

	return &client->rsp_table[tag];
}

static void tawk_drv_handle_resp(ipc_context_t *ipc, int tp_err,
				 ipc_buffer_t *buffer, void *cb_ctx)
{
	struct tawk_drv_req_state *ini_req_state =
			(struct tawk_drv_req_state *) cb_ctx;
	struct tawk_drv_client *client = ini_req_state->parent_client;

	if (ipc_buffer_rsp_tp_err(buffer))
		ini_req_state->state = TAWK_DRV_TP_ERR;
	else
		ini_req_state->state = TAWK_DRV_RSP;

#ifdef TAWK_DRV_DEBUG
	atomic_inc(&client->handle_resp_total);
#endif

	spin_lock(&client->read_wait_q.lock);
	list_add_tail(&ini_req_state->list_elem, &client->read_q);
	spin_unlock(&client->read_wait_q.lock);

	/* Use wake_up() and not wake_up_interruptible() to possibly
	 * wake up the tawk_drv_req_table_finish_responses() function.
	 */
	wake_up(&client->read_wait_q);
}

static void tawk_drv_handle_req(ipc_context_t *ipc, ipc_buffer_t *buffer,
				void *cb_ctx)
{
	struct tawk_drv_client *client = (struct tawk_drv_client *) cb_ctx;
	struct tawk_drv_req_state *tgt_rsp_state;

	mutex_lock(&client->rsp_table_lock);
	tgt_rsp_state = tawk_drv_alloc_rsp_state(client);
	tgt_rsp_state->state = TAWK_DRV_REQ;
	mutex_unlock(&client->rsp_table_lock);

	tgt_rsp_state->buffer = buffer;
	tgt_rsp_state->parent_client = client;

#ifdef TAWK_DRV_DEBUG
	atomic_inc(&client->handle_req_total);
#endif

	spin_lock(&client->read_wait_q.lock);
	list_add_tail(&tgt_rsp_state->list_elem, &client->read_q);
	spin_unlock(&client->read_wait_q.lock);
	wake_up_interruptible(&client->read_wait_q);
}

/* Get the IPC lock ready for submitting requests (we are an initiator).
 *
 * Returns 0 on success if the IPC stack is locked and ready for I/O,
 * otherwise a negative number for the caller to pass to the userspace
 * application.
 *
 * This call is blocking. If, when it acquires the IPC lock, the datalink
 * is not up, it waits for further events from the datalink callbacks.
 *
 * WARNING It might be a deadlock if the core IPC library is awaiting
 * resources from us because the link will never come up in this situation.
 */
static int tawk_drv_ipc_lock_online(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	while (true) {
		int rc;

		ipc_lock(ipc);

		/* Success? */
		if (client->link_is_up)
			return 0;

		/* Failure, but is it fatal? */
		if (tawk_drv_client_link_down_mode(client) != TAWK_DRV_LINK_DOWN_MODE_IGNORE) {
			ipc_unlock(ipc);
			return -ENOLINK;
		}

		/* Failure, not fatal, need to wait until link goes up */
		ipc_unlock(ipc);

		spin_lock(&client->link_wait_q.lock);
		rc = wait_event_interruptible_locked(client->link_wait_q,
		                                     client->link_is_up);
		spin_unlock(&client->link_wait_q.lock);

		/* Failure again? This must be a pending signal */
		if (rc)
			return rc;
	}
}

/* Get the IPC lock for completing outstanding requests (we are a target).
 */
static void tawk_drv_ipc_lock_offline(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	while (true) {
		ipc_lock(ipc);

		/* Success? */
		if (!client->link_is_up) {
			return;
		}

		ipc_unlock(ipc);

		wait_event(client->link_wait_q, !client->link_is_up);
	}
}

static ssize_t tawk_drv_device_write(struct file *file,
				     const char __user *user_buffer,
				     size_t user_len,
				     loff_t *offset)
{
	struct tawk_drv_req_state *rsp_state, *ini_req_state;
	tawk_drv_hdr_type_t header_type;
	tawk_drv_hdr_tag_t user_tag;
	tawk_drv_hdr_t header;
	ipc_buffer_t *buffer = NULL;
	size_t pld_len;
	int rc;

	struct tawk_drv_client *client = file->private_data;
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	if (user_len < TAWK_DRV_HDR_LEN)
		return -EINVAL;

	if (user_len > ipc_buffer_pld_size(buffer))
		return -EINVAL;

	rc = copy_from_user(&header, user_buffer, TAWK_DRV_HDR_LEN);
	if (rc)
		return -EFAULT;

	pld_len = tawk_drv_hdr_get_pld_len(header);
	if (user_len != pld_len + TAWK_DRV_HDR_LEN)
		return -EINVAL;

	user_tag = tawk_drv_hdr_get_tag(header);
	header_type = tawk_drv_hdr_get_type(header);

	switch (header_type) {
	case TAWK_DRV_HDR_TYPE_RSP:
		/* Get response table entry associated with the tag */
		mutex_lock(&client->rsp_table_lock);
		rsp_state = tawk_drv_get_rsp_state(client, user_tag);
		mutex_unlock(&client->rsp_table_lock);

		if (!rsp_state) {
			/* Shouldn't happen without client side error. Would happen
			 * if use attempted to respond with a tag that was not in the
			 * response table.
			 */
			return -ENOENT;
		}

		/* Acquire the IPC lock even if the link is down.
		 *
		 * If the link is down while we have at least one unhandled
		 * request from the remote side (which we are at this point),
		 * it won't go up until we've returned all outstanding buffers
		 * to the core IPC library.
		 */
		ipc_lock(ipc);
		ipc_request_make_response(ipc, rsp_state->buffer);
		ipc_unlock(ipc);

		ipc_buffer_set_rsp_errno(rsp_state->buffer,
					 tawk_drv_hdr_get_rsp_errno(header));
		ipc_buffer_set_pld_len(rsp_state->buffer,
				       pld_len);

		rc = copy_from_user(ipc_buffer_pld_ptr(rsp_state->buffer),
				    user_buffer + TAWK_DRV_HDR_LEN,
				    user_len - TAWK_DRV_HDR_LEN);
		if (rc)
			return -EFAULT;

		ipc_lock(ipc);
		ipc_response_send(ipc, rsp_state->buffer);
		ipc_unlock(ipc);

		/* Clear the response buffer state */
		mutex_lock(&client->rsp_table_lock);
		rsp_state->state = TAWK_DRV_FREE;
		rsp_state->buffer = NULL;
		mutex_unlock(&client->rsp_table_lock);

		break;
	case TAWK_DRV_HDR_TYPE_REQ:
		rc = tawk_drv_ipc_lock_online(client);

		/* Return if unable to continue with this write() invocation,
		 * e.g. the file descriptor is no longer valid, or a signal
		 * has interrupted it.
		 */
		if (rc)
			return rc;

		rc = tawk_drv_alloc_buffer(client, &buffer);
		ipc_unlock(ipc);

		/* Return if cannot allocate an IPC buffer */
		if (rc)
			return rc;

		/* If req_table has no free entries, wait until one response
		 * has been read and has set entry back to TAWK_DRV_FREE.
		 */
		spin_lock(&client->req_wait_q.lock);
		rc = wait_event_interruptible_locked(client->req_wait_q,
						     !list_empty(&client->req_free));
		if (rc) {
			spin_unlock(&client->req_wait_q.lock);
			goto err_free_buffer;
		}

		ini_req_state = tawk_drv_alloc_req_state(client);
		if (!ini_req_state) {
			spin_unlock(&client->req_wait_q.lock);
			rc = -ENOLINK;
			goto err_free_buffer;
		}

		ini_req_state->state = TAWK_DRV_REQ;
		spin_unlock(&client->req_wait_q.lock);

		ini_req_state->buffer = buffer;
		ini_req_state->parent_client = client;
		ini_req_state->user_tag = user_tag;

		ipc_buffer_set_req_endpoint(ini_req_state->buffer,
					    tawk_drv_hdr_get_req_endpoint(header));
		ipc_buffer_set_pld_len(ini_req_state->buffer,
				       pld_len);

		rc = copy_from_user(ipc_buffer_pld_ptr(ini_req_state->buffer),
				    user_buffer + TAWK_DRV_HDR_LEN,
				    user_len - TAWK_DRV_HDR_LEN);
		if (rc) {
			rc = -EFAULT;
			goto err_dealloc_req_state;
		}

		/* Here, if the link is down, it won't go up again because
		 * we retain at least one IPC buffer. Hence, we simply grab
		 * the IPC lock and check if the link is still up. If not,
		 * we cannot proceed.
		 */
		ipc_lock(ipc);

		if (!client->link_is_up) {
			ipc_unlock(ipc);
			rc = tawk_drv_client_link_down_mode(client) != TAWK_DRV_LINK_DOWN_MODE_IGNORE ?
				-ENOLINK : -EAGAIN;
			goto err_dealloc_req_state;
		}

		ipc_request_send_async(ipc, ini_req_state->buffer,
				       tawk_drv_handle_resp, ini_req_state);
		ipc_unlock(ipc);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return user_len;

err_dealloc_req_state:
	spin_lock(&client->req_wait_q.lock);
	tawk_drv_dealloc_req_state(client, ini_req_state);
	spin_unlock(&client->req_wait_q.lock);
err_free_buffer:
	ipc_lock(ipc);
	ipc_request_free(ipc, buffer);
	ipc_unlock(ipc);
	return rc;
}

static void tawk_drv_device_read_set_params(struct tawk_drv_req_state *read_item,
					    size_t *read_len,
					    size_t *pld_len)
{
	switch (read_item->state) {
	case TAWK_DRV_RSP:
	case TAWK_DRV_REQ:
		*pld_len = ipc_buffer_get_pld_len(read_item->buffer);
		*read_len = *pld_len + TAWK_DRV_HDR_LEN;
		break;
	case TAWK_DRV_TP_ERR:
		*pld_len = 0;
		*read_len = TAWK_DRV_HDR_LEN;
		break;
	case TAWK_DRV_FREE:
		/* Should not be possible */
		BUG_ON(1);
	}
}

/* Can read() proceed without blocking? */
static bool tawk_drv_device_read_ready(struct tawk_drv_client *client)
{
	WARN_ON(!spin_is_locked(&client->read_wait_q.lock));

	/* Yes, there is something to read */
	if (!list_empty(&client->read_q))
		return true;

	/* No, when the user decides to ignore the link events */
	if (tawk_drv_client_link_down_mode(client) == TAWK_DRV_LINK_DOWN_MODE_IGNORE)
		return false;

	/* Yes, when the user wants to close the IPC file on link down */
	return !client->link_is_up;
}

static ssize_t tawk_drv_device_read(struct file *file,
				    char __user *user_buffer,
				    size_t user_len,
				    loff_t *offset)
{
	struct tawk_drv_client *client = file->private_data;
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	struct tawk_drv_req_state *read_item;
	tawk_drv_hdr_t header;
	size_t read_len = 0;
	size_t pld_len = 0;
	int rc;

	if (user_len < TAWK_DRV_HDR_LEN)
		return -EINVAL;

	spin_lock(&client->read_wait_q.lock);
	if (file->f_flags & O_NONBLOCK && !tawk_drv_device_read_ready(client)) {
		spin_unlock(&client->read_wait_q.lock);
		return -EAGAIN;
	}

	rc = wait_event_interruptible_locked(client->read_wait_q,
	                                     tawk_drv_device_read_ready(client));
	if (rc) {
		spin_unlock(&client->read_wait_q.lock);
		return rc;
	}

	/* This is non-trivial. If our configuration is to invalidate the
	 * file descriptor when the datalink down event occurs, we might
	 * be woken up with an empty read list, so that we can convey it
	 * to the userland. However, if the user has decided to ignore
	 * the datalink down event, we should not be woken up and carry
	 * on reading as usual.
	 */
	if (list_empty(&client->read_q)) {
		spin_unlock(&client->read_wait_q.lock);
		return -ENOLINK;
	}

	read_item = list_entry((&client->read_q)->next,
				struct tawk_drv_req_state, list_elem);
	list_del(&read_item->list_elem);
	spin_unlock(&client->read_wait_q.lock);

	tawk_drv_device_read_set_params(read_item, &read_len, &pld_len);

	if (user_len < read_len) {
		rc = -EINVAL;
		goto err_replace_in_read_q;
	}

	switch (read_item->state) {
	case TAWK_DRV_RSP:
		tawk_drv_hdr_set_type(&header, TAWK_DRV_HDR_TYPE_RSP);
		tawk_drv_hdr_set_rsp_errno(&header,
				ipc_buffer_get_rsp_errno(read_item->buffer));
		tawk_drv_hdr_set_pld_len(&header, pld_len);
		tawk_drv_hdr_set_tag(&header, read_item->user_tag);

		rc = copy_to_user(user_buffer, &header, TAWK_DRV_HDR_LEN);
		if (rc)
			goto err_copy_failed;

		rc = copy_to_user(user_buffer + TAWK_DRV_HDR_LEN,
				  ipc_buffer_pld_ptr(read_item->buffer),
				  read_len - TAWK_DRV_HDR_LEN);
		if (rc)
			goto err_copy_failed;

		ipc_lock(ipc);
		ipc_response_free(ipc, read_item->buffer);
		ipc_unlock(ipc);

		spin_lock(&client->req_wait_q.lock);
		tawk_drv_dealloc_req_state(client, read_item);
		spin_unlock(&client->req_wait_q.lock);

		wake_up_interruptible(&client->req_wait_q);
		break;
	case TAWK_DRV_REQ:
		tawk_drv_hdr_set_type(&header, TAWK_DRV_HDR_TYPE_REQ);
		tawk_drv_hdr_set_pld_len(&header, pld_len);
		tawk_drv_hdr_set_req_endpoint(&header,
			ipc_buffer_get_req_endpoint(read_item->buffer));
		tawk_drv_hdr_set_tag(&header, read_item->user_tag);

		rc = copy_to_user(user_buffer, &header, TAWK_DRV_HDR_LEN);
		if (rc)
			goto err_copy_failed;

		rc = copy_to_user(user_buffer + TAWK_DRV_HDR_LEN,
				  ipc_buffer_pld_ptr(read_item->buffer),
				  read_len - TAWK_DRV_HDR_LEN);
		if (rc)
			goto err_copy_failed;
		break;
	case TAWK_DRV_TP_ERR:
		tawk_drv_hdr_set_type(&header, TAWK_DRV_HDR_TYPE_TPERR);
		tawk_drv_hdr_set_pld_len(&header, 0);
		tawk_drv_hdr_set_tperr_errno(&header,
				ipc_buffer_rsp_tp_err(read_item->buffer));

		rc = copy_to_user(user_buffer, &header, TAWK_DRV_HDR_LEN);
		if (rc)
			goto err_copy_failed;

		read_len = TAWK_DRV_HDR_LEN;

		ipc_lock(ipc);
		ipc_response_free(ipc, read_item->buffer);
		ipc_unlock(ipc);

		spin_lock(&client->req_wait_q.lock);
		tawk_drv_dealloc_req_state(client, read_item);
		spin_unlock(&client->req_wait_q.lock);

		wake_up_interruptible(&client->req_wait_q);
		break;
	default:
		/* Should *never* happen */
		WARN_ON(1);
		return -EOPNOTSUPP;
	}

	return read_len;

err_copy_failed:
	rc = -EFAULT;
err_replace_in_read_q:
	/* If read call fails replace the read item back in the read queue
	 * and wake up possibly other parallel read() invocations.
	 */
	spin_lock(&client->read_wait_q.lock);
	list_add(&read_item->list_elem, &client->read_q);
	spin_unlock(&client->read_wait_q.lock);
	wake_up_interruptible(&client->read_wait_q);
	return rc;
}

static int tawk_drv_device_reg_ep(struct tawk_drv_client *client,
				  unsigned long arg)
{
	uint32_t ep = (uint32_t) arg;

	return tawk_drv_req_handler_register(client, &tawk_drv_handle_req, ep);
}

static int tawk_drv_device_dereg_ep(struct tawk_drv_client *client,
				    unsigned long arg)
{
	uint32_t ep = (uint32_t) arg;

	return tawk_drv_req_handler_deregister(client, ep);
}

static int tawk_drv_device_get_pool_size(struct tawk_drv_client *client)
{
	return tawk_drv_channel_get_pool_size(client);
}

static int tawk_drv_device_set_pool_size(struct tawk_drv_client *client,
					 unsigned long arg)
{
	uint32_t size = (uint32_t) arg;

	return tawk_drv_channel_set_pool_size(client, size);
}

static int tawk_drv_device_get_reorder(struct tawk_drv_client *client)
{
	return 1;
}

static int tawk_drv_device_set_reorder(struct tawk_drv_client *client,
				       unsigned long arg)
{
	if (arg)
		return 0;
	else
		return -EOPNOTSUPP;
}

static int tawk_drv_client_set_link_down_mode(struct tawk_drv_client *client,
					      unsigned long arg)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	int rc = 0;

	if (arg > TAWK_DRV_LINK_DOWN_MODE_MAX)
		return -EINVAL;

	/* We must grab the lock to avoid racing with the link down event
	 * handler, which will make further link down mode amendments
	 * unsupported.
	 */
	ipc_lock(ipc);
	if (client->link_down_count) {
		rc = -ENOLINK;
		goto out;
	}
	spin_lock(&client->link_down_mode_lock);
	client->link_down_mode = arg;
	spin_unlock(&client->link_down_mode_lock);

out:
	ipc_unlock(ipc);
	return rc;
}

#ifdef TAWK_DRV_DEBUG
static int tawk_drv_device_reset_link(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;

	ipc_lock(ipc);
	ipc_request_reset(ipc);
	ipc_unlock(ipc);

	return 0;
}
#endif

static long tawk_drv_device_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct tawk_drv_client *client = file->private_data;
	int rc = 0;

	spin_lock(&client->link_wait_q.lock);
	if (tawk_drv_client_link_down_mode(client) != TAWK_DRV_LINK_DOWN_MODE_IGNORE &&
	    !client->link_is_up) {
		rc = -ENOLINK;
	}
	spin_unlock(&client->link_wait_q.lock);

	/* The ioctl() are not allowed once the link goes down.
	 * The user must close this file descriptor.
	 */
	if (rc)
		return rc;

	switch (cmd) {
	case TAWKIPCREGEP:
		return tawk_drv_device_reg_ep(client, arg);
	case TAWKIPCDEREGEP:
		return tawk_drv_device_dereg_ep(client, arg);
	case TAWKIPCGETPLSIZE:
		return tawk_drv_device_get_pool_size(client);
	case TAWKIPCSETPLSIZE:
		return tawk_drv_device_set_pool_size(client, arg);
	case TAWKIPCGETREORDER:
		return tawk_drv_device_get_reorder(client);
	case TAWKIPCSETREORDER:
		return tawk_drv_device_set_reorder(client, arg);
	case TAWKIPCGETLINKDOWNMODE:
		return tawk_drv_client_link_down_mode(client);
	case TAWKIPCSETLINKDOWNMODE:
		return tawk_drv_client_set_link_down_mode(client, arg);
#ifdef TAWK_DRV_DEBUG
	case TAWKIPCRESETLINK:
		return tawk_drv_device_reset_link(client);
#endif
	default:
		return -EINVAL;
	}
}

static __poll_t tawk_drv_device_poll(struct file *file,
				     struct poll_table_struct *poll_t)
{
	struct tawk_drv_client *client = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &client->read_wait_q, poll_t);

	spin_lock(&client->read_wait_q.lock);
	if (tawk_drv_device_read_ready(client))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock(&client->read_wait_q.lock);

	/*
	 * 1. File descriptor has an exclusive request pool:
	 *      We can report write readiness if request table is not full. The
	 *	FD's request table is the same size as its exclusive pool.
	 * 2. Else the file descriptor does not have exclusive pool:
	 *      Unless the client's request table is full, always report write
	 *	readiness. There is no sensible way to know whether all buffers
	 *	have been consumed by other FDs.
	 *
	 * NOTE: Writing response will never block.
	 */

	spin_lock(&client->req_wait_q.lock);
	if (!list_empty(&client->req_free))
		mask |= POLLOUT | POLLWRNORM;
	spin_unlock(&client->req_wait_q.lock);

	return mask;
}

static const struct file_operations tawk_drv_device_fops = {
	.owner = THIS_MODULE,
	.open = tawk_drv_device_open,
	.release = tawk_drv_device_release,
	.write = tawk_drv_device_write,
	.read = tawk_drv_device_read,
	.unlocked_ioctl = tawk_drv_device_ioctl,
	.poll = tawk_drv_device_poll,
};

static int tawk_drv_set_device_name(struct tawk_drv_data *tawk_data)
{
	int id;

	id = tawk_drv_device_id_alloc();
	if (id < 0)
		return id;

	/* TODO: Remove special case for /dev/tawkipcdev */
	if (id == 0) {
		snprintf(tawk_data->dev_name,
			 TAWK_DEVICE_NAME_LEN,
			 "%s", TAWK_DEVICE_NAME);
	} else {
		snprintf(tawk_data->dev_name,
			 TAWK_DEVICE_NAME_LEN,
			 "%s%03d", TAWK_DEVICE_NAME, id);
	}

	tawk_data->dev_id = id;
	return id;
}

static void tawk_drv_device_link_up_init(struct work_struct *work)
{
	struct tawk_drv_data *tawk_data =
		container_of(work, struct tawk_drv_data, misc_init_work);
	struct miscdevice *misc_device;
	int rc;

	misc_device = kzalloc(sizeof(*misc_device), GFP_KERNEL);
	if (!misc_device) {
		rc = -ENOMEM;
		goto err_alloc;
	}


	misc_device->name = tawk_data->dev_name;
	/* Choose next free minor number */
	misc_device->minor =  MISC_DYNAMIC_MINOR;
	misc_device->fops = &tawk_drv_device_fops;
	misc_device->parent = tawk_data->dev;

	rc = misc_register(misc_device);
	if (rc)
		goto err_register;

	tawk_data->mdev = misc_device;

	return;

err_register:
	kfree(misc_device);

err_alloc:
	WARN(rc, "Failed to create TAWK IPC device node rc=%d\n", rc);
}

static void tawk_drv_device_link_down_fini(struct work_struct *work)
{
	struct tawk_drv_data *tawk_data =
		container_of(work, struct tawk_drv_data, misc_fini_work);

	/* Unlikely, but might be NULL if misc_register() has failed */
	if (tawk_data->mdev) {
		misc_deregister(tawk_data->mdev);
		kfree(tawk_data->mdev);
		tawk_data->mdev = NULL;
	}

	tawk_drv_data_put(tawk_data);
}

static void tawk_drv_device_set_resource_sizes(struct tawk_drv_data *tawk_data)
{
	ipc_context_t *ipc = tawk_data->ipc_stack;
	size_t min_size, rc;

	ipc_assert_locked(ipc);

	tawk_data->req_limit = ipc_max_ini_requests(ipc);
	tawk_data->rsp_limit = ipc_max_tgt_requests(ipc);

	min_size = tawk_drv_minimum_common_pool_size();
	if (!min_size)
		min_size = tawk_data->req_limit;

	/* Ensure that initially the common pool size is all
	 * the available buffers
	 */
	rc = ipc_request_common_pool_try_resize(ipc, tawk_data->req_limit);
	if (rc < min_size)
		dev_err(tawk_data->dev,
			"Minimum expected common pool size %lu Actual size %lu",
			min_size, rc);
}

static void tawk_drv_device_link_up(ipc_context_t *ipc,
				    ipc_link_event_handle_t handle,
				    void *cb_ctx)
{
	struct tawk_drv_data *tawk_data = (struct tawk_drv_data *) cb_ctx;

	tawk_drv_device_set_resource_sizes(tawk_data);

	refcount_inc(&tawk_data->refs);
	queue_work(tawk_data->misc_workqueue, &tawk_data->misc_init_work);

	ipc_link_up_event_ack(ipc, handle);
}

static void tawk_drv_device_link_down(ipc_context_t *ipc,
				      ipc_link_event_handle_t handle,
				      void *cb_ctx)
{
	struct tawk_drv_data *tawk_data = (struct tawk_drv_data *) cb_ctx;

	/* On link down the device node disappears */
	queue_work(tawk_data->misc_workqueue, &tawk_data->misc_fini_work);
	ipc_link_down_event_ack(ipc, handle);
}

int tawk_drv_device_init(struct tawk_drv_data *tawk_data)
{
	int rc;

	mutex_init(&tawk_data->channel_lock);
	mutex_init(&tawk_data->req_handlers_lock);
	INIT_LIST_HEAD(&tawk_data->req_handlers);

	tawk_data->misc_workqueue = alloc_ordered_workqueue("tawkipcdev", 0);
	if (!tawk_data->misc_workqueue)
		return -ENOMEM;

	INIT_WORK(&tawk_data->misc_init_work, tawk_drv_device_link_up_init);
	INIT_WORK(&tawk_data->misc_fini_work, tawk_drv_device_link_down_fini);

	rc = tawk_drv_set_device_name(tawk_data);
	if (rc < 0)
		goto err_device_name;

	tawk_data->link_cb = (ipc_link_event_cb_t) {
		.link_up_cb   = tawk_drv_device_link_up,
		.link_down_cb = tawk_drv_device_link_down,
		.cb_ctx = tawk_data,
	};
	ipc_register_link_event_handler(tawk_data->ipc_stack,
					&tawk_data->link_cb);

	refcount_set(&tawk_data->refs, 1);

	return 0;

err_device_name:
	destroy_workqueue(tawk_data->misc_workqueue);
	return rc;
}

void tawk_drv_device_fini(struct tawk_drv_data *tawk_data)
{
	destroy_workqueue(tawk_data->misc_workqueue);
	tawk_drv_device_id_free(tawk_data->dev_id);
}

int tawk_drv_data_put(struct tawk_drv_data *tawk_data)
{
	if (!refcount_dec_and_test(&tawk_data->refs))
		return -1;

	mutex_destroy(&tawk_data->req_handlers_lock);
	mutex_destroy(&tawk_data->channel_lock);

	mutex_destroy(&tawk_data->channel_lock);
	ipc_lock(tawk_data->ipc_stack);
	ipc_device_fini(tawk_data->ipc_stack);
	ipc_free(tawk_data->ipc_stack);

	kfree(tawk_data);

	return 0;
}

#ifdef TAWK_DRV_DEBUG
static inline int list_entries(struct list_head *head, spinlock_t *lock)
{
	struct list_head *pos;
	int rc = 0;

	spin_lock(lock);

	list_for_each(pos, head)
		++rc;

	spin_unlock(lock);

	return rc;
}

int tawk_drv_client_req_free_count(struct tawk_drv_client *client)
{
	return list_entries(&client->req_free, &client->req_wait_q.lock);
}

int tawk_drv_client_req_in_use_count(struct tawk_drv_client *client)
{
	return list_entries(&client->req_in_use, &client->req_wait_q.lock);
}

int tawk_drv_client_read_q_count(struct tawk_drv_client *client)
{
	return list_entries(&client->read_q, &client->read_wait_q.lock);
}

bool tawk_drv_client_link_is_up(struct tawk_drv_client *client)
{
	bool res;

	spin_lock(&client->link_wait_q.lock);
	res = client->link_is_up;
	spin_unlock(&client->link_wait_q.lock);

	return res;
}

int tawk_drv_client_handle_req_count(struct tawk_drv_client *client)
{
	return atomic_read(&client->handle_req_total);
}

int tawk_drv_client_handle_resp_count(struct tawk_drv_client *client)
{
	return atomic_read(&client->handle_resp_total);
}

/* The counter can be 0 or 1 for any link down handling mode. It can be
 * greater than 1 for the "invincible" mode TAWK_DRV_LINK_DOWN_MODE_IGNORE.
 */
int tawk_drv_client_link_down_count(struct tawk_drv_client *client)
{
	int ret;

	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	ipc_lock(ipc);
	ret = client->link_down_count;
	ipc_unlock(ipc);

	return ret;
}
#endif
