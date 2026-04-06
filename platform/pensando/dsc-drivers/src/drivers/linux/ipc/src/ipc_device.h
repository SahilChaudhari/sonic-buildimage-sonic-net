// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_DEVICE_H_
#define _IPC_DEVICE_H_
#include "ipc_main.h"
#include "header/protocol.h"

#ifdef TAWK_DRV_DEBUG
#include <linux/atomic.h>
#endif

int tawk_drv_device_init(struct tawk_drv_data *tawk_data);
void tawk_drv_device_fini(struct tawk_drv_data *tawk_data);

enum tawk_drv_msg_state {
	TAWK_DRV_FREE, /* Currently unused */
	TAWK_DRV_REQ, /* In use by request */
	TAWK_DRV_RSP, /* In use by response */
	TAWK_DRV_TP_ERR, /* Transport error */
};

/**
 * struct tawk_drv_req_state
 * @buffer: Pointer to IPC buffer
 * @state: Current state
 * @user_tag: Tag from userspace (not IPC tag)
 * @list_elem: List head for adding to read queue
 * @req_table_elem: List head for entry into req_table
 * @parent_client: Client owner
 */
struct tawk_drv_req_state {
	ipc_buffer_t *buffer;
	enum tawk_drv_msg_state state;
	tawk_drv_hdr_tag_t user_tag;
	struct list_head list_elem;
	struct list_head req_table_elem;
	struct tawk_drv_client *parent_client;
};

/**
 * struct tawk_drv_client - representation of a FD's state
 * @rsp_table: Table tracking requests from remote
 * @req_free: Currently free request table entries
 * @req_in_use: Currently in-flight requests
 * @req_pool: Exclusive request buffer pool
 * @req_wait_q: Waitqueue for waiting for a req_table entry to become free
 * @rsp_table_lock: Serialize responders
 * @read_q: Queue of pending read-ready items
 * @read_wait_q: Waitqueue for read_q
 * @non_blocking: If file opened with O_NONBLOCK (i.e. non-blocking file
 *		  operation semantics)
 * @link_cb: Link event callback
 * @link_down_event: Link down event handle
 * @link_is_up: Our view on the link status, which is not always equal
 *              to ipc_link_is_up()
 * @link_wait_q: Waitqueue to wait for the link events
 * @link_down_mode: How to handle link down event for this client
 * @link_down_count: How many times link has transitioned to the down state
 * @tawk_data: Pointer to driver data
 */
#ifdef TAWK_DRV_DEBUG
struct dentry;
#endif

struct tawk_drv_client {
	struct tawk_drv_req_state *rsp_table;
	struct list_head req_free;
	struct list_head req_in_use;
	ipc_buffer_pool_t *req_pool;
	wait_queue_head_t req_wait_q;
	struct mutex rsp_table_lock;
	struct list_head read_q;
	wait_queue_head_t read_wait_q;
	bool non_blocking;
	ipc_link_event_cb_t link_cb;
	ipc_link_event_handle_t link_down_event;
	bool link_is_up;
	wait_queue_head_t link_wait_q;
	enum tawk_drv_link_down_mode link_down_mode;
	spinlock_t link_down_mode_lock;
	int link_down_count;
	struct tawk_drv_data *tawk_data;
#ifdef TAWK_DRV_DEBUG
	atomic_t handle_req_total;
	atomic_t handle_resp_total;
	struct dentry *debug_dentry;
#endif
};

bool tawk_drv_req_table_empty(struct tawk_drv_client *client);
int tawk_drv_req_table_resize(struct tawk_drv_client *client, size_t size);

#ifdef TAWK_DRV_DEBUG
int tawk_drv_client_req_free_count(struct tawk_drv_client *client);
int tawk_drv_client_req_in_use_count(struct tawk_drv_client *client);
int tawk_drv_client_read_q_count(struct tawk_drv_client *client);
bool tawk_drv_client_link_is_up(struct tawk_drv_client *client);
int tawk_drv_client_handle_req_count(struct tawk_drv_client *client);
int tawk_drv_client_handle_resp_count(struct tawk_drv_client *client);
int tawk_drv_client_link_down_count(struct tawk_drv_client *client);
#endif

int tawk_drv_data_put(struct tawk_drv_data *tawk_data);

#endif /* _IPC_DEVICE_H_ */
