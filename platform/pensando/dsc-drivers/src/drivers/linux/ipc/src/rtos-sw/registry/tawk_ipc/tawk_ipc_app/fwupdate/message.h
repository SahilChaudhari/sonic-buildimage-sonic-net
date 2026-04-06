// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  fwupdate/message.h
///  This file defines functions and macros for fwupdate-ipc
///
//----------------------------------------------------------------------------

#ifndef __FWUPDATE_COMMANDS_H__
#define __FWUPDATE_COMMANDS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "include/sdk/base.hpp"
#include "tawk_ipc_cmd/opcode.h"
#ifdef SALINA
#include "salina/fwupdate_enum.h"
#else
#include "elba/fwupdate_enum.h"
#endif

// define the packed attribute if not already defined
#ifndef __PACKED__
#define __PACKED__ __attribute__((packed))
#endif

#define FWUPDATE_CMD_SIZE 1000

// firmware update command opcodes
enum fwupdate_cmd_opcode_e {
    FWUPDATE_CMD_OP_FWSEL       = 1, // tawk IPC command to select firmware
    FWUPDATE_CMD_OP_ERASE_FLASH = 2, // tawk IPC command to erase flash
    FWUPDATE_CMD_OP_WRITE_FLASH = 3, // tawk IPC command to write to flash
    FWUPDATE_CMD_OP_SET_BOOTOPS = 4, // tawk IPC command to set boot options
    FWUPDATE_CMD_OP_GET_CHECKSUM = 5, // tawk IPC command to get checksum
    FWUPDATE_CMD_OP_META_DATA_READ = 6, // tawk IPC command to read meta data
    FWUPDATE_CMD_OP_VERIFY_STARTUP_IMAGE = 7, // tawk IPC command to verify
                                              // startup image
    FWUPDATE_CMD_OP_GET_CARD_INFO = 8, // tawk IPC command to get card info
    FWUPDATE_CMD_OP_GET_PARTITION_INFO = 9, // tawk IPC command to get partition
                                            // info
    FWUPDATE_CMD_OP_ERASE_FLASH_SECTOR = 10, // tawk IPC command to erase flash
                                             // sector
    // tawk IPC command to enable or disable secure boot feature
    FWUPDATE_CMD_OP_SECURE_BOOT_UPDATE = 11,
};

// firmware update application error codes
typedef enum fwupdate_app_err_codes_ {
    FWUPDATE_ERR_OK                        = 0,   // no error
    FWUPDATE_ERR_INVALID_DST               = 1,   // invalid destination
    FWUPDATE_ERR_FLASH_MAP                 = 2,   // flash mapping error
    FWUPDATE_FLASH_ERASE_IN_PROGRESS       = 3,   // flash erase in progress
    FWUPDATE_IMAGE_CHECKSUM_IN_PROGRESS    = 4,   // checksum in progress
    FWUPDATE_ERR                           = 5,   // general error
    FWUPDATE_ERR_MAGIC_NUM                 = 6,   // magic number error
    FWUPDATE_ERR_MAX                       = 255  // maximum error code
} fwupdate_err_t;

typedef enum fwupdate_image_type_ {
    FWUPDATE_RUNNING_IMAGE      = 0,   // running firmware
    FWUPDATE_STARTUP_IMAGE      = 1,   // startup firmware
#ifdef CONFIG_FWUPDATE_AINIC_MODE
    FWUPDATE_IMAGE_INVALID      = 2,   // invalid firmware
#else
    FWUPDATE_N1_STARTUP_IMAGE   = 2,   // n1-startup firmware
    FWUPDATE_IMAGE_INVALID      = 3,   // invalid firmware
#endif
} fwupdate_image_type_t;

typedef enum secure_boot_option_ {
    SECURE_BOOT_OPTION_NONE    = 0,
    SECURE_BOOT_OPTION_ENABLE  = 1,
    SECURE_BOOT_OPTION_DISABLE = 2,
    SECURE_BOOT_OPTION_MAX     = 3,
} secure_boot_option_t;

