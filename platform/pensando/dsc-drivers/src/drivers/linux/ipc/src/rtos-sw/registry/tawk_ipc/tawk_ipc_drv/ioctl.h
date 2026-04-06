// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

#include <linux/ioctl.h>

#define TAWKIPCREGEP _IOW('B', 8, unsigned long)
#define TAWKIPCDEREGEP _IOW('B', 9, unsigned long)

#define TAWKIPCGETPLSIZE _IOR('B', 16, unsigned int)
#define TAWKIPCSETPLSIZE _IOW('B', 17, unsigned int)

#define TAWKIPCGETREORDER _IOR('B', 18, unsigned int)
#define TAWKIPCSETREORDER _IOW('B', 19, unsigned int)

#define TAWKIPCGETLINKDOWNMODE _IOR('B', 20, unsigned int)
#define TAWKIPCSETLINKDOWNMODE _IOW('B', 21, unsigned int)
