// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#ifdef __KERNEL__
#include <libc_compat.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

#include <zephyr/kernel.h>


/*!
 * \file
 * \brief TAWK public interface
 *
 * \TODO: interface for exposing maximum number of outstanding requests
 * \TODO: negotiate maximum number of requests during link up

 * \mainpage
 * \section Synopsis
 * \subsection req_handlers Request Handlers
 * \code{.c}
 *
 * #include <tawk/ipc.h>
 *
 * static void request_handler_fn(ipc_context_t *ipc, ipc_buffer_t *buffer, void *cb_ctx)
 * {
 *   uint32_r ep = ipc_buffer_get_req_endpoint(buffer);
 *   void *req_buf = ipc_buffer_pld_ptr(buffer);
 *   size_t req_len = ipc_buffer_get_pld_len(buffer);
 *
 *   ...
 *
 *   ipc_request_make_response(ipc, buffer);
 *
 *   void *rsp_buf = ipc_buffer_pld_ptr(buffer);
 *   ...
 *   size_t rsp_len = 88;
 *   ipc_buffer_set_pld_len(buffer, rsp_len);
 *
 *   ipc_response_send(ipc, buffer);
 * }
 *
 * extern void ipc_context_t *global_ipc_context;
 *
 * static void service_init(void)
 * {
 *   static ipc_request_cb_t request_handler = {
 *     .handler = request_handler_fn,
 *     .endpoint = 42,
 *     .cb_ctx = NULL,
 *   };
 *
 *   ipc_lock(global_ipc_context);
 *   ipc_register_request_handler(global_ipc_context, &request_handler);
 *   ipc_unlock(global_ipc_context);
 * }
 * \endcode
 * \subsection send_req Sending Requests
 * \code{.c}
 * #include <tawk/ipc.h>
 *
 * static void response_handler(ipc_context_t *ipc, int tp_err, ipc_buffer_t *buffer, void *cb_ctx)
 * {
 *   if (tp_err != 0) {
 *     // link down, endpoint not recognsed, ...
 *   } else {
 *     void *rsp_buf = ipc_buffer_pld_ptr(buffer);
 *     size_t rsp_len = ipc_buffer_get_pld_len(buffer);
 *
 *     ...
 *   }
 *     ipc_buffer_response_free(ipc, buffer);
 *   }
 * }
 *
 * static void got_buffer(ipc_context_t *ipc, int tp_err, ipc_buffer_t *buffer, void *cb_ctx)
 * {
 *   ipc_assert_locked(ipc);
 *
 *   if (tp_err != 0) {
 *     // link down ...
 *   } else {
 *     ipc_buffer_set_req_endpoint(buffer, 42);
 *     void *req_buf = ipc_buffer_pld_ptr(buffer);
 *     ...
 *     size_t req_len = 10;
 *     ipc_buffer_set_pld_len(buffer, req_len);
 *
 *     ipc_request_send_async(ipc, buffer, response_handler, NULL);
 *   }
 * }
 *
 * static void client(void)
 * {
 *   static ipc_buffer_alloc_cb_t alloc_cb = {
 *     .cb = got_buffer,
 *     .cb_ctx = NULL,
 *   };
 *   ipc_request_alloc_async(ipc, &alloc_cb);
 *}
 * \endcode
 *
 *
 * \section buffers Buffer
 *
 * Requests are responses and exposed via this API as buffers.
 * Each buffer has a type (request, response and transport error
 * response), some type-specific metadata and a payload.
 *
 * Request buffers have an endpoint metadata field.  This is used
 * to mux requests.
 *
 * Response buffers have an errno metadata field.  This is available
 * for users to this API to use as desired.
 *
 * Transport error responses have no user-visible metadata.
 *
 *
 * \subsection lifecycle Lifecycle
 *
 * The user visible lifecycle of a buffer is as follows:
 *
 *  - The local peer:
 *    - Allocates a request buffer
 *    - Populates the response buffer with request metadata and payload
 *    - Sends the request buffer
 *  - The remote peer:
 *    - Receives the request buffer
 *    - Extracts and processes the request
 *    - Converts the request buffer into a response buffer
 *    - Populates the response buffer with response metadata (errno) and payload
 *    - Sends the response buffer
 *  - The local peer
 *    - Receives the response buffer
 *    - Extracts and processes the response
 *    - Either frees the response buffer or converts it back into a request buffer to send another request
 *
 * Additionally, it is possible for the local peer to free a request
 * buffer without sending it.
 *
 * If there is no registered handler for the request endpoint,
 * local peer will see its request buffer returned as a transport error
 * response.
 *
 * If the link goes down between buffer allocation and receipt of the
 * response the the local peer will see its request buffer returned as
 * as transport error response.
 *
 *
 * \subsection buffer_pools Pools
 *
 * There is a negotiated upper bound on the number of requests that
 * can be in flight.  Attempting to allocate a request buffer beyond
 * this this limit will fail (try alloc), or wait/block until an
 * outstanding request is completed at its buffer freed (async/sync
 * alloc).  Thus, in order to allow mutually non-blocking streams of
 * requests, it is necessary to limit the number of outstanding
 * requests within each stream such that the total number is never
 * exceeded.
 *
 * To assist with this, the TAWK IPC library provided the concept of
 * request pools.  These are pools of request buffers from which
 * request buffers can be allocated.  There is always a common pool,
 * which is allocate from by default.  However, user code can create
 * additional pools and distribute buffers between them.
 *
 * On link up, all available buffers are assigned to the common pool.
 * In order to assign buffers to another pool, user code must first
 * reduce the size of the common pool, thus making buffers available
 * for other pools to use.  In order to free a pool, that pool must
 * have had any assigned buffers removed by resizing it to size 0.
 *
 *
 * \section locking Locking and Blocking
 * A single mutex lock covers each instance of the IPC stack.  This lock
 * must be locked whenever traversing the API boundary, i.e. on call and
 * return of both API functions and callbacks.
 *
 * Code must never block whilst holding the lock.  In general, API
 * functions never drop the lock (and by implication, never block).
 * Exceptions to this are clearly labelled.
 *
 * Similarly, in general, callback functions must never drop the
 * lock (and by implication, never block).  Exceptions where this
 * is permitted are clearly labelled.
 *
 * Some conditions (e.g. link state) are guaranteed constant as long
 * as the lock is lock.  (This is only true for users of this API,
 * i.e. code make function calls in.  It's not clearly not true for
 * the implementation)
 *
 * It is acceptable for user code to use the IPC lock to protect its
 * own state, provided that,
 *   a) that state is directly related to interactions with the IPC
 *      stack, and
 *   b) the amount of additional time that the* lock is held within
 *      user code is negligible compared to that within the IPC
 *      stack.
 *
 * As a rule of thumb, if user code acquires and releases the lock
 * without interacting with the IPC stack then its doing something wrong.
 *
 *
 * \section link Link Status
 *
 * IPC communication is performed over an abtract "link" that represents
 * whether the peer is ready for communication.  The IPC transport provides
 * no reliability and a link down even can cause loss of requests/responses.
 *
 * As such, the link state is exposed to users of this API so that they can
 * implement reliablity or otherwise handle loss of communication as they
 * wish.
 *
 * The link state as exposed to users of the API has four possible states:
 *  - `DOWN_ACK`
 *  - `UP_REQ`
 *  - `UP_ACK`
 *  - `DOWN_REQ`
 *
 * These states are never explictly exposed, and are only implicity in the
 * API.  The link state remains constant whilst the lock is held.
 *
 * Transitions an ACK states to the subsequent REQ states occur spontaneously.
 * Transitions from REQ states to the subsequent ACK state occur when all users
 * of the API has acknowledges the transition.
 *
 * For transitions from `UP_REQ` to `UP_ACK`, this acknowledgement occurs when
 * all registered link up handles have provided an acknowledgement of the event
 *
 * For transitions from `DOWN_REQ` to `DOWN_ACK`, the acknowedgement occurs when all
 * all the following are complete:
 *  - All link down handlers have provided an acknowldgement of the event
 *  - All buffers passed to a request handler have been returned with send_response
 *  - All request buffers allocated have been returned with send_request
 *  - All waiting async allocation callbacks have returned.
 *  - All request buffer pools have been freed
 *
 * It is only valid to attempt to allocate buffers when not in a DOWN_ACK link
 * state.  Users of this API should ensure this by either a) synchronising
 * processing allocation buffers with the link state using link event hooks, or
 * b) ensuring \ref ipc_link_is_up returns `true`.
 *
 *
 * \section sec Security Model
 *
 * It is assumed that all users of this API form a single trusted entity:
 *  - It is assumed that the API contract will not be violated.
 *
 *  - All users of the API are assumed to be permitted to see both
 *    a) any data written into a buffer by a user of this API, or b) any
 *    data sent by the remote peer.
 *
 * It is assume that the remote peer of a given IPC intstance is a single
 * untrusted entity:
 *  - The remote peer is assumed to be permitted to see any data written
 *    into a buffer by a user of this API
 *
 * As such, no scrubbing of buffers is performed.  The following buffer
 * regions must be assumed to contain arbitrary data from previous requests
 * of responses.
 *  - Buffers ready for populating a request: bytes in `[0:buf_size)`
 *  - Buffers containing a received request: bytes in `[pld_len:buf_size)`
 *  - Buffers reading for populating a response: bytes in `[0:buf_size)`
 *  - Buffers containing a received response: bytes in `[pld_len:buf_size)`
 *
 * If either a) a user of this API on an IPC instance, or b) the remote
 * peer attached to that same IPC instance is bridging to another entity
 * at a different trust level then the following rules must be obeyed:
 *
 *  - When allowing an external entity to populate a request or response buffer,
 *    no prexisting data within that buffer should be exposed to the external
 *    entity.
 *
 *  - When exposing the data of a received request or response buffer
 *    to an external entity, only bytes the range [0:pld_len) should be
 *    exposed.
 *
 *  - When populating a buffer, whether with data from an external entity or
 *    otherwise, all bytes in the range [0:pld_len) should be explicity set.
 */

