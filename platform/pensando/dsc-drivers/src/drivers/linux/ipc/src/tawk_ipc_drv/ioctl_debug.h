// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <linux/ioctl.h>

// Please make sure these do not overlap with the production codes from ioctl.h
#define TAWKIPCRESETLINK _IO('B', 100)
