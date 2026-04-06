// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/slab.h>
#include <tawk/ipc.h>
#include "ipc_device.h"
#include "ipc_req_handle.h"

/* Internal representation of request callback used as table entry to
 * tawk_drv_req_handlers
 */
struct tawk_drv_req_handler {
	struct list_head list;
	ipc_request_cb_t req_cb;
	struct tawk_drv_client *parent_client;
};

static struct tawk_drv_req_handler *tawk_drv_req_handler_exists(struct tawk_drv_data *tawk_data,
								uint32_t requested_ep)
{
	struct tawk_drv_req_handler *handler;

	/* TODO: Consider removing WARN_ON */
	WARN_ON(!mutex_is_locked(&tawk_data->req_handlers_lock));

	list_for_each_entry(handler, &tawk_data->req_handlers, list) {
		if (handler->req_cb.endpoint == requested_ep)
			return handler;
	}
	return NULL;
}

static void tawk_drv_req_handler_free(struct tawk_drv_client *client,
				      struct tawk_drv_req_handler *handler)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;

	/* TODO: Consider removing WARN_ON */
	WARN_ON(!mutex_is_locked(&tawk_data->req_handlers_lock));

	ipc_lock(ipc);
	ipc_deregister_request_handler(ipc, &handler->req_cb);
	ipc_unlock(ipc);

	list_del(&handler->list);
	kfree(handler);
}

static void tawk_drv_req_handler_init(struct tawk_drv_client *client,
				      struct tawk_drv_req_handler *handler)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;

	/* TODO: Consider removing WARN_ON */
	WARN_ON(!mutex_is_locked(&tawk_data->req_handlers_lock));

	ipc_lock(ipc);
	ipc_register_request_handler(ipc, &handler->req_cb);
	ipc_unlock(ipc);

	INIT_LIST_HEAD(&handler->list);
	list_add_tail(&handler->list, &tawk_data->req_handlers);
}

/* Free all request handlers registered by client */
void tawk_drv_req_handler_free_client_handlers(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	struct tawk_drv_req_handler *curr, *nxt;

	mutex_lock(&tawk_data->req_handlers_lock);
	list_for_each_entry_safe(curr, nxt, &tawk_data->req_handlers, list) {
		if (curr->parent_client == client)
			tawk_drv_req_handler_free(client, curr);
	}
	mutex_unlock(&tawk_data->req_handlers_lock);
}

int tawk_drv_req_handler_register(struct tawk_drv_client *client,
				  ipc_request_cb *cb,
				  uint32_t ep)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	struct tawk_drv_req_handler *new_handler;
	int rc;

	mutex_lock(&tawk_data->req_handlers_lock);
	new_handler = tawk_drv_req_handler_exists(tawk_data, ep);
	if (new_handler) {
		rc = -EBUSY;
		goto out;
	}

	new_handler = kzalloc(sizeof(struct tawk_drv_req_handler), GFP_KERNEL);
	if (!new_handler) {
		rc = -ENOMEM;
		goto out;
	}

	new_handler->req_cb.endpoint = ep;
	new_handler->req_cb.cb_ctx = client;
	new_handler->req_cb.handler = cb;
	new_handler->parent_client = client;

	tawk_drv_req_handler_init(client, new_handler);
	rc = 0;

out:
	mutex_unlock(&tawk_data->req_handlers_lock);
	return rc;
}

int tawk_drv_req_handler_deregister(struct tawk_drv_client *client,
				    uint32_t ep)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	struct tawk_drv_req_handler *handler;
	int rc = 0;

	mutex_lock(&tawk_data->req_handlers_lock);
	handler = tawk_drv_req_handler_exists(tawk_data, ep);
	if (!handler) {
		rc = -ENOENT;
		goto out;
	}

	if (handler->parent_client != client) {
		rc = -EINVAL;
		goto out;
	}

	tawk_drv_req_handler_free(client, handler);
out:
	mutex_unlock(&tawk_data->req_handlers_lock);
	return rc;
}