typedef struct ipc_context_s ipc_context_t;


/*! \brief Acquire the IPC lock.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must not be held
 * \post The lock will be held
 * \remark May block
 */
extern void ipc_lock(ipc_context_t *ipc);

/*! \brief Release the IPC lock.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will not be held
 * \remark Will not block
 */
extern void ipc_unlock(ipc_context_t *ipc);

/*! \brief Invoke UB if the caller does not hold the IPC lock.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
*/
extern void ipc_assert_locked(ipc_context_t *ipc);


/*! \brief Wait on a condition variable using the IPC lock.
 *
 * \param ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark May drop the lock and block
*/
extern void ipc_condvar_wait(ipc_context_t *ipc, struct k_condvar *cv);


/*! \brief Opaque type used to represent a buffer */
typedef struct ipc_buffer_s ipc_buffer_t;

/*! \brief Get a doubly-linked-list node pointer for a buffer
 *
 * It is useful for user code to be able to maintain lists of buffers,
 * e.g. as a work queue.  This function returns a pointer to a
 * doubly-linked-link node associated with the buffer.  The referenced
 * `sys_dnode_t` can be used by user code however it wishes.
 *
 * For all newly allocated initiator request buffers and for all
 * target request buffers passed to a handler callback, the
 * `sys_dnode_t` will have been initialised with `sys_dnode_init`.
 *
 * For all initiator buffers, the returned pointer (and the association
 * between the `sys_dnode_t` and the buffer) remains until the buffer
 * is freed, either as a request buffer via \ref ipc_request_free or
 * as a response buffer via \ref ipc_response_free.
 *
 * For all target buffers, the returned pointer (and the association
 * between the `sys_dnode_t` and the buffer) remains valid until the
 * buffer is send to the remote peer via \ref ipc_response_send.
 *
 * In both cases, user code must ensure that the `sys_dnode_t` is not
 * linked (i.e. sys_dnode_is_linked returns false) when this occurs.
 *
 * \param[in]  buffer An IPC buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern sys_dnode_t *ipc_buffer_dnode(ipc_buffer_t *buffer);

/*! \brief Given a double-linked-list node, get the associated buffer
 *
 * This function is the inverse of  \ref ipc_buffer_dnode.
 *
 * \param[in]  node A Zephyr dnode
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern ipc_buffer_t *ipc_buffer_from_dnode(sys_dnode_t *node);

/*! \brief Get a pointer to the payload for a buffer
 *
 * It is only valid to use this pointer to access
 * payload data bytes up to the current payload
 * length.
 *
 * The retutned pointer remains valid only whilst
 * the buffer remains owned by user and the local
 * peer, retains the same length and retains the
 * same type.  It is invalidated by:
 *
 * \ref ipc_buffer_set_pld_len, \ref ipc_request_send_async
 * \ref ipc_response_send, \ref ipc_request_free,
 * \ref ipc_response_free, \ref ipc_request_make_response,
 * \ref ipc_response_reuse.
 *
 * Only valid for request and response buffers
 *
 * \param[in]  buffer An IPC buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern void *ipc_buffer_pld_ptr(ipc_buffer_t *buffer);

/*! \brief Get the maximum size of a buffer's payload
 *
 * Returns the maximum size that this buffers
 * payload can be.
 *
 * The retutned size remains valid only whilst
 * the buffer remains owned by user and the local
 * peer and retains the same type.  It is invalidated
 * by:
 *
 * \ref ipc_request_send_async
 * \ref ipc_response_send, \ref ipc_request_free,
 * \ref ipc_response_free, \ref ipc_request_make_response,
 * \ref ipc_response_reuse.
 *
 * Only valid for request and response buffers
 *
 * \param[in]  buffer An IPC buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern size_t ipc_buffer_pld_size(ipc_buffer_t *buffer);

/*! \brief Get the current size of a buffer's payload
 *
 * The retutned size remains valid only whilst
 * the buffer remains owned by user and the local
 * peer and retains the same type.  It is invalidated
 * by:
 *
 * \ref ipc_buffer_set_pld_len, \ref ipc_request_send_async
 * \ref ipc_response_send, \ref ipc_request_free,
 * \ref ipc_response_free, \ref ipc_request_make_response,
 * \ref ipc_response_reuse.
 *
 * Only valid for request and response buffers
 *
 * \param[in]  buffer An IPC buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern size_t ipc_buffer_get_pld_len(ipc_buffer_t *buffer);

/*! \brief Set the current size of a buffer's payload
 *
 * If the payload length is changed from `old_pld_len` to `new_pld_len` then:
 *
 *  - Any payload data bytes in the range `[0, MIN(old_pld_len, new_pld_len)`
 *    remain valid.
 *  - If `new_pld_len > old_pld_len` then any data bytes in the range `[old_pld_len, new_pld_len)`
 *    must be treated as uninitialised.
 *
 * Only valid for request and response buffers
 *
 * \param[in]  buffer An IPC buffer
 * \param[in]  len The desired payload length
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern void ipc_buffer_set_pld_len(ipc_buffer_t *buffer, size_t len);

/*! \brief Get the request endpoint metadata value for a request buffer
 *
 * Only valid for request buffers
 *
 * \param[in]  buffer An IPC request buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern uint32_t ipc_buffer_get_req_endpoint(ipc_buffer_t *buffer);

/*! \brief Set the request endpoint metadata value for a request buffer
 *
 * Only valid for request buffers
 *
 * \param[in]  buffer An IPC request buffer
 * \param[in]  ep The desired request endpoint metadata value
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern void ipc_buffer_set_req_endpoint(ipc_buffer_t *buffer, uint32_t ep);

/*! \brief Get the response errno metadata value for a response buffer
 *
 * Only valid for response buffers
 *
 * \param[in]  buffer An IPC response buffer
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern uint32_t ipc_buffer_get_rsp_errno(ipc_buffer_t *buffer);

/*! \brief Set the response errno metadata value for a response buffer
 *
 * Only valid for response buffers
 *
 * \param[in]  buffer An IPC response buffer
 * \param[in]  rsp_errno The resonse errno metadata value
  *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern void ipc_buffer_set_rsp_errno(ipc_buffer_t *buffer, uint32_t rsp_errno);

/*! \brief Determine whether a buffer contains a transport error response
 *
 * Only valid for reponse and transport error reponse buffers
 *
 * \param buffer An IPC response buffer or transport error response buffer
 *
 * \return 0 if the buffer is a response buffer
 * \return non-zero if the buffer is transport error response.
 *
 * \remark It is not required that the lock be held.  Not thread
 * safe; if multiple threads are sharing a buffer then
 * they must arrange synchronisation themselves.
 * \remark Will not block
 */
