// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.
//
//----------------------------------------------------------------------------
///
/// \file
///  ipc pal commands.h
///  This file defines commands and structures for pal ipc.
///
//----------------------------------------------------------------------------

#ifndef __IPC_PAL_SERVER_H__
#define __IPC_PAL_SERVER_H__

#ifdef __cplusplus
extern "C" {
#if 0
} /* close to calm emacs autoindent */
#endif
#endif

#include "tawk_ipc_cmd/opcode.h"
#include "tawk_ipc_app/base.h"

// enum command response status
// TODO remove this and use IPC API Error
typedef enum ipc_pal_status_e  {
    IPC_PAL_CMD_STATUS_SUCCESS      = 0,     /* cmd success, end of data */
    IPC_PAL_CMD_STATUS_SUCCESS_MOD  = 1,     /* cmd success, mid of data */
    IPC_PAL_CMD_STATUS_TIMEOUT      = 2,     /* cmd timeout */
    IPC_PAL_CMD_STATUS_FAILED       = 3,     /* cmd failed */
    IPC_PAL_CMD_STATUS_CMD_BUSY     = 4,     /* cmd channel busy */
} ipc_pal_status;

// -----FRU Related Command -----
// enum command message type
typedef enum ipc_fru_cmdtype_e  {
    IPC_MSG_FRU_READ = 1,
} ipc_fru_cmdtype_t;

#define IPC_CMD_FRU_READ IPC_CMD_MAKE(IPC_MSG_FRU_READ,1)

typedef struct ipc_fru_cmdreq_s {
    uint32_t mod_offset;
    uint32_t nretry;
}__PACKED__ ipc_fru_cmdreq_t;

#define IPC_FRU_DATA_SIZE 1000
// fru command response
// status       command completion status
// data         fru data
typedef struct ipc_fru_cmdrsp_s {
    ipc_pal_status status;
    uint8_t data[IPC_FRU_DATA_SIZE];
}__PACKED__ ipc_fru_cmdrsp_t;

typedef union {
    ipc_fru_cmdreq_t req;
    ipc_fru_cmdrsp_t rsp;
}__PACKED__ ipc_fru_msg_t;

// -----QSFP----
typedef enum ipc_qsfp_cmdtype_e {
    IPC_MSG_IS_QSFP_PORT_PSNT                  = 1,
    IPC_MSG_IS_QSFP_PORT_RESET                 = 2,
    IPC_MSG_QSFP_SET_PORT                      = 3,
    IPC_MSG_QSFP_RESET_PORT                    = 4,
    IPC_MSG_QSFP_SET_LOW_POWER_MODE            = 5,
    IPC_MSG_QSFP_RESET_LOW_POWER_MODE          = 6,
    IPC_MSG_QSFP_SET_LED                       = 7,
    IPC_MSG_QSFP_READ                          = 8,
    IPC_MSG_QSFP_WRITE                         = 9,
    IPC_MSG_QSFP_DOM_READ                      = 10,
    IPC_MSG_WRITE_QSFP_TEMP                    = 11,
    IPC_MSG_QSFP_SET_PORT_LINK_STATUS          = 12,
    IPC_MSG_QSFP_POWER_EN                      = 13,
    IPC_MSG_QSFP_POWER_DIS                     = 14,
    IPC_MSG_IS_QSFP_PORT_POWERED_UP            = 15,
    IPC_MSG_IS_QSFP_PORT_POWER_FAIL            = 16,
    IPC_MSG_IS_QSFP_PORT_POWER_CTRL_PRESENT    = 17,
} ipc_qsfp_cmdtype_t;