#define HASH_SIZE 64 //hash size for SHA512
#define METADATA_BUFFER_SIZE    900
#define DTB_NAME_STRING_SIZE    64
#define DTB_VERSION_STRING_SIZE 32

// version number for firmware update commands
#define FWUPDATE_CMDS_VERSION_1 1

// define command for firmware selection image
#define FWUPDATE_CMD_FWSEL_IMAGE IPC_CMD_MAKE(FWUPDATE_CMD_OP_FWSEL, FWUPDATE_CMDS_VERSION_1)

// structure for firmware type message
typedef union {
    struct {
        fwupdate_image_type_t image_type;
    } req;

    struct {
        firmware_type_t  fw_type; // firmware A or B or gold
    } resp;
} __PACKED__ fwupdate_run_startup_image_msg_t;

// define command for erasing flash
#define FWUPDATE_CMD_ERASE_FLASH IPC_CMD_MAKE(FWUPDATE_CMD_OP_ERASE_FLASH, FWUPDATE_CMDS_VERSION_1)

// structure for erase flash message
typedef union {
    struct {
        flash_partition_t img_id;     // flash partition or image id
        uint64_t          erase_size; // erase size
    } req;

    struct {
        uint8_t status; // status of the erase operation, its a long running
                        // operation response inprogress apart from the last
                        // response
    } resp;
} __PACKED__ fwupdate_erase_flash_msg_t;

// define command for writing to flash
#define FWUPDATE_CMD_WRITE_FLASH IPC_CMD_MAKE(FWUPDATE_CMD_OP_WRITE_FLASH, FWUPDATE_CMDS_VERSION_1)

// structure for write flash message
typedef union {
    struct {
        flash_partition_t img_id; // flash partition or image id
        uint64_t flash_offset;    // flash offset for the write operation
        uint64_t size;            // size of the data
        uint8_t is_eof;           // end of File indicator
        uint8_t is_verify;        // represent-read data need to be verify with
                                  // i/p data or not (i.e value 1 is need verify
                                  // 0 - not required, it applicable only for
                                  // cpld)
        uint8_t data[];           // data to be written
    } req;

    struct {
        uint64_t          next_offset; // current flash offset after the write
                                       // operation, complete image is divided
                                       // in multiple chunks and send over ipc
    } resp;
} __PACKED__ fwupdate_write_flash_msg_t;

// define command for setting boot options
#define FWUPDATE_CMD_SET_BOOT_OPS IPC_CMD_MAKE(FWUPDATE_CMD_OP_SET_BOOTOPS, FWUPDATE_CMDS_VERSION_1)

// structure for boot options message
typedef union {
    struct {
        firmware_type_t fw_type;    // firmware A or B or gold
    } req;

    struct {
        // response structure as no response data required and
        // response is enough here
    } resp;
} __PACKED__ fwupdate_boot_ops_msg_t;

// define command for getting checksum from a installed image
#define FWUPDATE_CMD_GET_CHECKSUM IPC_CMD_MAKE(FWUPDATE_CMD_OP_GET_CHECKSUM, FWUPDATE_CMDS_VERSION_1)
typedef union {
    struct {
        flash_partition_t img_id;    // flash partition or image id
        uint64_t img_size;           // size of the image
    } req;

    struct {
        uint8_t status;              // status of the operation
        uint8_t hash[HASH_SIZE];     // calculated SHA512
    } resp;
}__PACKED__ fwupdate_get_checksum_msg_t;

// define command for reading meta data from flash
#define FWUPDATE_CMD_META_DATA_READ IPC_CMD_MAKE(FWUPDATE_CMD_OP_META_DATA_READ, FWUPDATE_CMDS_VERSION_1)