extern int ipc_buffer_rsp_tp_err(ipc_buffer_t *buffer);

/*! \brief Get the maximum number of outstanding initiator requests
 *
 * Returns the maximum number of requests initiated by the local peer
 * that can be inflight simultaneously.  This is the maximum size of
 * all requests pools, including the common pool.  Equivalently, if
 * solely using the common pool at its default size, this is also the
 * maximum value by which the number of calls to \ref
 * ipc_request_alloc_async can exceed the total number of calls to
 * \ref ipc_request_free and \ref ipc_response_free before the delay
 * to the callback becomes unbounded.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock will be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock must be held
 * \remark Will not block
 */
size_t ipc_max_ini_requests(ipc_context_t *ipc);

/*! \brief Get the maximum number of outstanding target requests
 *
 * Returns the maximum number of requests initiated by the remote peer that
 * can be inflight simultaneously.  Equivalently, this is the maximum value
 * by which the number of calls to a registers request handler can exceed the
 * total number of calls \ref ipc_response_send.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock will be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock must be held
 * \remark Will not block
 */
size_t ipc_max_tgt_requests(ipc_context_t *ipc);

/*! \brief A structure used to track state associated pool of request buffers */
typedef struct ipc_buffer_pool_s {
#ifndef __DOXYGEN__
  sys_dnode_t q;
  sys_dlist_t free;
  sys_dlist_t wait;
  size_t size;
#else
  int no_public_members;
#endif
} ipc_buffer_pool_t;


