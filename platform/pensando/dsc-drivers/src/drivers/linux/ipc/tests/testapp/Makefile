# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

IPC_DIR := ../..
TAWK_REGISTRY := $(IPC_DIR)/include/rtos-sw/registry/tawk_ipc
TAWK_DIR := $(IPC_DIR)/include/rtos-sw/src/lib/tawk

CFLAGS=-I$(TAWK_REGISTRY)				\
       -I$(TAWK_DIR)/extra/zephyr_compat/include	\
       -I$(IPC_DIR)/include 					\
       -MD -g -Wall -Werror -std=gnu99

TEST_SRCS=main.c $(IPC_DIR)/include/header/protocol.c

all: tests

tests:
	gcc $(CFLAGS) $(TEST_SRCS) -o testapp

clean:
	rm -f testapp*