// structure for read meta data
typedef union {
    struct {
        flash_partition_t img_id; // image id UBOOT or KERNEL or DTB
    } req;

    struct {
        flash_partition_t img_id; // flash partition or image id
        uint8_t dtb_version[DTB_VERSION_STRING_SIZE]; // DTB version information
        uint8_t dtb_name[DTB_NAME_STRING_SIZE];       // DTB file name
        uint32_t size;                                // size of the data
        uint8_t data[METADATA_BUFFER_SIZE];           // data to be written
    } resp;
} __PACKED__ fwupdate_read_meta_data_msg_t;

// define command for reading meta data from flash
#define FWUPDATE_CMD_VERIFY_STARTUP_IMAGE IPC_CMD_MAKE(FWUPDATE_CMD_OP_VERIFY_STARTUP_IMAGE, FWUPDATE_CMDS_VERSION_1)

// structure for read meta data
typedef union {
    struct {
        flash_partition_t img_id; // image id UBOOT or KERNEL
    } req;

    struct {
        flash_partition_t img_id; // flash partition or image id
        uint8_t hash[HASH_SIZE];  // hash value
        uint32_t size;            // size of the data
        uint8_t status;           // status of the operation
        uint8_t data[METADATA_BUFFER_SIZE]; // data to be written
    } resp;
} __PACKED__ fwupdate_verify_startup_image_msg_t;

// define command for get card info
#define FWUPDATE_CMD_GET_CARD_INFO IPC_CMD_MAKE(FWUPDATE_CMD_OP_GET_CARD_INFO, FWUPDATE_CMDS_VERSION_1)

// structure for get card version
typedef union {
    struct {
    } req;

    struct {
        fwupdate_get_card_info_t card_info;
    } resp;
} __PACKED__ fwupdate_get_card_info_msg_t;

// define command for get partition info
#define FWUPDATE_CMD_GET_PARTITION_INFO IPC_CMD_MAKE(FWUPDATE_CMD_OP_GET_PARTITION_INFO, FWUPDATE_CMDS_VERSION_1)

// structure for get partition info
typedef union {
    struct {
        uint8_t partition_index;
        partition_op_code_t opcode;
    } req;

    struct {
        uint8_t partition_count;
        fwupdate_get_partition_info_t  partition_info[];
    } resp;
} __PACKED__ fwupdate_get_partition_info_msg_t;

// define command for erasing flash by sector
#define FWUPDATE_CMD_ERASE_FLASH_SECTOR IPC_CMD_MAKE(FWUPDATE_CMD_OP_ERASE_FLASH_SECTOR, FWUPDATE_CMDS_VERSION_1)

// structure for erase flash sector message
typedef union {
    struct {
        flash_partition_t img_id;       // flash partition or image id
        uint64_t          offset;       // flash offset should be multiples of
                                        // sector size (multiples of 4096)
        uint64_t          erase_size;   // erase size should be multiples of
                                        // sector size (multiples of 4096)
    } req;

    struct {
        uint8_t status; // status of the erase operation
    } resp;
} __PACKED__ fwupdate_erase_flash_sector_msg_t;

// define command to control the secure boot feature
#define FWUPDATE_CMD_SECURE_BOOT_UPDATE IPC_CMD_MAKE(FWUPDATE_CMD_OP_SECURE_BOOT_UPDATE, FWUPDATE_CMDS_VERSION_1)

#define SECURE_BOOT_UPDATE_MSG_RESERVED    900

// structure for enable or disable the secure feature in cpld
typedef union {
    struct {
        // secure boot option, enable or disable
        secure_boot_option_t secure_boot_option;
        // reserved
        uint8_t reserved[SECURE_BOOT_UPDATE_MSG_RESERVED];
    } req;

    struct {
        // status of the secure boot update operation
        uint32_t status;
        // reserved
        uint8_t reserved[SECURE_BOOT_UPDATE_MSG_RESERVED];
    } resp;
} __PACKED__ secure_boot_update_msg_t;

#ifdef __cplusplus
}
#endif

#endif // !__FWUPDATE_COMMANDS_H__