/*! \brief Initialise a pool of request buffers.
 *
 * The initialised pool will have size of 0.
 *
 * The pointer, `pool`, must remain valid until the pool is finalised.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_pool_init(ipc_context_t *ipc, ipc_buffer_pool_t *pool);

/*! \brief Finalise a pool of request buffers.
 *
 * The pool must have size 0 before being finalised.  All pools other
 * than the common pool must be finalised before the link can program
 * from a DOWN_REQ to a DOWN_ACK state.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_pool_fini(ipc_context_t *ipc, ipc_buffer_pool_t *pool);

/*! \brief Query the size of a pool of request buffers.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern size_t ipc_request_pool_get_size(ipc_context_t *ipc, ipc_buffer_pool_t *pool);

/*! \brief Attempt to change the size of a pool of request buffers.
 *
 * If `new_size` is greater than the current size then the pool will be enlarged
 * so long as request buffers are available.  The total number of buffers across
 * all pools (including the common pool) cannot exceed the value returned by
 * \ref ipc_max_ini_requests.
 *
 * If `new_size` is less than the current size then the pool will be shrunk, subject
 * to the limitation that the pool cannot be shrunk beyond the number of oustanding
 * requests allocated from the pool.
 *
 * In both cases, returns the resulting size of the pool.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 * \param[in]  new_size The desired pool size
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern size_t ipc_request_pool_try_resize(ipc_context_t *ipc, ipc_buffer_pool_t *pool, size_t new_size);

/*! \brief Query the size of the common pool of request buffers.
 *
 * \sa ipc_request_pool_get_size
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern size_t ipc_request_common_pool_get_size(ipc_context_t *ipc);

/*! \brief Attempt to change the size of the common pool of request buffers.
 *
 * \sa ipc_request_pool_try_resize
 *
 * \param[in]  ipc The IPC context
 * \param[in]  new_size The desired common pool size
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern size_t ipc_request_common_pool_try_resize(ipc_context_t *ipc, size_t new_size);

/*! \brief A signature for a generic callback associated with a buffer
 *
 * The precise semantics of the arguments are documented in \ref ipc_request_alloc_async
 * and \ref ipc_request_send_async.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  tp_err Whether a transport error occured
 * \param[in]  buffer The buffer to which the callback relates
 * \param[in]  cb_ctx An opaque context pointer
 *
 * \pre The lock will be held
 * \pre The link will be in an UP_ACK or DOWN_REQ state
 * \post The lock must be held
 * \remark Must not block
 */
