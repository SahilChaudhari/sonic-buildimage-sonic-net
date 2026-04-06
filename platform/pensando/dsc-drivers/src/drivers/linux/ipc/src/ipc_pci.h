// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#ifndef _IPC_PCI_H_
#define _IPC_PCI_H_

#define PCI_VENDOR_ID_PENSANDO        0x1dd8
#define PCI_DEVICE_ID_PENSANDO_TAWK_PF 0x1012
#define TAWK_DRIVER_NAME "tawk_ipc"
#define TAWK_PF_BAR 0

int tawk_drv_pci_register_driver(void);
void tawk_drv_pci_unregister_driver(void);

#endif /* _IPC_PCI_H_ */
