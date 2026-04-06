// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  fwupdate/salina/fwupdate_enum.h
///  This file defines enumerations for fwupdate operations
///
//----------------------------------------------------------------------------

#ifndef __FWUPDATE_ENUM_H__
#define __FWUPDATE_ENUM_H__

#ifdef __cplusplus
extern "C" {
#endif

// define the packed attribute if not already defined
#ifndef __PACKED__
#define __PACKED__ __attribute__((packed))
#endif

#define FLASH_ERASE_SECTOR_SIZE 4096

#if (defined(CONFIG_FWUPDATE_AINIC_MODE) && !defined(CONFIG_FWUPDATE_AINIC_MODE_LENI))
typedef enum flash_partition {
    FLASH_A35_BOOT0                  = 0,  // A35-Boot0 partition, ie a35_boot0
    FLASH_A35_BL1_A                  = 1,  // A35-BL1 partition 0, ie a35_bl1_0
    FLASH_A35_BL1_B                  = 2,  // A35-BL1 partition 1, ie a35_bl1_1
    FLASH_A35_GOLD_FIP               = 3,  // A35-Gold partition, ie a35_gold_fip
    FLASH_A35_UBOOT_A                = 4,  // A35-u-Boot partition A, ie uboota
    FLASH_A35_UBOOT_B                = 5,  // A35-u-Boot partition B, ie ubootb
    FLASH_A35_KERNEL_A               = 6,  // A35-kernel partition A, ie fwa
    FLASH_A35_KERNEL_B               = 7,  // A35-kernel partition B, ie fwb
    FLASH_A35_RUN_CFG_A              = 8,  // A35-run time cfg0 partition, ie dtb
    FLASH_A35_RUN_CFG_B              = 9,  // A35-run time cfg1 partition, ie dtb
    FLASH_A35_GOLD_UBOOT             = 10, // A35-gold uboot partition
    FLASH_A35_FW_CFG_A               = 11, // A35-firmware cfg0 partition,
    FLASH_A35_FW_CFG_B               = 12, // A35-firmware cfg1 partition,
    FLASH_A35_PEN_TRUST_RUN_FW_A     = 13, // PenTrust Runtime FW Slot 0 partition
    FLASH_A35_PEN_TRUST_RUN_FW_B     = 14, // PenTrust Runtime FW Slot 1 partition
    FLASH_A35_PEN_TRUST_SOFT_ROM_A   = 15, // PenTrust SoftROM 0 partition
    FLASH_A35_PEN_TRUST_SOFT_ROM_B   = 16, // PenTrust SoftROM 1 partition
    // flash cpld should keep in the last i.e before partition_max
    // this above enum values should match with the index of struct
    // flash_info_t g_flash_info[]
    FLASH_CPLD                       = 17, // cpld- its in SPI flash
    FLASH_CPLD_GOLD                  = 18, // cpld gold
    FLASH_CPLD_UFM1                  = 19, // ufm1 partition in cpld flash
    FLASH_CPLD_UFM2                  = 20, // ufm2 partition in cpld flash
    FLASH_PARTITION_MAX              = 21 // invalid firmware partition
} flash_partition_t;

typedef enum flash_image_type_id {
     IMAGE_TYPE_BOOT0           =  0,  //  a35 boot0 image
     IMAGE_TYPE_BL1_A           =  1,  //  a35 bl1a image
     IMAGE_TYPE_BL1_B           =  2,  //  a35 bl1b image
     IMAGE_TYPE_KERNEL          =  3,  //  zephyr image
     IMAGE_TYPE_UBOOTA          =  4,  //  uboota image
     IMAGE_TYPE_UBOOTB          =  5,  //  ubootb image
     IMAGE_TYPE_UBOOTG          =  6,  //  ubootg image
     IMAGE_TYPE_DTB             =  7,  //  dtb - device config
     IMAGE_TYPE_FW_CFG          =  8,  //  fw_cfg - catalog info
     IMAGE_TYPE_PENTRUST_RUN   =  9,  //  pentrust runtime
     IMAGE_TYPE_PENTRUST_SOFT  = 10,  //  pentrust softrom
     IMAGE_TYPE_CPLD            = 11,  //  cpld
     IMAGE_TYPE_CPLD_UFM1       = 12,  //  ufm1 image
     IMAGE_TYPE_CPLD_UFM2       = 13,  //  ufm2 image
     IMAGE_TYPE_MAX             = 14   //  invalid image type id
} flash_image_type_id_t;

#else         // Applicable for DPU MODE and CONFIG_FWUPDATE_AINIC_MODE_LENI
typedef enum flash_partition {
    FLASH_A35_BOOT0                  = 0,  // A35-Boot0 partition, ie a35_boot0
    FLASH_A35_BL1_A                  = 1,  // A35-BL1 partition 0, ie a35_bl1_0
    FLASH_A35_BL1_B                  = 2,  // A35-BL1 partition 1, ie a35_bl1_1
    FLASH_A35_GOLD_FIP               = 3,  // A35-Gold partition, ie a35_gold_fip
    FLASH_A35_UBOOT_A                = 4,  // A35-u-Boot partition A, ie uboota
    FLASH_A35_UBOOT_B                = 5,  // A35-u-Boot partition B, ie ubootb
    FLASH_A35_KERNEL_A               = 6,  // A35-kernel partition A, ie fwa
    FLASH_A35_KERNEL_B               = 7,  // A35-kernel partition B, ie fwb
    FLASH_A35_RUN_CFG_A              = 8,  // A35-run time cfg0 partition, ie dtb
    FLASH_A35_RUN_CFG_B              = 9,  // A35-run time cfg1 partition, ie dtb
    FLASH_N1_GOLD_UBOOT              = 10, // n1 gold uboot
    FLASH_N1_GOLD_KERNEL             = 11, // n1 gold kernel
    FLASH_N1_BOOT0                   = 12, // N1-Boot0 partition, ie n1_boot0
    FLASH_N1_UBOOT_A                 = 13, // N1-u-Boot partition A,ie n1_fip_a
    FLASH_N1_UBOOT_B                 = 14, // N1-u-Boot partition B,ie n1_fip_b
    FLASH_A35_GOLD_UBOOT             = 15, // A35-gold uboot partition
    FLASH_A35_FW_CFG_A               = 16, // A35-firmware cfg0 partition,
    FLASH_A35_FW_CFG_B               = 17, // A35-firmware cfg1 partition,
    FLASH_A35_PEN_TRUST_RUN_FW_A     = 18, // PenTrust Runtime FW Slot 0 partition
    FLASH_A35_PEN_TRUST_RUN_FW_B     = 19, // PenTrust Runtime FW Slot 1 partition
    FLASH_A35_PEN_TRUST_SOFT_ROM_A   = 20, // PenTrust SoftROM 0 partition
    FLASH_A35_PEN_TRUST_SOFT_ROM_B   = 21, // PenTrust SoftROM 1 partition
    // flash cpld should keep in the last i.e before partition_max
    // this above enum values should match with the index of struct
    // flash_info_t g_flash_info[]
    FLASH_CPLD                       = 22, // cpld- its in SPI flash
    FLASH_CPLD_GOLD                  = 23, // cpld gold
    FLASH_CPLD_UFM1                  = 24, // ufm1 partition in cpld flash
    FLASH_CPLD_UFM2                  = 25, // ufm2 partition in cpld flash
    FLASH_PARTITION_MAX              = 26 // invalid firmware partition
} flash_partition_t;

typedef enum flash_image_type_id {
     IMAGE_TYPE_BOOT0           =  0,  //  a35 boot0 image
     IMAGE_TYPE_BL1_A           =  1,  //  a35 bl1a image
     IMAGE_TYPE_BL1_B           =  2,  //  a35 bl1b image
     IMAGE_TYPE_KERNEL          =  3,  //  zephyr image
     IMAGE_TYPE_UBOOTA          =  4,  //  uboota image
     IMAGE_TYPE_UBOOTB          =  5,  //  ubootb image
     IMAGE_TYPE_UBOOTG          =  6,  //  ubootg image
     IMAGE_TYPE_DTB             =  7,  //  dtb - device config
     IMAGE_TYPE_FW_CFG          =  8,  //  fw_cfg - catalog info
     IMAGE_TYPE_PENTRUST_RUN    =  9,  //  pentrust runtime
     IMAGE_TYPE_PENTRUST_SOFT   = 10,  //  pentrust softrom
     IMAGE_TYPE_CPLD            = 11,  //  cpld
     IMAGE_TYPE_N1_BOOT0        = 12,  // n1 boot0 image
     IMAGE_TYPE_N1_UBOOTA       = 13,  // n1 uboota image
     IMAGE_TYPE_N1_UBOOTB       = 14,  // n1 ubootb image
     IMAGE_TYPE_N1_UBOOTG       = 15,  // n1 ubootg image
     IMAGE_TYPE_N1_KERNEL       = 16,  // n1 gold kernel image
     IMAGE_TYPE_CPLD_UFM1       = 17,  //  ufm1 image
     IMAGE_TYPE_CPLD_UFM2       = 18,  //  ufm2 image
     IMAGE_TYPE_MAX             = 19   //  invalid image type id
} flash_image_type_id_t;
#endif

// firmware selection indicating which firmware to boot
typedef enum firmware_type_e {
    FIRMWARE_TYPE_MAINFW_A    = 0, // boot from A35 firmware A
    FIRMWARE_TYPE_MAINFW_B    = 1, // boot from A35 firmware B
    FIRMWARE_TYPE_GOLDFW      = 2, // boot from A35 gold firmware
#ifdef CONFIG_FWUPDATE_AINIC_MODE
    FIRMWARE_TYPE_MAX         = 3  // invalid boot option
#else
    FIRMWARE_TYPE_N1_MAINFW_A = 3, // boot from N1 firmware A
    FIRMWARE_TYPE_N1_MAINFW_B = 4, // boot from N1 firmware B
    FIRMWARE_TYPE_N1_GOLDFW   = 5, // boot from N1 gold firmware
    FIRMWARE_TYPE_MAX         = 6  // invalid boot option
#endif
} firmware_type_t;

// card info
typedef struct {
    uint8_t  sw_version[64];// SW version
    uint32_t board_id;      // Board id
    uint8_t asic_type;      // ASIC type
    uint8_t cpld_id;        // CPLD ID
    uint8_t is_cpld_gold;   // is cpld goldfw running
    uint8_t major_rev;      // major revision
    uint8_t minor_rev;      // minor revision
    uint8_t ts_minute;      // timestamp minute in BCD format
    uint8_t ts_hour;        // timestamp hour in BCD format
    uint8_t ts_day;         // timestamp day in BCD format
    uint8_t ts_month;       // timestamp month in BCD format
    uint8_t ts_year;        // timestamp year in BCD format
    uint8_t boot0_version;  // store boot0 version information
    uint8_t fwupdate_ipc_version; // version information to handle
                                  // this structure params in future
                                  // between zephyr and linux
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    uint8_t n1_boot0_version;     // store n1 boot0 version information
    uint8_t reserved[431];        // reserved this buffer for enhancement
#else
    uint8_t reserved[432];  // reserved this buffer for enhancement
#endif
} __PACKED__ fwupdate_get_card_info_t;

typedef enum partition_op_code {
     GET_PARTITION_COUNT   =  0,  // get partition count
     GET_PARTITION_INFO    =  1,  // get partition data
} partition_op_code_t;

#define FW_VERSION_STRING_SIZE 64

// partition info
typedef struct {
    flash_partition_t      partition_id;         // partition id
    flash_image_type_id_t  image_type_id;        // image type id
    char                   partition_name[16];   // partition name
    char                   image_type_name[16];  // image type name
    uint64_t               partition_size;       // partition size
    uint8_t                slot_id;              // slot id 0-A, 1-B, 2-G
    bool                   is_running;          // is running firmware
    bool                   is_startup;          // is startup firmware
    uint8_t                fw_version[FW_VERSION_STRING_SIZE];
} __PACKED__ fwupdate_get_partition_info_t;

#define FLASH_FWSEL_ARRAY_ENTRY_SIZE FIRMWARE_TYPE_MAX

static inline flash_partition_t
image_name_to_partition_id (const char *image_name)
{
    if (strcmp(image_name, "boot0") == 0) {
        return FLASH_A35_BOOT0;
    } else if (strcmp(image_name, "uboot-a") == 0) {
        return FLASH_A35_UBOOT_A;
    } else if (strcmp(image_name, "uboot-b") == 0) {
        return FLASH_A35_UBOOT_B;
    } else if (strcmp(image_name, "fw-a") == 0) {
        return FLASH_A35_KERNEL_A;
    } else if (strcmp(image_name, "fw-b") == 0) {
        return FLASH_A35_KERNEL_B;
    } else if (strcmp(image_name, "dtb") == 0) {
        return FLASH_A35_RUN_CFG_A;
    } else if (strcmp(image_name, "dtb-b") == 0) {
        return FLASH_A35_RUN_CFG_B;
    } else if (strcmp(image_name, "fw_cfg") == 0) {
        return FLASH_A35_FW_CFG_A;
    } else if (strcmp(image_name, "fw_cfg-b") == 0) {
        return FLASH_A35_FW_CFG_B;
    } else if (strcmp(image_name, "cpld") == 0) {
        return FLASH_CPLD;
    } else if (strcmp(image_name, "ufm1") == 0) {
        return FLASH_CPLD_UFM1;
    } else if (strcmp(image_name, "ufm2") == 0) {
        return FLASH_CPLD_UFM2;
    } else if (strcmp(image_name, "cpld-g") == 0) {
        return FLASH_CPLD_GOLD;
    } else if (strcmp(image_name, "a35-golduboot") == 0) {
        return FLASH_A35_GOLD_UBOOT;
    } else if (strcmp(image_name, "a35-goldfip") == 0) {
        return FLASH_A35_GOLD_FIP;
    } else if (strcmp(image_name, "a35-bl1-0") == 0) {
        return FLASH_A35_BL1_A;
    } else if (strcmp(image_name, "a35-bl1-1") == 0) {
        return FLASH_A35_BL1_B;
    } else if (strcmp(image_name, "a35-pentrustfw-0") == 0) {
        return FLASH_A35_PEN_TRUST_RUN_FW_A;
    } else if (strcmp(image_name, "a35-pentrustfw-1") == 0) {
        return FLASH_A35_PEN_TRUST_RUN_FW_B;
    } else if (strcmp(image_name, "a35-pentrustsrfw-0") == 0) {
        return FLASH_A35_PEN_TRUST_SOFT_ROM_A;
    } else if (strcmp(image_name, "a35-pentrustsrfw-1") == 0) {
        return FLASH_A35_PEN_TRUST_SOFT_ROM_B;
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    } else if (strcmp(image_name, "n1-uboot-g") == 0) {
        return FLASH_N1_GOLD_UBOOT;
    } else if (strcmp(image_name, "n1-kernel-g") == 0) {
        return FLASH_N1_GOLD_KERNEL;
    } else if (strcmp(image_name, "n1-boot0") == 0) {
        return FLASH_N1_BOOT0;
    } else if (strcmp(image_name, "n1-uboot-a") == 0) {
        return FLASH_N1_UBOOT_A;
    } else if (strcmp(image_name, "n1-uboot-b") == 0) {
        return FLASH_N1_UBOOT_B;
#endif
    } else {
        return FLASH_PARTITION_MAX;
    }
}

static inline const char*
partition_id_to_image_name (flash_partition_t id)
{
    switch (id) {
    case FLASH_A35_BOOT0:
        return "boot0";
    case FLASH_A35_UBOOT_A:
        return "uboot-a";
    case FLASH_A35_UBOOT_B:
        return "uboot-b";
    case FLASH_A35_KERNEL_A:
        return "fw-a";
    case FLASH_A35_KERNEL_B:
        return "fw-b";
    case FLASH_A35_RUN_CFG_A:
        return "dtb";
    case FLASH_A35_RUN_CFG_B:
        return "dtb-b";
    case FLASH_A35_FW_CFG_A:
        return "fw_cfg";
    case FLASH_A35_FW_CFG_B:
        return "fw_cfg-b";
    case FLASH_CPLD:
        return "cpld";
    case FLASH_CPLD_UFM1:
        return "ufm1";
    case FLASH_CPLD_UFM2:
        return "ufm2";
    case FLASH_CPLD_GOLD:
        return "cpld-g";
    case FLASH_A35_GOLD_UBOOT:
        return "a35-golduboot";
    case FLASH_A35_GOLD_FIP:
        return "a35-goldfip";
    case FLASH_A35_BL1_A:
        return "a35-bl1-0";
    case FLASH_A35_BL1_B:
        return "a35-bl1-1";
    case FLASH_A35_PEN_TRUST_RUN_FW_A:
        return "a35-pentrustfw-0";
    case FLASH_A35_PEN_TRUST_RUN_FW_B:
        return "a35-pentrustfw-1";
    case FLASH_A35_PEN_TRUST_SOFT_ROM_A:
        return "a35-pentrustsrfw-0";
    case FLASH_A35_PEN_TRUST_SOFT_ROM_B:
        return "a35-pentrustsrfw-1";
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FLASH_N1_GOLD_UBOOT:
        return "n1-uboot-g";
    case FLASH_N1_GOLD_KERNEL:
        return "n1-kernel-g";
    case FLASH_N1_BOOT0:
        return "n1-boot0";
    case FLASH_N1_UBOOT_A:
        return "n1-uboot-a";
    case FLASH_N1_UBOOT_B:
        return "n1-uboot-b";
#endif
    default :
        return "unknown";
    }
}

static inline bool
is_uboot_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_UBOOT_A:
    case FLASH_A35_UBOOT_B:
    case FLASH_A35_GOLD_UBOOT:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FLASH_N1_GOLD_UBOOT:
    case FLASH_N1_UBOOT_A:
    case FLASH_N1_UBOOT_B:
#endif
        return true;
    default :
        return false;
    }
}