#define IPC_CMD_IS_QSFP_PORT_PSNT                   IPC_CMD_MAKE(IPC_MSG_IS_QSFP_PORT_PSNT          ,1)
#define IPC_CMD_IS_QSFP_PORT_RESET                  IPC_CMD_MAKE(IPC_MSG_IS_QSFP_PORT_RESET         ,1)
#define IPC_CMD_QSFP_SET_PORT                       IPC_CMD_MAKE(IPC_MSG_QSFP_SET_PORT              ,1)
#define IPC_CMD_QSFP_RESET_PORT                     IPC_CMD_MAKE(IPC_MSG_QSFP_RESET_PORT            ,1)
#define IPC_CMD_QSFP_SET_LOW_POWER_MODE             IPC_CMD_MAKE(IPC_MSG_QSFP_SET_LOW_POWER_MODE    ,1)
#define IPC_CMD_QSFP_RESET_LOW_POWER_MODE           IPC_CMD_MAKE(IPC_MSG_QSFP_RESET_LOW_POWER_MODE  ,1)
#define IPC_CMD_QSFP_SET_LED                        IPC_CMD_MAKE(IPC_MSG_QSFP_SET_LED               ,1)
#define IPC_CMD_QSFP_READ                           IPC_CMD_MAKE(IPC_MSG_QSFP_READ                  ,1)
#define IPC_CMD_QSFP_WRITE                          IPC_CMD_MAKE(IPC_MSG_QSFP_WRITE                 ,1)
#define IPC_CMD_QSFP_DOM_READ                       IPC_CMD_MAKE(IPC_MSG_QSFP_DOM_READ              ,1)
#define IPC_CMD_WRITE_QSFP_TEMP                     IPC_CMD_MAKE(IPC_MSG_WRITE_QSFP_TEMP            ,1)
#define IPC_CMD_QSFP_SET_PORT_LINK_STATUS           IPC_CMD_MAKE(IPC_MSG_QSFP_SET_PORT_LINK_STATUS  ,1)
#define IPC_CMD_QSFP_POWER_EN                       IPC_CMD_MAKE(IPC_MSG_QSFP_POWER_EN  ,1)
#define IPC_CMD_QSFP_POWER_DIS                      IPC_CMD_MAKE(IPC_MSG_QSFP_POWER_DIS  ,1)
#define IPC_CMD_IS_QSFP_PORT_POWERED_UP             IPC_CMD_MAKE(IPC_MSG_IS_QSFP_PORT_POWERED_UP  ,1)
#define IPC_CMD_IS_QSFP_PORT_POWER_FAIL             IPC_CMD_MAKE(IPC_MSG_IS_QSFP_PORT_POWER_FAIL  ,1)
#define IPC_CMD_IS_QSFP_PORT_POWER_CTRL_PRESENT     IPC_CMD_MAKE(IPC_MSG_IS_QSFP_PORT_POWER_CTRL_PRESENT  ,1)

#define IPC_QSFP_REQ_DATA_SIZE  212
typedef struct ipc_qsfp_cmdreq_s {
    uint32_t mod_offset;
    uint32_t port;
    uint32_t bus;
    uint32_t regaddr; /* regaddr, offset */
    uint32_t devaddr;
    uint32_t size;
    uint32_t nretry;
    union {
    int cpld_data;
    uint8_t data[IPC_QSFP_REQ_DATA_SIZE];
    };
}__PACKED__ ipc_qsfp_cmdreq_t;

// TODO Check this #define PALX_QSFP_DATA_SIZE   1784
#define IPC_QSFP_RSP_DATA_SIZE   512
typedef struct ipc_qsfp_cmdrsp_s {
    ipc_pal_status status;
    uint32_t size;
    union {
        uint8_t qsfp_data[IPC_QSFP_RSP_DATA_SIZE];
        int cpld_data;
    };
}__PACKED__ ipc_qsfp_cmdrsp_t;

typedef union {
    ipc_qsfp_cmdreq_t req;
    ipc_qsfp_cmdrsp_t rsp;
}__PACKED__ ipc_qsfp_msg_t;

// -----CPLD-----
typedef enum ipc_cpld_cmdtype_e {
    IPC_MSG_SYSTEM_GET_LED          = 1,
    IPC_MSG_SYSTEM_SET_LED          = 2,
    IPC_MSG_SET_CARD_STATUS         = 3,
    IPC_MSG_WRITE_FW_VER_TO_CPLD    = 4,
    IPC_MSG_CPLD_REV                = 5,
    IPC_MSG_CPLD_ID                 = 6,
    IPC_MSG_CPLD_MINOR_REV          = 7,
    IPC_MSG_CPLD_BOOT_STATUS        = 8,
    IPC_MSG_GET_RESET_CAUSE         = 9,
    IPC_MSG_GET_RESET_EVENT         = 10,
    IPC_MSG_GET_CPLD_REG            = 11,
    IPC_MSG_SET_CPLD_REG            = 12,
} ipc_cpld_cmdtype_t;