typedef void (ipc_buffer_cb)(ipc_context_t *ipc, int tp_err, ipc_buffer_t *buffer, void *cb_ctx);

/*! \brief Try to allocate a request buffer from a pool without blocking
 *
 * If the link is in the UP_ACK state and a request buffer is available
 * then it is allocated and returned.  Otherwise, `NULL` is returned.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern ipc_buffer_t *ipc_request_pool_try_alloc(ipc_context_t *ipc, ipc_buffer_pool_t *pool);

/*! \brief Try to allocate a request buffer from the common pool without blocking
 *
 * \sa ipc_request_pool_try_alloc
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern ipc_buffer_t *ipc_request_try_alloc(ipc_context_t *ipc);

/*! \brief A structure used to track state associated with an async buffer allocation */
typedef struct ipc_buffer_alloc_cb_s {
#ifndef __DOXYGEN__
  sys_dnode_t q;
#endif
  ipc_buffer_cb *cb;
  void *cb_ctx;
} ipc_buffer_alloc_cb_t;

/*! \brief Allocate a request buffer from a pool
 *
 * Allocates a request buffer and calls the callback.  The buffer passed to the
 * callback will have zero length payload.
 *
 * The callback may be called before this function has returned.
 *
 * If the link is in a DOWN_REQ when this function is called or transitions to
 * a DOWN_REQ state whilst waiting for a buffer then the callback will be called
 * with `tp_err == ENOLINK` and `buffer == NULL`.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 * \param[in]  cb A structure containing the callback for when the buffer is
 *                allocated and an opaque context pointer.  A unique instance of
 *                \ref ipc_buffer_alloc_cb_t must be provided for event async
 *                allocation.  The pointer must remain valid until after the
 *                callback has returned.
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_pool_alloc_async(ipc_context_t *ipc, ipc_buffer_pool_t *pool, ipc_buffer_alloc_cb_t *cb);

/*! \brief Allocate a request buffer from the common pool
 *
 * \sa ipc_request_pool_alloc_async
 *
 * \param[in]  ipc The IPC context
 * \param[in]  cb A structure containing the callback for when the buffer is
 *                allocated and an opaque context pointer.  A unique instance of
 *                \ref ipc_buffer_alloc_cb_t must be provided for event async
 *                allocation.  The pointer must remain valid until after the
 *                callback has returned.
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_alloc_async(ipc_context_t *ipc, ipc_buffer_alloc_cb_t *cb);

/*! \brief Allocate a request buffer
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The request buffer to free
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_free(ipc_context_t *ipc, ipc_buffer_t *buffer);

/*! \brief Convert a request buffer into a response buffer
 *
 * The resultoing response buffer will have zero length payload
 * and zero value for rsp_errno metadata.  Only valid to call on request
 * buffers obtained as an argument to a request handler callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The request buffer to make into a response buffer
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_make_response(ipc_context_t *ipc, ipc_buffer_t *buffer);

/*! \brief Free a response buffer
 *
 * Only valid to call on response buffers obtained as an argument to a response handler callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The response buffer to free
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_response_free(ipc_context_t *ipc, ipc_buffer_t *buffer);

/*! \brief Reuse a response buffer as a request buffer
 *
 * Only valid to call on response buffers obtained as an argument to a response handler callback.
 *
 * If the link is in a DOWN_REQ state then the response buffer will be converted into a transport
 * error resposne buffer and ENOLINK will be returned.  This only valid action is to free the buffer.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The response buffer to reuse as a request buffer
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern int ipc_response_reuse(ipc_context_t *ipc, ipc_buffer_t *buffer);

/*! \brief Send a request buffer to the remote peer
 *
 * Only valid to call on request buffers allocated on the local peer
 *
 * When the response callback is called, `tp_err == 0` indicates that no
 * transport error occurred and that the buffer is a response buffer.
 *
 * If the remote peer indicates the now request handler is register for the request
 * buffer endpoint then the callback will be called witin `tp_err == ENOSYS`
 *
 * If the link enters a `DOWN_REQ` state before the response buffer is received from
 * the remote peer then the response callback will be called with `tp_err == ENOLINK`.
 *
 * In both cases, and in all cases where `tp_err != 0`, the buffer is a transport
 * error reponse and can only be freed.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The request buffer to send to the remote peer
 * \param[in]  rsp_cb A callback to be called when the response is received
 * \param[in]  cb_ctx An opaque value to be passed into the response callback
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_send_async(ipc_context_t *ipc, ipc_buffer_t *buffer, ipc_buffer_cb *rsp_cb, void *cb_ctx);

/*! \brief Send a response buffer to the remote peer
 *
 * Only valid to call on response buffers obtained as an argument to a response handler callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The response buffer to send
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_response_send(ipc_context_t *ipc, ipc_buffer_t *buffer);

/*! \brief Request a reset of the IPC link
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_request_reset(ipc_context_t *ipc);

/*! \brief Request a reset of the IPC link only if the link is up
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_maybe_request_reset(ipc_context_t *ipc);

/*! \brief Check whether the IPC link is up
 *
 * Will return true if the link is in the UP_REQ or UP_ACK states.
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern bool ipc_link_is_up(ipc_context_t *ipc);

/*! \brief An opaque type used to acknowledge link events back the the IPC stack */
typedef struct ipc_link_event_handle_s *ipc_link_event_handle_t;

