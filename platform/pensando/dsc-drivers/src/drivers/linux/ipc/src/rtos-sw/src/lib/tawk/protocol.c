// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#include <zephyr/kernel.h>
#include "protocol.h"


#define IPC_TRANSPORT_HDR_MASK(field_)                          \
  GENMASK64(((IPC_TRANSPORT_HDR_ ## field_ ## _LBN) +           \
             (IPC_TRANSPORT_HDR_ ## field_ ## _WIDTH) - 1),     \
            (IPC_TRANSPORT_HDR_ ## field_ ## _LBN))

#define IPC_TRANSPORT_HDR_FIELD_GET(hdr_, field_)       \
  FIELD_GET(IPC_TRANSPORT_HDR_MASK(field_), hdr_)

#define IPC_TRANSPORT_HDR_FIELD_PREP(field_, value_)    \
  FIELD_PREP(IPC_TRANSPORT_HDR_MASK(field_), value_)

#define IPC_TRANSPORT_HDR_FIELD_SET(hdr_, field_, value_)       \
  do {                                                          \
    (hdr_) &= ~IPC_TRANSPORT_HDR_MASK(field_);                  \
    (hdr_) |= IPC_TRANSPORT_HDR_FIELD_PREP(field_, value_);     \
  } while (0)


const ipc_hdr_t ipc_hdr_noop =
  (IPC_TRANSPORT_HDR_FIELD_PREP(TYPE, IPC_TRANSPORT_HDR_TYPE_NOOP)|
   IPC_TRANSPORT_HDR_FIELD_PREP(NOOP_RSVD0, 0));


bool ipc_hdr_validate(ipc_hdr_t hdr)
{
  /* N.B. we avoid accessor function here so that they can
   * assert the correctness of the header themselves */
  switch (IPC_TRANSPORT_HDR_FIELD_GET(hdr, TYPE)) {
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    if (IPC_TRANSPORT_HDR_FIELD_GET(hdr, NOOP_RSVD0) != 0)
      return false;
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    switch (IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_MSGCODE)) {
    case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
      if (IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_VER_MIN) >
          IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_VER_MAX))
        return false;
      break;

    case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
      break;

    case IPC_TRANSPORT_HDR_TPMSG_READY:
      if (IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_READY_RSVD0) != 0)
        return false;
      break;

    default:
      return false;
    }
    break;

  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPERR:
    if (IPC_TRANSPORT_HDR_FIELD_GET(hdr, PLD_LEN) != 0)
      return false;
    break;

  default:
    return false;
  }

  return true;
}

unsigned ipc_hdr_get_owner(ipc_hdr_t hdr)
{
  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, OWNER);
}

void ipc_hdr_set_owner(ipc_hdr_t *hdr, unsigned owner)
{
  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, OWNER, owner);
}


ipc_hdr_type_t ipc_hdr_get_type(ipc_hdr_t hdr)
{
  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TYPE);
}

void ipc_hdr_set_type(ipc_hdr_t *hdr,
                      ipc_hdr_type_t hdr_type)
{
  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    break;

  default:
    __ASSERT(0, "undefined hdr_type: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TYPE, hdr_type);
}


ipc_hdr_tpmsg_msgcode_t ipc_hdr_get_tpmsg_code(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    break;
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    __ASSERT(0, "invalid hdr_type for tpmsg code field: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_MSGCODE);
}

void ipc_hdr_set_tpmsg_code(ipc_hdr_t *hdr,
                            ipc_hdr_tpmsg_msgcode_t msgcode)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    break;
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    __ASSERT(0, "invalid hdr_type for tpmsg code field: %u", hdr_type);
    break;
  }

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    break;

  default:
    __ASSERT(0, "undefined msgcoden: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_MSGCODE, msgcode);
}

ipc_hdr_tag_t ipc_hdr_get_tag(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
    break;

  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    __ASSERT(0, "invalid hdr_type for tag field: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TAG);
}

void ipc_hdr_set_tag(ipc_hdr_t *hdr,
                     ipc_hdr_tag_t tag)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
    break;

  case IPC_TRANSPORT_HDR_TYPE_NOOP:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    __ASSERT(0, "invalid hdr_type for tag field: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TAG, tag);
}


size_t ipc_hdr_get_pld_len(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    break;

  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for pld len: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, PLD_LEN);
}

void ipc_hdr_set_pld_len(ipc_hdr_t *hdr,
                         size_t pld_len)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
    __ASSERT(pld_len == 0, "pld_len must be 0 in tperr");
    break;

  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for pld len: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, PLD_LEN, pld_len);
}