static inline bool
is_kernel_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_KERNEL_A:
    case FLASH_A35_KERNEL_B:
    case FLASH_A35_GOLD_FIP:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FLASH_N1_GOLD_KERNEL:
#endif
        return true;
    default :
        return false;
    }
}

static inline bool
is_dtb_imageid (flash_partition_t image_id)
{
    if (image_id == FLASH_A35_RUN_CFG_A) {
        return true;
    } else if (image_id == FLASH_A35_RUN_CFG_B) {
        return true;
    } else if (image_id == FLASH_A35_FW_CFG_A) {
        return true;
    } else if (image_id == FLASH_A35_FW_CFG_B) {
        return true;
    } else {
        return false;
    }
}

static inline bool
is_boot0_imageid (flash_partition_t image_id)
{
    if (image_id == FLASH_A35_BOOT0) {
        return true;
    }
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    if (image_id == FLASH_N1_BOOT0) {
        return true;
    }
#endif
    return false;
}

static inline bool
is_bl1_fw_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_BL1_A:
    case FLASH_A35_BL1_B:
        return true;
    default:
        return false;
    }
}

static inline bool
is_pentrust_fw_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_PEN_TRUST_RUN_FW_A:
    case FLASH_A35_PEN_TRUST_RUN_FW_B:
        return true;
    default:
        return false;
    }
}