/*! \brief A signature for a callback on link event change
 *
 * All link event callback must acknowledged when they've performed the necessary
 * processing, e.g. free resources on link down, preparing for requests on link up.
 * The acknowledge the event, \ref ipc_link_up_event_ack or \ref ipc_link_down_event_ack
 * should be called with the `handle` value supplied to the callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  handle An opaque value used to associated event callbacks with acknowledgements
 * \param[in]  cb_ctx An opaque context pointer
 *
 * \pre The lock will be held
 * \post The lock must be held
 * \remark Must not block
 */
typedef void (ipc_link_event_cb)(ipc_context_t *ipc, ipc_link_event_handle_t handle, void *cb_ctx);

/*! \brief A structure used to track state associated with link handler registration */
typedef struct ipc_link_event_cb_s {
#ifndef __DOXYGEN__
  sys_dnode_t q;
#endif
  ipc_link_event_cb *link_up_cb;
  ipc_link_event_cb *link_down_cb;
  void *cb_ctx;
} ipc_link_event_cb_t;

/*! \brief Register a callback for link events
 *
 * \param[in]  ipc The IPC context
 * \param[in]  cb A pointer to a structure containing the callbacks for when the transitions and an opaque context
 *                pointer.  A unique instance of \ref ipc_link_event_cb_t must be provided for event
 *                register link event handler.  The pointer must remain valid until after the handlers
 *                have been deregisters (i.e. \ref ipc_deregister_link_event_handler has returned)
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_register_link_event_handler(ipc_context_t *ipc, ipc_link_event_cb_t *cb);

/*! \brief Deregister a callback for link events
 *
 * \param[in]  ipc The IPC context
 * \param[in]  cb A pointer previous passed to \ref ipc_register_link_event_handler
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_deregister_link_event_handler(ipc_context_t *ipc, ipc_link_event_cb_t *cb);

/*! \brief Acknowledge a link up event
 *
 * \param[in]  ipc The IPC context
 * \param[in]  handle A handle as passed to a register link up event handler
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 *
 */
