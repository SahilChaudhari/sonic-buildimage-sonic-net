# TAWK IPC Linux Driver

## Deployment

### Development environment

```sh
make
sudo insmod tawk_ipc.ko
```

### RPM Package

```sh
make dist
rpmbuild -ta ~/tawk_ipc-*.tar.gz
# Kmods option
sudo yum install ~/rpmbuild/RPMS/*/{tawk_ipc-common,kmod-tawk_ipc}*.rpm
# Akmods option
sudo yum install ~/rpmbuild/RPMS/noarch/tawk_ipc-{common,akmod}*.rpm
# DKMS option
sudo yum install ~/rpmbuild/RPMS/noarch/tawk_ipc-{common,dkms}*.rpm
```

#### Build dependencies

If packages required to build are not installed:

```sh
rpmbuild -ts ~/tawk_ipc-*.tar.gz
sudo yum-builddep ~/rpmbuild/SRPMS/tawk_ipc*.src.rpm
```

#### Kernel compatibility

Kmod RPM supports weak-modules. Distributions with stable kABI (eg. RHEL) will reuse the `tawk_ipc.ko` provided for one kernel version with all kernel versions with compatible `Modules.symvers`.

By default, the kernel version to build for is derived from the latest installed kernel source (eg. `kernel(-rt|-aarch64)?-devel` RPM for RHEL). When prebuilding for a specific kernel, using the following will select that version, add its kernel source as a dependency to the SRPM, and uniquely identify the subpackage accordingly:

```sh
rpmbuild --define "kernel_version $(uname -r)" ...
```

> [!TIP]
> When using OracleLinux UEK, this must always be defined as `kernel-uek-devel` is not automatically selected by OracleLinux macros.

## Usage

The IPC linux driver supports two-way communication between X86 and A35. Userspace interacts with the kernel driver by reading and writing to a file descriptor. Each file descriptor has its own state (e.g. read queue and wait queues).

Making requests to from x86 to A35 would look something like:

```c
fd = open("/dev/tawkipcdev"); /* /dev/tawkipcdev is file that the driver exposes for userspace interaction */
write(fd, buffer1, buffer1_size); /* write request */
read(fd, buffer2, buffer2_size); /* read response */
close(fd); /* clean up and free all state associated with FD */
```

To handle requests from A35 to x86 it is required to register as an endpoint handler:

```c
fd = open("/dev/tawkipcdev");
ioctl(fd, TAWKIPCREGEP, <endpoint_number>); /* Register handler for endpoint_number */
read(fd, buffer1, buffer1_size); /* read A35 request */
write(fd, buffer2, buffer2_size); /* write response */
close(fd);
```

Each endpoint can have at most one handler.

### Exclusive Request Buffer Pools

Users can request that a file descriptor uses an exclusive pool of request buffers, rather than using the common pool. By default exclusive request buffer pools are not enabled. To enable exclusive pools the minimum size of the common pool must be decreased, by default the minimum size of the common pool is the total number of request buffers. Currently, enabling exclusive buffer pools must be done at driver load time:

```sh
sudo insmod tawk_pci.ko min_common_pool_size=n   (where n < total number of request buffers)
```

To create/resize an exclusive buffer pool:

```c
ioctl(fd, TAWKIPCSETPLSIZE, <n>);
```

Attempting to resize the buffer pool while there are items still in the FD's read queue is not permitted. Resizing an exclusive pool can be partially successful. \

To query the size of an exclusive buffer pool:

```c
ioctl(fd, TAWKIPCGETPLSIZE, <unused parameter>);
```

> [!NOTE]
>
> * There are no ordering guarantees.
> * The `read()` and `write()` handlers are currently blocking calls.
> * The `read()` call can handle only one response at a time, i.e. not as many responses as the user-supplied buffer permits.

## Implementation details

### How to issue requests?

There are three major phases in the lifecycle of a request (issues by "us"):

1. The userland application prepares a request and makes the `write()` syscall. In the syscall handler, the driver allocates an instance of `ipc_buffer_t` for submission into the IPC library and one `tawk_drv_req_state` to correlate the response with the userland request later. It then submits an instance of `ipc_buffer_t` into the IPC library and provides a callback `tawk_drv_handle_resp()` to call on completion.

2. When the IPC library is ready to complete a request, either with a response or an error condition, it invokes the provided callback.

3. The userspace application must make the `read()` syscall to receive a response in the blocking or non-blocking way. On successful completion, the driver releases both `tawk_drv_req_state` and `ipc_buffer_t`.

Please see an illustration below:

![Issue request](assets/issue-request.png)

