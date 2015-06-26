/* Copyright (C) 2014 Mozilla Foundation
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

#ifndef hw_snep_h
#define hw_snep_h

#include <stddef.h>
#include <stdint.h>

#ifndef BITORDER_MSB_FIRST
#define BITORDER_MSB_FIRST 0
#endif

enum {
    SNEP_VERSION_MAJOR = 0x01,
    SNEP_VERSION_MINOR = 0x00
};

enum snep_msg {
    /* request codes */
    SNEP_REQ_CONTINUE = 0x00,
    SNEP_REQ_GET = 0x01,
    SNEP_REQ_PUT = 0x02,
    SNEP_REQ_REJECT = 0x7f,
    /* response codes */
    SNEP_RSP_CONTINUE = 0x80,
    SNEP_RSP_SUCCESS = 0x81,
    SNEP_RSP_NOT_FOUND = 0xc0,
    SNEP_RSP_EXCESS_DATA = 0xc1,
    SNEP_RSP_BAD_REQUEST = 0xc2,
    SNEP_RSP_NOT_IMPLEMENTED = 0xe0,
    SNEP_RSP_UNSUPPORTED_VERSION = 0xe1,
    SNEP_RSP_REJECT = 0xff
};

struct snep_version {
#if BITORDER_MSB_FIRST
    uint8_t major:4;
    uint8_t minor:4;
#else
    uint8_t minor:4;
    uint8_t major:4;
#endif
} __attribute__((packed));

struct snep {
    struct snep_version ver;
    uint8_t msg;
    uint32_t len;
    uint8_t info[0];
} __attribute__((packed));

size_t
snep_create_msg(struct snep* snep, enum snep_msg msg, uint32_t len);

size_t
snep_create_req_put(struct snep* snep, uint32_t len);

size_t
snep_create_rsp_success(struct snep* snep, uint32_t len);

size_t
snep_create_rsp_excess_data(struct snep* snep);

size_t
snep_create_rsp_bad_request(struct snep* snep);

size_t
snep_create_rsp_not_implemented(struct snep* snep);

size_t
snep_create_rsp_unsupported_version(struct snep* snep);

#endif
