# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

include ${MKDEFS}/pre.mk
MODULE_ASIC     := salina
MODULE_ARCH     := aarch64
MODULE_TARGET   := ipc.submake
include ${MKDEFS}/post.mk
