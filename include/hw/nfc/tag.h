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

enum t1t_command_set {
    RALL_COMMAND = 0x00,
    RID_COMMAND = 0x78
};

/* [Type 1 Tag Operation Specification 5.7] */
struct t1t_common_hdr {
    uint8_t cmd;
};

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

struct t2t_common_hdr {
    uint8_t cmd;
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

struct t3t_common_hdr {
    uint8_t len;
    uint8_t cmd;
};

enum t3t_command_set {
    POLLING_COMMAND = 0x00,
    CHECK_COMMAND = 0x06,
    UPDATE_COMMAND = 0x08
};

enum t3t_response_code {
    POLLING_RESPONSE = 0x01,
    CHECK_RESPONSE = 0x07,
    UPDATE_RESPONSE = 0x09
};

enum {
    T3T_BLOCK_LEN_BIT = 0x80
};

struct t3t_check_command {
    uint8_t len;
    uint8_t cmd;
    uint8_t id[8];
    uint8_t nsv;
    uint16_t scl[];
} __attribute__((packed));

struct t3t_check_command_tail {
    uint8_t nbl;
    uint8_t bl[];
} __attribute__((packed));

struct t3t_check_response {
    uint8_t len;
    uint8_t code;
    uint8_t id[8];
    uint8_t status1;
    uint8_t status2;
    uint8_t nbl;
    uint8_t data[];
} __attribute__((packed));

enum t4t_file_select{
    NONE,
    CC_SELECT,
    NDEF_SELECT
};

struct t4t_app_sel_command {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    uint8_t lc;
    uint8_t data[7];
    uint8_t le;
} __attribute__((packed));

struct t4t_app_sel_response {
    uint8_t sw1;
    uint8_t sw2;
} __attribute__((packed));

struct t4t_cc_sel_command {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    uint8_t lc;
    uint8_t data[2];
} __attribute__((packed));

struct t4t_cc_sel_response {
    uint8_t sw1;
    uint8_t sw2;
} __attribute__((packed));

struct t4t_ndef_sel_command {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    uint8_t lc;
    uint8_t data[2];
} __attribute__((packed));

struct t4t_ndef_sel_response {
    uint8_t sw1;
    uint8_t sw2;
} __attribute__((packed));

struct t4t_rb_command {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    uint8_t le;
} __attribute__((packed));

struct t4t_rb_response {
    uint8_t data[0];
    uint8_t data_tail[];
} __attribute__((packed));

union command_packet {
    struct t1t_common_hdr t1t;
    struct t2t_common_hdr t2t;
    struct t3t_common_hdr t3t;

