/* Copyright (C) 2013 Mozilla Foundation
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#ifndef hw_llcp_h
#define hw_llcp_h

#ifndef BITORDER_MSB_FIRST
#define BITORDER_MSB_FIRST 0
#endif

#include <stddef.h>
#include <stdint.h>

enum {
    LLCP_VERSION_MAJOR = 0x01,
    LLCP_VERSION_MINOR = 0x01
};

enum {
    LLCP_NUMBER_OF_SAPS = 64
};

/*
 * LLCP parameters
 */

enum llcp_param {
    LLCP_PARAM_VERSION = 0x01,
    LLCP_PARAM_MIUX = 0x02,
    LLCP_PARAM_WKS = 0x03,
    LLCP_PARAM_LTO = 0x04,
    LLCP_PARAM_RW = 0x05,
    LLCP_PARAM_SN = 0x06,
    LLCP_PARAM_OPT = 0x07,
    LLCP_PARAM_SDREQ = 0x08,
    LLCP_PARAM_SDRES = 0x09
};

struct llcp_param_version {
    uint8_t type;
    uint8_t len;
#if BITORDER_MSB_FIRST
    uint8_t major:4;
    uint8_t minor:4;
#else
    uint8_t minor:4;
    uint8_t major:4;
#endif
};

struct llcp_param_miux {
    uint8_t type;
    uint8_t len;
    uint16_t miux;
};

struct llcp_param_wks {
    uint8_t type;
    uint8_t len;
    uint16_t wks;
};

struct llcp_param_lto {
    uint8_t type;
    uint8_t len;
    uint16_t lto;
};

struct llcp_param_rw {
    uint8_t type;
    uint8_t len;
#if BITORDER_MSB_FIRST
    uint8_t unused:4;
    uint8_t rw:4;
#else
    uint8_t rw:4;
    uint8_t unused:4;
#endif
};

struct llcp_param_sn {
    uint8_t type;
    uint8_t len;
    uint8_t uri[];
};

struct llcp_param_opt {
    uint8_t type;
    uint8_t len;
#if BITORDER_MSB_FIRST
    uint8_t unused:6;
    uint8_t lsc:2;
#else
    uint8_t lsc:2;
    uint8_t unused:6;
#endif
};

struct llcp_param_sdreq {
    uint8_t type;
    uint8_t len;
    uint8_t tid;
    uint8_t uri[];
};

struct llcp_param_sdres {
    uint8_t type;
    uint8_t len;
    uint8_t tid;
#if BITORDER_MSB_FIRST
    uint8_t unused:2;
    uint8_t sap:6;
#else
    uint8_t sap:6;
    uint8_t unused:2;
#endif
};

/*
 * LLCP packets
 */

enum nfc_llcp_ptype {
    LLCP_PTYPE_SYMM = 0x0,
    LLCP_PTYPE_PAX = 0x1,
    LLCP_PTYPE_AGF = 0x2,
    LLCP_PTYPE_UI = 0x3,
    LLCP_PTYPE_CONNECT = 0x4,
    LLCP_PTYPE_DISC = 0x5,
    LLCP_PTYPE_CC = 0x6,
    LLCP_PTYPE_DM = 0x7,
    LLCP_PTYPE_FRMR = 0x8,
    LLCP_PTYPE_SNL = 0x9,
    LLCP_PTYPE_I = 0xc,
    LLCP_PTYPE_RR = 0xd,
    LLCP_PTYPE_RNR = 0xe
};

struct llcp_packet {
#if BITORDER_MSB_FIRST
    uint16_t dsap:6;
    uint16_t ptype:4;
    uint16_t ssap:6;
#else
    uint8_t ptype0:2;
    uint8_t dsap:6;
    uint8_t ssap:6;
    uint8_t ptype1:2;
#endif
};

struct llcp_version {
    uint8_t ns:4;
    uint8_t nr:4;
};

size_t
llcp_create_packet(struct llcp_packet* llcp, unsigned char dsap,
                   unsigned char ptype, unsigned char ssap);

unsigned char
llcp_ptype(const struct llcp_packet* llcp);

/* used during link establishment */
size_t
llcp_create_param_tail(uint8_t* p);

/*
 * LLCP connection state
 */

struct llcp_connection_state {
    /* data-link connection state variables; [LLCP], Sec 5.6.1 */
    uint16_t v_s;
    uint16_t v_sa;
    uint16_t v_r;
    uint16_t v_ra;
    /* data-link connection parameters; [LLCP], Sec 5.6.2 */
    uint8_t miu;
    uint8_t rw_l;
    uint8_t rw_r;
};

struct llcp_connection_state*
llcp_init_connection_state(struct llcp_connection_state* cs);

#endif
