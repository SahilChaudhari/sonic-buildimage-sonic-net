// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <linux/slab.h>
#include "ipc_device.h"
#include "ipc_channel.h"


static void tawk_drv_channel_try_increase_common_pool(struct tawk_drv_client *client,
						      size_t num_returned);

static int tawk_drv_channel_init(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	ipc_buffer_pool_t *req_pool;

	WARN_ON(!mutex_is_locked(&client->tawk_data->channel_lock));
	ipc_assert_locked(ipc);

	if (client->req_pool)
		return 0;

	req_pool = kzalloc(sizeof(ipc_buffer_pool_t), GFP_KERNEL);
	if (!req_pool)
		return -ENOMEM;

	ipc_request_pool_init(ipc, req_pool);

	client->req_pool = req_pool;
	return 0;
}

static void tawk_drv_channel_remove(struct tawk_drv_client *client)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;
	size_t current_pool_size;


	WARN_ON(!mutex_is_locked(&tawk_data->channel_lock));
	ipc_assert_locked(ipc);
	WARN_ON(!spin_is_locked(&client->req_wait_q.lock));

	if (!client->req_pool)
		return;

	/* TODO: Remove once PS-11202 done */
	WARN(!tawk_drv_req_table_empty(client),
	     "Not kernel driver bug; missing feature: %s:%d\n",
	     __FILE__, __LINE__);

	current_pool_size = ipc_request_pool_get_size(ipc, client->req_pool);
	ipc_request_pool_try_resize(ipc, client->req_pool, 0);
	tawk_drv_channel_try_increase_common_pool(client, current_pool_size);
	ipc_request_pool_fini(ipc, client->req_pool);

	kfree(client->req_pool);
	client->req_pool = NULL;

	tawk_drv_req_table_resize(client, tawk_data->req_limit);
}

void tawk_drv_channel_fini(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;

	mutex_lock(&client->tawk_data->channel_lock);
	ipc_lock(ipc);
	spin_lock(&client->req_wait_q.lock);

	tawk_drv_channel_remove(client);

	spin_unlock(&client->req_wait_q.lock);
	ipc_unlock(ipc);
	mutex_unlock(&client->tawk_data->channel_lock);
}

int tawk_drv_channel_get_pool_size(struct tawk_drv_client *client)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	size_t pool_size = 0;

	mutex_lock(&client->tawk_data->channel_lock);
	if (client->req_pool) {
		ipc_lock(ipc);
		pool_size = ipc_request_pool_get_size(ipc, client->req_pool);
		ipc_unlock(ipc);
	}
	mutex_unlock(&client->tawk_data->channel_lock);

	return pool_size;
}

int tawk_drv_channel_set_pool_size(struct tawk_drv_client *client,
				   uint32_t requested_size)
{
	struct tawk_drv_data *tawk_data = client->tawk_data;
	ipc_context_t *ipc = tawk_data->ipc_stack;
	size_t common_size;
	size_t curr_size;
	bool empty;
	int rc = 0;

	mutex_lock(&client->tawk_data->channel_lock);
	ipc_lock(ipc);

	spin_lock(&client->req_wait_q.lock);
	empty = tawk_drv_req_table_empty(client);
	spin_unlock(&client->req_wait_q.lock);

	if (!empty) {
		rc = -EAGAIN;
		goto err_unlock;
	}

	if (requested_size > tawk_data->req_limit) {
		rc = -EINVAL;
		goto err_unlock;
	}

	if (requested_size == 0) {
		spin_lock(&client->req_wait_q.lock);
		tawk_drv_channel_remove(client);
		spin_unlock(&client->req_wait_q.lock);
		goto out_unlock;
	}

	rc = tawk_drv_channel_init(client);
	if (rc)
		goto err_unlock;

	curr_size = ipc_request_pool_get_size(ipc, client->req_pool);
	if (curr_size == requested_size) {
		ipc_unlock(ipc);
		rc = curr_size;
		goto out_unlock;
	}

	rc = ipc_request_pool_try_resize(ipc, client->req_pool, requested_size);
	common_size = ipc_request_common_pool_get_size(ipc);

	/* If the new pool size (rc) is smaller than the requested size, attempt
	 * to resize the common pool. The common pool always needs to have at
	 * least one request buffer available. If requested_size is smaller than
	 * the original pool size then rc == requested_size - decreasing the size
	 * of an exclusive pool will always succeed.
	 */
	if (rc < requested_size) {
		size_t min_common_size = tawk_drv_minimum_common_pool_size();
		size_t reduce_size;

		if (requested_size - rc > common_size - min_common_size)
			reduce_size = common_size - min_common_size;
		else
			reduce_size = requested_size - rc;

		ipc_request_common_pool_try_resize(ipc, common_size - reduce_size);
		rc = ipc_request_pool_try_resize(ipc, client->req_pool,
						 requested_size);
	} else if (rc < curr_size) {
		/* Increase common pool size after shrinking exclusive pool */
		tawk_drv_channel_try_increase_common_pool(client, curr_size - rc);
	}

	spin_lock(&client->req_wait_q.lock);
	tawk_drv_req_table_resize(client, rc);
	spin_unlock(&client->req_wait_q.lock);

err_unlock:
out_unlock:
	ipc_unlock(ipc);
	mutex_unlock(&client->tawk_data->channel_lock);
	return rc;
}

static void tawk_drv_channel_try_increase_common_pool(struct tawk_drv_client *client,
						      size_t num_returned)
{
	ipc_context_t *ipc = client->tawk_data->ipc_stack;
	size_t curr_size;

	ipc_assert_locked(ipc);

	curr_size = ipc_request_common_pool_get_size(ipc);

	ipc_request_common_pool_try_resize(ipc, curr_size + num_returned);
}
