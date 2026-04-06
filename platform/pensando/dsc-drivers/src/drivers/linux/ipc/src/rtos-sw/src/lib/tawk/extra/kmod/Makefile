# SPDX-License-Identifier: MIT or GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

obj-m := tawk.o
tawk-objs :=  main.o ../../ipc.o ../../platform/platform.o ../../device/device.o ../../device/pcie.o ../../hardware/hardware.o ../../hardware/mailbox.o ../../hardware/rstsigs.o ../../datalink/datalink.o ../../datalink/fifo.o ../../datalink/rstfsm.o ../../protocol.o ../../transport/transport.o ../../transport/cfgfsm.o ../zephyr_compat/zephyr_compat.o
KVERSION = $(shell uname -r)
ccflags-y = -I$(CURDIR)/../../include -I$(CURDIR)/../zephyr_compat/include -I$(CURDIR)/libc_compat -std=gnu99 -DIPC_BUILD_KERNEL_MODULE=1
ccflags-y += -I$(CURDIR)/../../../../../registry/tawk_ipc

all:
	make -C /lib/modules/$(KVERSION)/build M=$(CURDIR) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(CURDIR) clean