    struct t1t_rall_command rall_cmd;
    struct t1t_rid_command rid_cmd;
    struct t2t_read_command read_cmd;
    struct t3t_check_command check_cmd;
    struct t4t_app_sel_command app_sel_cmd;
    struct t4t_cc_sel_command cc_sel_cmd;
    struct t4t_ndef_sel_command ndef_sel_cmd;
    struct t4t_rb_command rb_cmd;
};

union response_packet {
    struct t1t_rall_response rall_rsp;
    struct t1t_rid_response rid_rsp;
    struct t2t_read_response read_rsp;
    struct t3t_check_response check_rsp;
    struct t4t_app_sel_response app_sel_rsp;
    struct t4t_cc_sel_response cc_sel_rsp;
    struct t4t_ndef_sel_response ndef_sel_rsp;
    struct t4t_rb_response rb_rsp;
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

/**
 * There is no specific size defined in T3T spec.
 */
enum {
    T3T_BLOCK_SIZE = 16,
    T3T_BLOCK_NUM = 64,
    T3T_MEMORY_SIZE = T3T_BLOCK_NUM * T3T_BLOCK_SIZE
};

struct nfc_t3t_raw {
    uint8_t mem[T3T_MEMORY_SIZE];
};

struct nfc_t3t_format {
    uint8_t ver;
    uint8_t nbr;
    uint8_t nbw;
    uint8_t nmaxb[2];
    uint8_t unused[4];
    uint8_t writef;
    uint8_t rwflag;
    uint8_t ln[3];
    uint8_t cs[2];
    uint8_t data[T3T_BLOCK_NUM - 1][T3T_BLOCK_SIZE];
} __attribute__((packed));

union nfc_t3t {
    struct nfc_t3t_raw raw;
    struct nfc_t3t_format format;
};

/**
 * There is no specific size defined in T3T spec.
 * CC size is defined in [T4TOP4] Table 5.
 */
struct nfc_t4t_format {
    uint8_t cc[15];
    uint8_t data[1024];
} __attribute__((packed));

union nfc_t4t {
    struct nfc_t4t_format format;
};

struct nfc_tag {
    enum nfc_tag_type type;
    union {
        union nfc_t1t t1;
        union nfc_t2t t2;
        union nfc_t3t t3;
        union nfc_t4t t4;
    }t;
};

#define INIT_NFC_T1T(tag_, uid_, res_) \
    tag_ = { \
        .type = T1T, \
        .t.t1.format.uid = uid_, \
        .t.t1.format.res = res_ \
    }

#define INIT_NFC_T2T(tag_, internal_, lock_, cc_) \
    tag_ = { \
        .type = T2T, \
        .t.t2.format.internal = internal_, \
        .t.t2.format.lock = lock_, \
        .t.t2.format.cc = cc_ \
    }

#define INIT_NFC_T3T(tag_, v_, r_, w_, nb_, u_, wf_, rw_, ln_, cs_) \
    tag_ = { \
        .type = T3T, \
        .t.t3.format.ver = v_, \
        .t.t3.format.nbr = r_, \
        .t.t3.format.nbw = w_, \
        .t.t3.format.nmaxb = nb_, \
        .t.t3.format.unused = u_, \
        .t.t3.format.writef = wf_, \
        .t.t3.format.rwflag = rw_, \
        .t.t3.format.ln = ln_, \
        .t.t3.format.cs = cs_, \
    }

#define INIT_NFC_T4T(tag_, cc_) \
    tag_ = { \
        .type = T4T, \
        .t.t4.format.cc = cc_, \
    }

#define FORMAT_NFC_T1T(tag_, uid_, res_) \
    { \
    static const uint8_t uid[] = uid_; \
    static const uint8_t res[] = res_; \
    tag_->type = T1T; \
    memset(tag_->t.t1.format.data, 0, sizeof(tag_->t.t1.format.data)); \
    memcpy(tag_->t.t1.format.uid, uid, sizeof(uid)); \
    memcpy(tag_->t.t1.format.res, res, sizeof(res)); \
    }

#define FORMAT_NFC_T2T(tag_, itl_, lock_, cc_) \
    { \
    static const uint8_t itl[] = itl_; \
    static const uint8_t lock[] = lock_; \
    static const uint8_t cc[] = cc_; \
    tag_->type = T2T; \
    memset(tag_->t.t2.format.data, 0, sizeof(tag_->t.t2.format.data)); \
    memcpy(tag_->t.t2.format.internal, itl, sizeof(itl)); \
    memcpy(tag_->t.t2.format.lock, lock, sizeof(lock));   \
    memcpy(tag_->t.t2.format.cc, cc, sizeof(cc)); \
    }

#define FORMAT_NFC_T3T(tag_, v_, r_, w_, nb_, u_, wf_, rw_, ln_, cs_) \
    { \
    static const uint8_t nb[] = nb_; \
    static const uint8_t u[] = u_; \
    static const uint8_t ln[] = ln_; \
    static const uint8_t cs[] = cs_; \
    tag_->type = T3T; \
    memset(tag_->t.t3.format.data, 0, sizeof(tag_->t.t3.format.data)); \
    tag_->t.t3.format.ver = v_; \
    tag_->t.t3.format.nbr = r_; \
    tag_->t.t3.format.nbw = w_; \
    memcpy(tag_->t.t3.format.nmaxb, nb, sizeof(nb)); \
    memcpy(tag_->t.t3.format.unused, u, sizeof(u));   \
    tag_->t.t3.format.writef = wf_; \
    tag_->t.t3.format.rwflag = rw_; \
    memcpy(tag_->t.t3.format.ln, ln, sizeof(ln)); \
    memcpy(tag_->t.t3.format.cs, cs, sizeof(cs)); \
    }

#define FORMAT_NFC_T4T(tag_, cc_) \
    { \
    static const uint8_t cc[] = cc_; \
    tag_->type = T4T; \
    memset(tag_->t.t4.format.data, 0, sizeof(tag_->t.t4.format.data)); \
    memcpy(tag_->t.t4.format.cc, cc, sizeof(cc)); \
    }

extern struct nfc_tag nfc_tags[4];

int
nfc_tag_set_data(struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len);

int
nfc_tag_format(struct nfc_tag* tag);

size_t
process_t1t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);

size_t
process_t2t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);

size_t
process_t3t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);

size_t
process_t4t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp);
#endif