#define IPC_CMD_SYSTEM_GET_LED           IPC_CMD_MAKE(IPC_MSG_SYSTEM_GET_LED          , 1)
#define IPC_CMD_SYSTEM_SET_LED           IPC_CMD_MAKE(IPC_MSG_SYSTEM_SET_LED          , 1)
#define IPC_CMD_SET_CARD_STATUS          IPC_CMD_MAKE(IPC_MSG_SET_CARD_STATUS         , 1)
#define IPC_CMD_WRITE_FW_VER_TO_CPLD     IPC_CMD_MAKE(IPC_MSG_WRITE_FW_VER_TO_CPLD    , 1)
#define IPC_CMD_CPLD_REV                 IPC_CMD_MAKE(IPC_MSG_CPLD_REV         , 1)
#define IPC_CMD_CPLD_ID                  IPC_CMD_MAKE(IPC_MSG_CPLD_ID          , 1)
#define IPC_CMD_CPLD_MINOR_REV           IPC_CMD_MAKE(IPC_MSG_CPLD_MINOR_REV   , 1)
#define IPC_CMD_CPLD_BOOT_STATUS         IPC_CMD_MAKE(IPC_MSG_CPLD_BOOT_STATUS , 1)
#define IPC_CMD_GET_RESET_CAUSE          IPC_CMD_MAKE(IPC_MSG_GET_RESET_CAUSE  , 1)
#define IPC_CMD_GET_RESET_EVENT          IPC_CMD_MAKE(IPC_MSG_GET_RESET_EVENT  , 1)
#define IPC_CMD_GET_CPLD_REG             IPC_CMD_MAKE(IPC_MSG_GET_CPLD_REG, 1)
#define IPC_CMD_SET_CPLD_REG             IPC_CMD_MAKE(IPC_MSG_SET_CPLD_REG, 1)

#define IPC_CPLD_DATA_SIZE   8

typedef struct cpld_reg_s {
    uint8_t addr;
    uint8_t data;
}__PACKED__ cpld_reg_t;

typedef struct ipc_cpld_cmdreq_s {
    uint32_t nretry;
    union {
    int cpld_data;
    uint8_t data[IPC_CPLD_DATA_SIZE];
    cpld_reg_t reg;
    };
}__PACKED__ ipc_cpld_cmdreq_t;

typedef struct ipc_cpld_cmdrsp_s {
    ipc_pal_status status;
    uint32_t size;
    union {
    int cpld_data;
    uint8_t system_data[IPC_CPLD_DATA_SIZE];
    cpld_reg_t reg;
    };
}__PACKED__ ipc_cpld_cmdrsp_t;

typedef union {
    ipc_cpld_cmdreq_t req;
    ipc_cpld_cmdrsp_t rsp;
}__PACKED__ ipc_cpld_msg_t;

// -----FWSEL-----
typedef enum ipc_fwsel_cmdtype_e  {
    IPC_MSG_FW_SEL = 1,
} ipc_fwsel_cmdtype_t;

#define IPC_CMD_FW_SEL IPC_CMD_MAKE(IPC_MSG_FW_SEL,1)

typedef enum ipc_fwsel_fwtype_e {
    IPC_FWSEL_FWTYPE_NONE,
    IPC_FWSEL_FWTYPE_MAINFWA,
    IPC_FWSEL_FWTYPE_MAINFWB,
    IPC_FWSEL_FWTYPE_GOLDFW,
}ipc_fwsel_fwtype_t;

// fwselcommandmessage
// typecommandtype
// fwtypefirmwaretype
typedef struct ipc_fwsel_cmdreq_s {
    ipc_fwsel_fwtype_t fwtype;
}__PACKED__ ipc_fwsel_cmdreq_t;

// fwselcommandresponse
// statuscommandcompletionstatus
typedef struct ipc_fwsel_cmdrsp_s {
    ipc_pal_status status;
    uint32_t size;
}__PACKED__ ipc_fwsel_cmdrsp_t;

typedef union {
    ipc_fwsel_cmdreq_t req;
    ipc_fwsel_cmdrsp_t rsp;
}__PACKED__ ipc_fwsel_msg_t;

