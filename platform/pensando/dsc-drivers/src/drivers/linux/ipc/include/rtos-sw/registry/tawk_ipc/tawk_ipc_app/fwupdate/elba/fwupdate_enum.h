// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  fwupdate/elba/fwupdate_enum.h
///  This file defines enumeration for fwupdate operations
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

// elba bootflash partitions
typedef enum flash_partition {
    FLASH_UBOOT_A       = 0,  // u-Boot partition A, ie uboota
    FLASH_UBOOT_B       = 1,  // u-Boot partition B, ie ubootb
    FLASH_KERNEL_A      = 2,  // kernel partition A, ie fwa
    FLASH_KERNEL_B      = 3,  // kernel partition B, ie fwb
    FLASH_USERVARS_DTB  = 4,  // uservars partition, ie dtb
    FLASH_CPLD          = 5,  // cpld firmware in spi flash
    FLASH_CPLD_UFM1     = 6,  // ufm1 partition in cpld flash
    FLASH_CPLD_UFM2     = 7,  // ufm2 partition in cpld flash
    FLASH_PARTITION_MAX = 8   // invalid firmware partition
} flash_partition_t;

// firmware selection indicating which firmware to boot
typedef enum firmware_type_e {
    FIRMWARE_TYPE_MAINFW_A = 0, // boot from main firmware A
    FIRMWARE_TYPE_MAINFW_B = 1, // boot from main firmware B
    FIRMWARE_TYPE_GOLDFW   = 2, // boot from gold firmware
    FIRMWARE_TYPE_MAX      = 3  // invalid boot option
} firmware_type_t;

typedef enum flash_image_type_id {
     IMAGE_TYPE_KERNEL          =  1,  //  zephyr image
     IMAGE_TYPE_UBOOTA          =  2,  //  uboota image
     IMAGE_TYPE_UBOOTB          =  3,  //  ubootb image
     IMAGE_TYPE_DTB             =  4,  //  dtb - device config
     IMAGE_TYPE_CPLD            =  5,  //  cpld
     IMAGE_TYPE_CPLD_UFM1       =  6,  //  ufm1
     IMAGE_TYPE_CPLD_UFM2       =  7,  //  ufm2
     IMAGE_TYPE_MAX             =  8   //  invalid image type id
} flash_image_type_id_t;

// card information
typedef struct {
    uint8_t sw_version[64]; // SW Version
    uint32_t board_id;      // Board id
    uint8_t asic_type;      // ASIC type
    uint8_t cpld_id;        // CPLD ID
    uint8_t is_cpld_gold;   // is cpld goldfw running
    uint8_t major_rev;      // major revision
    uint8_t minor_rev;      // minor revision
    uint8_t boot0_version;  // store boot0 version information
    uint8_t fwupdate_ipc_version;
} __PACKED__ fwupdate_get_card_info_t;

typedef enum partition_op_code {
     GET_PARTITION_COUNT   =  0,  // get partition count
     GET_PARTITION_INFO    =  1,  // get partition data
} partition_op_code_t;

#define FW_VERSION_STRING_SIZE 64

// partition info
typedef struct fwudpate_get_partition_info_s {
    flash_partition_t      partition_id;         // partition id
    const char             partition_name[16];   // partition name
    flash_image_type_id_t  image_type_id;        // image type id
    const char             image_type_name[16];  // image type name
    uint64_t               partition_size;       // partition size
    uint8_t                slot_id;              // slot id 0-A, 1-B, 2-G
    bool                   is_running;          // is running firmware
    bool                   is_startup;          // is startup firmware
    uint8_t                fw_version[FW_VERSION_STRING_SIZE];
} __PACKED__ fwupdate_get_partition_info_t;

// start using the FIRMWARE_TYPE_MAX
#define FLASH_FWSEL_ARRAY_ENTRY_SIZE (FIRMWARE_TYPE_MAX + 2)

static inline flash_partition_t
image_name_to_partition_id (const char *image_name)
{
    if (strcmp(image_name, "uboot-a") == 0) {
        return FLASH_UBOOT_A;
    } else if (strcmp(image_name, "uboot-b") == 0) {
        return FLASH_UBOOT_B;
    } else if (strcmp(image_name, "fw-a") == 0) {
        return FLASH_KERNEL_A;
    } else if (strcmp(image_name, "fw-b") == 0) {
        return FLASH_KERNEL_B;
    } else if (strcmp(image_name, "dtb") == 0) {
        return FLASH_USERVARS_DTB;
    } else if (strcmp(image_name, "cpld") == 0) {
        return FLASH_CPLD;
    } else {
        return FLASH_PARTITION_MAX;
    }
}

static inline const char*
partition_id_to_image_name (flash_partition_t id)
{
    switch (id) {
    case FLASH_UBOOT_A:
        return "uboot-a";
    case FLASH_UBOOT_B:
        return "uboot-b";
    case FLASH_KERNEL_A:
        return "fw-a";
    case FLASH_KERNEL_B:
        return "fw-b";
    case FLASH_USERVARS_DTB:
        return "dtb";
    case FLASH_CPLD:
        return "cpld";
    case FLASH_CPLD_UFM1:
        return "ufm1";
    case FLASH_CPLD_UFM2:
        return "ufm2";
    default :
        return "unknown";
    }
}

static inline bool
is_uboot_imageid (flash_partition_t image_id)
{
    if ((image_id == FLASH_UBOOT_A) || (image_id == FLASH_UBOOT_B)) {
        return true;
    } else {
        return false;
    }
}

static inline bool
is_kernel_imageid (flash_partition_t image_id)
{
    if ((image_id == FLASH_KERNEL_A) || (image_id == FLASH_KERNEL_B)) {
        return true;
    } else {
        return false;
    }
}

static inline bool
is_dtb_imageid (flash_partition_t image_id)
{
    if (image_id == FLASH_USERVARS_DTB) {
        return true;
    } else {
        return false;
    }
}

static inline bool
is_boot0_imageid (flash_partition_t image_id)
{
    return false;
}

static inline bool
is_fw_cfg_imageid (flash_partition_t image_id)
{
    return false;
}

static inline bool
is_cpld_imageid (flash_partition_t image_id)
{
    switch (image_id) {
    case FLASH_CPLD:
        return true;
    default:
        return false;
    }
}

static inline bool
is_relocatable_imageid(flash_partition_t image_id)
{
    if (is_uboot_imageid(image_id) ||
        is_boot0_imageid(image_id)) {
        return false;
    }

    return true;
}

static inline bool
is_goldfw_type(firmware_type_t type_id)
{
    switch (type_id) {
    case FIRMWARE_TYPE_GOLDFW:
        return true;
    default:
        return false;
    }
}

// TODO - remove the below func once this array variable "supported_fw_type"
// start using the FIRMWARE_TYPE_MAX
static inline int
get_max_num_of_partitions ()
{
    return FLASH_FWSEL_ARRAY_ENTRY_SIZE;
}

#ifdef __cplusplus
}
#endif

#endif // !__FWUPDATE_ENUM_H__
