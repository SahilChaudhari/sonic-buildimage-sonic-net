/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2025 Advanced Micro Devices, Inc */

#ifndef _IONIC_PHC_STATE_H_
#define _IONIC_PHC_STATE_H_

struct ionic_phc_state {
	__u32 seq;
	__u32 rsvd;
	__aligned_u64 mask;
	__aligned_u64 tick;
	__aligned_u64 nsec;
	__aligned_u64 frac;
	__u32 mult;
	__u32 shift;
};

#endif /* _IONIC_PHC_STATE_H_ */