// sensor commands
typedef enum ipc_sensor_cmdtype_e {
    IPC_MSG_SENSOR_PIN          = 1,
    IPC_MSG_SENSOR_POUT1        = 2,
    IPC_MSG_SENSOR_POUT2        = 3,
    IPC_MSG_SENSOR_VIN          = 4,
    IPC_MSG_SENSOR_VOUT1        = 5,
    IPC_MSG_SENSOR_VOUT2        = 6,
    IPC_MSG_SENSOR_BOARD_TEMP   = 7,
    IPC_MSG_SENSOR_DIE_TEMP     = 8,
    IPC_MSG_SENSOR_HW_CRIT_TEMP = 9,
} ipc_sensor_cmdtype_t;

#define IPC_CMD_SENSOR_PIN      IPC_CMD_MAKE(IPC_MSG_SENSOR_PIN, 1)
#define IPC_CMD_SENSOR_POUT1    IPC_CMD_MAKE(IPC_MSG_SENSOR_POUT1, 1)
#define IPC_CMD_SENSOR_POUT2    IPC_CMD_MAKE(IPC_MSG_SENSOR_POUT2, 1)

#define IPC_CMD_SENSOR_VIN      IPC_CMD_MAKE(IPC_MSG_SENSOR_VIN, 1)
#define IPC_CMD_SENSOR_VOUT1    IPC_CMD_MAKE(IPC_MSG_SENSOR_VOUT1, 1)
#define IPC_CMD_SENSOR_VOUT2    IPC_CMD_MAKE(IPC_MSG_SENSOR_VOUT2, 1)

#define IPC_CMD_SENSOR_BOARD_TEMP   IPC_CMD_MAKE(IPC_MSG_SENSOR_BOARD_TEMP, 1)
#define IPC_CMD_SENSOR_DIE_TEMP     IPC_CMD_MAKE(IPC_MSG_SENSOR_DIE_TEMP, 1)
#define IPC_CMD_SENSOR_HW_CRIT_TEMP IPC_CMD_MAKE(IPC_MSG_SENSOR_HW_CRIT_TEMP, 1)

// sensor command message
// type         command type
// sub_type     sensor command sub type
typedef struct ipc_sensor_cmdreq_s {
}__PACKED__ ipc_sensor_cmdreq_t;

// sensor command response
// status       command completion status
// size         response size
typedef struct ipc_sensor_cmdrsp_s {
    ipc_pal_status status;
    uint32_t size;
    union {
    uint8_t __data[4];
    int pin;
    int pout1;
    int pout2;
    int vin;
    int vout1;
    int vout2;
    int boardtemp;
    int dietemp;
    int hwcrittemp;
    };
}__PACKED__ ipc_sensor_cmdrsp_t;

typedef union {
    ipc_sensor_cmdreq_t req;
    ipc_sensor_cmdrsp_t rsp;
}__PACKED__ ipc_sensor_msg_t;

// -----FREQUENCY-----
typedef enum ipc_freq_cmdtype_e  {
    IPC_MSG_FREQ = 1,
} ipc_freq_cmdtype_t;

#define IPC_CMD_FREQ IPC_CMD_MAKE(IPC_MSG_FREQ,1)

typedef enum ipc_freq_type_e {
    IPC_FREQ_TYPE_CORE,
    IPC_FREQ_TYPE_CPU,
    IPC_FREQ_TYPE_P4_STAGE,
}ipc_freq_type_t;

// freqcommandmessage
// typecommandtype
// typefrequencytype
typedef struct ipc_freq_cmdreq_s {
    ipc_freq_type_t type;
}__PACKED__ ipc_freq_cmdreq_t;

// freqcommandresponse
// freqfrequency
// statuscommandcompletionstatus
typedef struct ipc_freq_cmdrsp_s {
    uint32_t freq;
    ipc_pal_status status;
    uint32_t size;
}__PACKED__ ipc_freq_cmdrsp_t;

typedef union {
    ipc_freq_cmdreq_t req;
    ipc_freq_cmdrsp_t rsp;
}__PACKED__ ipc_freq_msg_t;

#endif    // __IPC_PAL_SERVER_H__