extern void ipc_link_up_event_ack(ipc_context_t *ipc, ipc_link_event_handle_t handle);

/*! \brief Acknowledge a link down event
 *
 * \param[in]  ipc The IPC context
 * \param[in]  handle A handle as passed to a register link up event handler
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 *
 */
extern void ipc_link_down_event_ack(ipc_context_t *ipc, ipc_link_event_handle_t handle);


/*! \brief A signature for a callback on request recept
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer A request buffer
 * \param[in]  cb_ctx An opaque context pointer
 *
 * \pre The lock will be held
 * \post The lock must be held
 * \remark Must not block
 */
typedef void (ipc_request_cb)(ipc_context_t *ipc, ipc_buffer_t *buffer, void *cb_ctx);

/*! \brief A structure used to track state associated with request endpoint handler registration */
typedef struct ipc_request_cb_s {
#ifndef __DOXYGEN__
  sys_dnode_t q;
#endif
  ipc_request_cb *handler;
  uint32_t endpoint;
  void *cb_ctx;
} ipc_request_cb_t;

/*! \brief Register a callback for requests to a given endpoint
 *
 * \param[in]  ipc The IPC context
 * \param[in]  cb A pointer to a structure containing a request endpoint, a callback to call when
 *                requests are received on that endpoint, and an opaque context pointer.
 *                A unique instance of \ref ipc_request_cb_t must be provided for each registered
 *                request handler.  The pointer must remain valid until after the handler
 *                has been deregisters (i.e. \ref ipc_deregister_request_handler has returned)
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_register_request_handler(ipc_context_t *ipc, ipc_request_cb_t *cb);

/*! \brief Deregister a callback for requests to a given endpoint
 *
 * \param[in]  ipc The IPC context
 * \param[in]  cb A pointer previous passed to \ref ipc_deregister_request_handler
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark Will not block
 */