static inline bool
is_pentrust_sftrom_fw_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_PEN_TRUST_SOFT_ROM_A:
    case FLASH_A35_PEN_TRUST_SOFT_ROM_B:
        return true;
    default:
        return false;
    }
}

static inline bool
is_cpld_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_CPLD:
    case FLASH_CPLD_GOLD:
    case FLASH_CPLD_UFM1:
    case FLASH_CPLD_UFM2:
        return true;
    default:
        return false;
    }
}

static inline bool
is_relocatable_imageid (flash_partition_t image_id)
{
    if (is_uboot_imageid(image_id) ||
        is_bl1_fw_imageid(image_id) ||
        is_boot0_imageid(image_id)) {
        return false;
    }

    return true;
}

static inline bool
is_mainfwa_type(firmware_type_t type_id)
{
    switch (type_id) {
    case FIRMWARE_TYPE_MAINFW_A:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FIRMWARE_TYPE_N1_MAINFW_A:
#endif
        return true;
    default:
        return false;
    }
}

static inline bool
is_goldfw_type (firmware_type_t type_id)
{
    switch (type_id) {
    case FIRMWARE_TYPE_GOLDFW:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FIRMWARE_TYPE_N1_GOLDFW:
#endif
        return true;
    default:
        return false;
    }
}

static inline bool
is_gold_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_GOLD_FIP:
    case FLASH_A35_GOLD_UBOOT:
    case FLASH_CPLD_GOLD:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FLASH_N1_GOLD_KERNEL:
    case FLASH_N1_GOLD_UBOOT:
#endif
        return true;
    default:
        return false;
    }
}

static inline bool
is_mainfw_shared_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_A35_BOOT0:
    case FLASH_CPLD:
    case FLASH_CPLD_UFM1:
    case FLASH_CPLD_UFM2:
#ifndef CONFIG_FWUPDATE_AINIC_MODE
    case FLASH_N1_BOOT0:
#endif
        return true;
    default:
        return false;
    }
}

static inline int
get_max_num_of_partitions ()
{
    return FIRMWARE_TYPE_MAX;
}

#ifdef __cplusplus
}
#endif

#endif // !__FWUPDATE_ENUM_H__
