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

#ifndef nfc_tag_h
#define nfc_tag_h

#include "qemu-common.h"

enum {
    MAXIMUM_SUPPORTED_TAG_SIZE = 1024
};

enum nfc_tag_type {
    T1T = 0,
    T2T,
    T3T,
    T4T,
};

struct common_command_hdr {
    uint8_t cmd;
};

enum t1t_command_set {
    RALL_COMMAND = 0x00,
    RID_COMMAND = 0x78
};

/* [Type 1 Tag Operation Specification 5.7] */
struct t1t_rall_command {
    uint8_t cmd;
};

struct t1t_rall_response {
    uint8_t payload[122];
    uint8_t status;
};

/* [Digital], Table48 */
struct t1t_rid_command {
    uint8_t cmd;
    uint8_t add;
    uint8_t data;
    uint8_t uid;
};

struct t1t_rid_response {
    uint8_t hr[2];
    uint8_t uid[4];
    uint8_t status;
};

/* [Digital], Table51 */
enum t2t_command_set {
    READ_SEGMENT_COMMAND = 0x10,
    READ_COMMAND = 0x30,
};

struct t2t_read_command {
    uint8_t cmd;
    uint8_t bno;
};

/* [Digital], Table52 */
struct t2t_read_response {
    uint8_t payload[16];
    /* status byte is not documented in NFC Digital.
     * But libnfc-nci will read an extra status byte so add it in response packet.
     */
    uint8_t status;
};

union command_packet {
    struct common_command_hdr common;
    struct t1t_rall_command rall_cmd;
    struct t1t_rid_command rid_cmd;
    struct t2t_read_command read_cmd;
};

union response_packet {
    struct t1t_rall_response rall_rsp;
    struct t1t_rid_response rid_rsp;
    struct t2t_read_response read_rsp;
};

/* [Type 1 Tag Operation Specification 2.1/2.2]
 * Static Memory Structure.
 */
enum {
    T1T_HRO = 0x11,
    T1T_HR1 = 0x00
};

enum {
    T1T_STATIC_MEMORY_SIZE = 120
};

struct nfc_t1t_raw {
    uint8_t mem[T1T_STATIC_MEMORY_SIZE];
};

struct nfc_t1t_format {
    uint8_t uid[8];
    uint8_t data[96];
    uint8_t res[16];
} __attribute__((packed));

union nfc_t1t {
    struct nfc_t1t_raw raw;
    struct nfc_t1t_format format;
};

/* [Type 2 Tag Operation Specification 2.1]
 * Static Memory Structure.
 */
enum {
    T2T_STATIC_MEMORY_SIZE = 64
};

struct nfc_t2t_raw {
    uint8_t mem[T2T_STATIC_MEMORY_SIZE];
};

struct nfc_t2t_format {
    uint8_t internal[10];
    uint8_t lock[2];
    uint8_t cc[4];
    uint8_t data[48];
} __attribute__((packed));

union nfc_t2t {
    struct nfc_t2t_raw raw;
    struct nfc_t2t_format format;
};

struct nfc_tag {
    enum nfc_tag_type type;
    union {
        union nfc_t1t t1;
        union nfc_t2t t2;
    };
};

#define INIT_NFC_T1T(tag_, uid_, res_) \
    tag_ = { \
        .type = T1T, \
        .t1.format.uid = uid_, \
        .t1.format.res = res_ \
    }

#define INIT_NFC_T2T(tag_, internal_, lock_, cc_) \
    tag_ = { \
        .type = T2T, \
        .t2.format.internal = internal_, \
        .t2.format.lock = lock_, \
        .t2.format.cc = cc_ \
    }

extern struct nfc_tag nfc_tags[2];

int
nfc_tag_set_data(const struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len);

size_t
process_t1t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);

size_t
process_t2t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);
#endif