extern void ipc_deregister_request_handler(ipc_context_t *ipc, ipc_request_cb_t *cb);


/*! \brief Block until the link is in an UP_REQ or UP_ACK state
 *
 * \param[in]  ipc The IPC context
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \post The link will be in an UP_REQ or UP_ACK
 * \remark May drop the lock and block
 */
extern void ipc_block_on_link_up(ipc_context_t *ipc);

/*! \brief Allocate a request buffer from a pool.  Block if necessary
 *
 * Return 0 on success, non-zero on failure.  The return value is
 * as would have been passed as the `tp_err` argument to the async
 * callback.
 *
 * On success, *buffer points to an allocated request buffer.  On failure,
 * no buffer is allocated.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  pool A request buffer pool
 * \param[out] buffer The allocated buffer (if successful)
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark May drop the lock and block
 */
extern int ipc_request_pool_alloc(ipc_context_t *ipc, ipc_buffer_pool_t *pool, ipc_buffer_t **buffer);

/*! \brief Allocate a request buffer from the common pool.  Block if necessary
 *
 * \sa ipc_request_pool_alloc
 *
 * \param[in]  ipc The IPC context
 * \param[out] buffer The allocated buffer (if successful)
 *
 * \pre The lock must be held
 * \pre The link must be in an UP_REQ, UP_ACK or DOWN_REQ state
 * \post The lock will be held
 * \remark May drop the lock and block
 */
extern int ipc_request_alloc(ipc_context_t *ipc, ipc_buffer_t **buffer);


/*! \brief Send a request.  Block until a response is received.
 *
 * Returns 0 if a normal respose is received and non-zero
 * if a transport error response is recieved.  The return value is
 * as would have been passed as the `tp_err` argument to the async
 * callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  buffer The request buffer to send to the remote peer
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark May drop the lock and block
 */
extern int ipc_request_exec(ipc_context_t *ipc, ipc_buffer_t *buffer);

/* \brief A deprecated alias for \ref ipc_request_exec
 */
static inline int ipc_request_send(ipc_context_t *ipc, ipc_buffer_t *buffer)
{
  return ipc_request_exec(ipc, buffer);
}


/*! \brief Send a request.  Block until a response is received.  Don't
 *  require the caller to allocate buffers.
 *
 * Returns 0 if a normal respose is received and non-zero
 * if a transport error response is recieved.  The return value is
 * as would have been passed as the `tp_err` argument to the async
 * callback.
 *
 * \param[in]  ipc The IPC context
 * \param[in]  req_ep The desired request endpoint metadata value
 * \param[in]  req_pld A buffer containing the data to copy into the request payload
 * \param[in]  req_pld_len The length request payload
 * \param[out] rsp_errno The error metadata value from the response; only valid
 *                       if a normal response is received.
 * \param[out] rsp_pld A buffer to copy the response payload data into
 * \param[in,out] rsp_pld_len On entry, the size of the buffer pointed to be
 *                 `rsp_pld`.  On exit, the amount of data copied into the
 *                response payload buffer.
 *
 * \pre The lock must be held
 * \post The lock will be held
 * \remark May drop the lock and block
*/
extern int ipc_request(ipc_context_t *ipc,
                       uint32_t req_ep, void *req_pld, size_t req_pld_len,
                       uint32_t *rsp_errno, void *rsp_pld, size_t *rsp_pld_len);

/**
 * \brief Dump the current state of the IPC library for debugging purposes.
 *
 * \param[in] ipc The IPC context
 *
 * \pre The lock doesn't need to be held
 * \remark The lock won't be acquired at any time and may block
 */
extern void ipc_dump(ipc_context_t *ipc);