The `write()` syscall allocates `ipc_buffer_t` from the IPC library. The allocation is only possible when the link is (acknowledged) up. When the link is down, the driver waits for the link up event in the `link_wait_q` wait queue. The allocation might fail if the last link down event was "fatal" (i.e. the userland application must close the file descriptor), or the userland application must handle a Unix signal. Either way, the driver must return from the `write()` syscall early.

It then allocates `tawk_drv_req_state` from the `req_free` list. Upon successful allocation, it atomically (spinlock) moves the allocated instance from the `req_free` list onto the `req_in_use` list. If `req_free` is empty, it uses `req_wait_q` to wait until `req_free` becomes non-empty. If the userland application receives a signal, `wait_event_interruptible_locked()` returns a non-zero code, in which case the syscall deallocates `ipc_buffer_t` and returns early.

Finally, `write()` submits a request with `ipc_request_send_async()`, but also only when the link is up. If, at this point, the link is not up, it must undo the allocation and return `ipc_buffer_t` to allow the core IPC library to ACK the link down with the remote side.

Eventually, the IPC library calls `tawk_drv_handle_resp()`. The function atomically adds the related `tawk_drv_req_state` onto the `read_q` list and signals `read_wait_q`. Please note, that now an instance of `tawk_drv_req_state` resides on two lists at the same time.

The `read()` syscall is more tolerable to the link condition. In the blocking mode, it uses `read_wait_q` to wait until `read_q` becomes non-empty or the "fatal" link down event occurs. Otherwise, it returns `EAGAIN`. Then, when the driver copies the response or error into the user-supplied buffer, and releases `ipc_buffer_t` and `tawk_drv_req_state`.

### How to handle requests?

It is symmetric to issuing IPC requests, but requires one additional phase in the request lifecycle:

1. The userland application must make an `ioctl()` to register a request handler with the IPC library. The control path will eventually reach `ipc_register_request_handler()` with a function pointer to `tawk_drv_handle_req()` in the driver to handle requests. It is a one-off invocation, i.e. the application does not need to re-register a handler after receiving a request.

2. Eventually, the IPC library calls `tawk_drv_handle_req()` and it allocates one `tawk_drv_alloc_rsp_state` from the pre-allocated response array. Additionally, it immediately places this `tawk_drv_alloc_rsp_state` onto the `read_q` list to unblock the `read()` syscall.

3. The userland application calls `read()` (blocking/non-blocking) and has a chance to read either an IPC response to a previously issued request, or an IPC request. In this case, we are concerned about the latter case. Additionally, `read()` removes `tawk_drv_alloc_rsp_state` from `read_q`.

4. Then the userland application calls `write()` to submit a response into the IPC library. It also "releases" `tawk_drv_alloc_rsp_state`, so that it is available for the next request.

Please find the response handling outline in the illustration below:

![Handle request](assets/handle-request.png)

### How to handle link down event?

The TAWK IPC driver on x86 supports three tactics to handle the IPC link down event:

1. Strict, when all userland applications must close their `/dev/tawkipcdevNNN` files to ACK the link down event. Until then, the link cannot go up for any application.

2. Moderately strict, when each individual userspace application must close the file descriptor, but it does not affect other applications. The link can go up again, and the `/dev/tawkipcdevNNN` might re-appear there, but the file descriptor would remain in the "invalid" state until the application reopens it.

3. "Invincible" mode, when the driver won't be bothered telling the userland application about the link down event. The IPC core library will fail all outstanding requests, but I/O on the affected file descriptor can proceed.

The user can select the behaviour at the module probe time.

The strict mode (1) is the default tactic, i.e. the graceful way for the application to handle the link down event is to close the file descriptor and reopen it to ACK. However, the application cannot reliably know if the last failed requests were legitimate failures on the remote side, or synthetic responses from the IPC core library! The "invincible" mode is a workaround for the applications that do not implement the outlined graceful ACK.

## Troubleshooting

The default `echo t > /proc/sysrq-trigger` is highly valuable in the TAWK IPC driver troubleshooting. It shows (almost) all kernel stack traces, including the workqueue ones.

Another debugging tactic is to find out the `/dev/tawkipcdevNNN` users with `lsof` and examine their stack with `/proc/PID/stack`.

The TAWK IPC driver supports the debugging build flavour, enabled with the `TAWK_DRV_DEBUG` macro (see [Makefile](Makefile)). The driver will create various files in debugfs (e.g. `/sys/kernel/debug/tawk_ipc/bar_access_stats`). Please note that the debugfs mount point might be OS-specific.

When the Linux kernel panic becomes an obstacle, the users can disable or defer it with the `panic=` parameter (or via `sysctl`). Additionally, `ghes.disable=y` sometimes might be helpful for the hosts that trigger kernel panic with arbitrary PCIe errors.

---

```yaml
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
```