uint16_t ipc_hdr_get_tpmsg_cfg_ver_min(ipc_hdr_t hdr)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_min field: %u", msgcode);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_VER_MIN);
}

void ipc_hdr_set_tpmsg_cfg_ver_min(ipc_hdr_t *hdr,
                                      uint16_t ver_min)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(*hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_min field: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_CFG_VER_MIN, ver_min);
}

uint16_t ipc_hdr_get_tpmsg_cfg_ver_max(ipc_hdr_t hdr)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_max field: %u", msgcode);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_VER_MAX);
}

void ipc_hdr_set_tpmsg_cfg_ver_max(ipc_hdr_t *hdr,
                                      uint16_t ver_max)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(*hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_max field: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_CFG_VER_MAX, ver_max);
}


uint16_t ipc_hdr_get_tpmsg_cfg_caps_ini_max(ipc_hdr_t hdr)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_min field: %u", msgcode);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_CAPS_INI_MAX);
}

void ipc_hdr_set_tpmsg_cfg_caps_ini_max(ipc_hdr_t *hdr,
                                           uint16_t ini_max)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(*hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for ver_min field: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_CFG_CAPS_INI_MAX, ini_max);
}


uint16_t ipc_hdr_get_tpmsg_cfg_caps_tgt_max(ipc_hdr_t hdr)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for tgt_max field: %u", msgcode);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPMSG_CFG_CAPS_TGT_MAX);
}

void ipc_hdr_set_tpmsg_cfg_caps_tgt_max(ipc_hdr_t *hdr,
                                           uint16_t tgt_max)
  {
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(*hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    __ASSERT(0, "invalid msgcode for tgt_max field: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_CFG_CAPS_TGT_MAX, tgt_max);
}

void ipc_hdr_clr_tpmsg_ready_rsvd0(ipc_hdr_t *hdr)
{
  ipc_hdr_tpmsg_msgcode_t msgcode = ipc_hdr_get_tpmsg_code(*hdr);

  switch (msgcode) {
  case IPC_TRANSPORT_HDR_TPMSG_READY:
    break;

  case IPC_TRANSPORT_HDR_TPMSG_CFG_VER:
  case IPC_TRANSPORT_HDR_TPMSG_CFG_CAPS:
    __ASSERT(0, "invalid msgcode for rsvd0 field: %u", msgcode);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPMSG_READY_RSVD0, 0);
}

ipc_hdr_req_endpoint_t ipc_hdr_get_req_endpoint(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for endpoint field: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, REQ_ENDPOINT);
}

void ipc_hdr_set_req_endpoint(ipc_hdr_t *hdr,
                              ipc_hdr_req_endpoint_t ep)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_REQ:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for endpoint field: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, REQ_ENDPOINT, ep);
}


ipc_hdr_rsp_errno_t ipc_hdr_get_rsp_errno(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for errno field: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, RSP_ERRNO);
}

void ipc_hdr_set_rsp_errno(ipc_hdr_t *hdr,
                           ipc_hdr_rsp_errno_t rsp_errno)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_RSP:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for errno field: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, RSP_ERRNO, rsp_errno);
}

ipc_hdr_tperr_errcode_t ipc_hdr_get_tperr_errcode(ipc_hdr_t hdr)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for tperr code field: %u", hdr_type);
    break;
  }

  return IPC_TRANSPORT_HDR_FIELD_GET(hdr, TPERR_ERRCODE);
}

void ipc_hdr_set_tperr_errcode(ipc_hdr_t *hdr,
                               ipc_hdr_tperr_errcode_t errcode)
{
  ipc_hdr_type_t hdr_type = ipc_hdr_get_type(*hdr);

  switch (hdr_type) {
  case IPC_TRANSPORT_HDR_TYPE_TPERR:
    break;

  case IPC_TRANSPORT_HDR_TYPE_TPMSG:
  case IPC_TRANSPORT_HDR_TYPE_REQ:
  case IPC_TRANSPORT_HDR_TYPE_RSP:
  case IPC_TRANSPORT_HDR_TYPE_NOOP:
    __ASSERT(0, "invalid hdr_type for tperr code field: %u", hdr_type);
    break;
  }

  IPC_TRANSPORT_HDR_FIELD_SET(*hdr, TPERR_ERRCODE, errcode);
}
