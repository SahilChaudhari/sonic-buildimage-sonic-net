# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

TOPDIR := ../../../../../..
IPC_DIR := ../..
TAWK_REGISTRY := $(IPC_DIR)/include/rtos-sw/registry/tawk_ipc
TAWK_DIR := $(IPC_DIR)/include/rtos-sw/src/lib/tawk

IPCDRV_DIR:=$(TOPDIR)/nic/sdk/platform/ipcdrv
IPCCMD_DIR:=$(TOPDIR)/nic/sdk/platform/ipccmd
LIBYAML_DIR:=$(TOPDIR)/platform/src/third-party/libyaml
TEST_SRCS:= test_app.c \
            $(IPC_DIR)/include/header/protocol.c \
            popval.c \
            tawktest_cmd.c \
            arg_parse.c \
            testset.c

CFLAGS=-I$(TAWK_REGISTRY)				\
       -I$(TAWK_DIR)/extra/zephyr_compat/include	\
       -I$(IPCCMD_DIR)/include                                 \
       -I$(IPCDRV_DIR)/include                                 \
       -I$(IPCCMD_DIR)/src/platform/posix/include              \
       -I$(TOPDIR)/nic/sdk                                     \
       -I$(IPC_DIR)/include						\
       -MD -g -std=gnu99

IPCDRV_SRCS:=$(IPCDRV_DIR)/src/ipcdrv.c   \
	      $(IPCDRV_DIR)/src/protocol.c


IPCCMD_SRCS:=$(IPCCMD_DIR)/src/platform/posix/ipc_cmd_client.c \
	      $(IPCCMD_DIR)/src/platform/posix/ipc_cmd_server.c \
             $(IPCCMD_DIR)/src/platform/posix/ipc_cmd_dev.c

IPCCMD_INCS=-I$(IPCCMD_DIR)/src/platform/posix/include \
            -I$(IPCCMD_DIR)/src    \
            -I$(IPCCMD_DIR)/include       \
            -I$(IPCCMD_DIR)/../ipcdrv/include    \

LIBYAML_INCS=-I$(LIBYAML_DIR)/include

all: test_app

libyaml.a:
	@make --directory=$(LIBYAML_DIR)

TEST_SRCS += $(IPCDRV_SRCS)
TEST_SRCS += $(IPCCMD_SRCS)
TEST_SRCS += $(LIBYAML_DIR)/libyaml.a

CFLAGS += $(IPCCMD_INCS) \
          $(LIBYAML_INCS)

test_app: libyaml.a
	gcc $(CFLAGS) $(TEST_SRCS) -lpthread -o stress_test

clean:
	rm -f stress_test*
