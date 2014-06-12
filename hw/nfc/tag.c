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

#include "hw/nfc/debug.h"
#include "hw/nfc/nfc.h"
#include "hw/nfc/re.h"
#include "hw/nfc/tag.h"

#define T1T_UID { 0x01, 0x02, 0x03, 0x04, \
                  0x05, 0x06, 0x07, 0x08 }

#define T1T_RES { 0x00 }

/* [Type 1 Tag Operation Specification], 6.1.4 Capability Container */
static const uint8_t t1t_cc[4] = { 0xE1, 0x10, 0x0E, 0x00 };

#define T2T_INTERNAL { 0x04, 0x82, 0x2f, 0x21, \
                       0x5a, 0x53, 0x28, 0x80, \
                       0xa1, 0x48 }

/* 00 for read/write */
#define T2T_LOCK { 0x00, 0x00 }

/* [Type 2 Tag Operation Specification], 6.1 NDEF Management */
#define T2T_CC { 0xe1, 0x10, 0x12, 0x00 }

/* [Type 3 Tag Operation Specification] */
#define T3T_V { 0x10 }                    // Version
#define T3T_R { 0x04 }                    // Number of blocks can be read using one Check Command
#define T3T_W { 0x01 }                    // Number of blocks can be written using one Update Command
#define T3T_NB { 0x00, 0x0d }             // Maximum number of blocks available for NDEF data
#define T3T_U { 0x00, 0x00, 0x00, 0x00 }  // Unused
#define T3T_WF { 0x00 }                   // 00h:Writing data finished, 0Fh:Writing data in progress
#define T3T_RW { 0x01 }                   // 00h:Read only, 01h:Read/Write available
#define T3T_LN { 0x00, 0x00, 0x00 }       // Actual size of the stored NDEF data in bytes
#define T3T_CS { 0x00, 0x23 }             // Checksum: Byte0 + Byte1 + ... + Byte 13

static uint8_t NDEF_MESSAGE_TLV = 0x03;
static uint8_t NDEF_TERMINATOR_TLV = 0xFE;

struct nfc_tag nfc_tags[3] = {
   INIT_NFC_T1T([0], T1T_UID, T1T_RES),
   INIT_NFC_T2T([1], T2T_INTERNAL, T2T_LOCK, T2T_CC),
   INIT_NFC_T3T([2], T3T_V, T3T_R, T3T_W, T3T_NB, T3T_U, T3T_WF, T3T_RW, T3T_LN, T3T_CS)
};

static void
set_t1t_data(struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len)
{
    ssize_t offset = 0;
    uint8_t* data;

    assert(tag);
    assert(ndef_msg);
    assert(len < sizeof(tag->t.t1.format.data));

    data = tag->t.t1.format.data;

    /* [Type 1 Tag Operation Specificatio] 6.1 NDEF Management */
    memcpy(data + offset, t1t_cc, sizeof(t1t_cc));
    offset += sizeof(t1t_cc);

    data[offset++] = NDEF_MESSAGE_TLV;
    data[offset++] = len;

    memcpy(data + offset, ndef_msg, len);
    offset += len;

    data[offset++] = NDEF_TERMINATOR_TLV;
    memset(data + offset, 0, sizeof(tag->t.t1.format.data) - offset);
}

static void
set_t2t_data(struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len)
{
    ssize_t offset = 0;
    uint8_t* data;

    assert(tag);
    assert(ndef_msg);
    assert(len < sizeof(tag->t.t2.format.data));

    data = tag->t.t2.format.data;

    data[offset++] = NDEF_MESSAGE_TLV;
    data[offset++] = len;

    memcpy(data + offset, ndef_msg, len);
    offset += len;

    data[offset++] = NDEF_TERMINATOR_TLV;
    memset(data + offset, 0, sizeof(tag->t.t2.format.data) - offset);
}

static void
set_t3t_data(struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len)
{
    uint16_t cs = 0;
    uint8_t i;

    assert(tag);
    assert(ndef_msg);
    assert(len < sizeof(tag->t.t3.format.data));

    /* Re-calculate LN & Checksum */
    tag->t.t3.format.ln[0] = (len >> 16) & 0xff;
    tag->t.t3.format.ln[1] = (len >> 8) & 0xff;
    tag->t.t3.format.ln[2] = len & 0xff;

    for (i = 0; i < tag->t.t3.format.cs - &tag->t.t3.format.ver; i++) {
        cs += tag->t.t3.raw.mem[i];
    }

    tag->t.t3.format.cs[0] = (cs >> 8) & 0xff;
    tag->t.t3.format.cs[1] = cs & 0xff;

    /* Copy NDEF data, start from BLOCK one */
    memcpy(tag->t.t3.format.data , ndef_msg, len);
}

int
nfc_tag_set_data(struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len)
{
    switch (tag->type) {
        case T1T:
            set_t1t_data(tag, ndef_msg, len);
            break;
        case T2T:
            set_t2t_data(tag, ndef_msg, len);
            break;
        case T3T:
            set_t3t_data(tag, ndef_msg, len);
            break;
        default:
            assert(0);
            return -1;
    }

    return 0;
}

static size_t
process_t2t_read(const struct t2t_read_command* cmd, uint8_t* consumed,
                 uint8_t* mem, struct t2t_read_response* rsp)
{
    size_t i;
    size_t offset;
    size_t max_read;

    assert(cmd);
    assert(consumed);
    assert(mem);
    assert(rsp);

    offset = cmd->bno * 4;
    max_read = sizeof(rsp->payload);

    for (i = 0; i < max_read && offset < T2T_STATIC_MEMORY_SIZE; i++, offset++) {
        rsp->payload[i] = mem[offset];
    }
    rsp->status = 0;

    *consumed = sizeof(struct t2t_read_command);

    return sizeof(struct t2t_read_response);
}

static size_t
process_t1t_rid(struct nfc_tag* tag, const struct t1t_rid_command* cmd,
                uint8_t* consumed, struct t1t_rid_response* rsp)
{
    assert(tag);
    assert(cmd);
    assert(consumed);
    assert(rsp);

    rsp->hr[0] = T1T_HRO;
    rsp->hr[1] = T1T_HR1;

    memcpy(rsp->uid, tag->t.t1.format.uid, sizeof(rsp->uid));

    rsp->status = 0;

    *consumed = sizeof(struct t1t_rid_command);

    return sizeof(struct t1t_rid_response);
}

static size_t
process_t1t_rall(const struct t1t_rall_command* cmd, uint8_t* consumed,
                 uint8_t* mem, struct t1t_rall_response* rsp)
{
    size_t i;
    size_t offset;

    assert(cmd);
    assert(consumed);
    assert(rsp);

    offset = 0;

    rsp->payload[offset++] = T1T_HRO;
    rsp->payload[offset++] = T1T_HR1;

    for (i = 0; i < T1T_STATIC_MEMORY_SIZE ; i++) {
        rsp->payload[offset++] = mem[i];
    }

    rsp->status = 0;

    *consumed = sizeof(struct t1t_rall_command);

    return sizeof(struct t1t_rall_response);
}

size_t
process_t1t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp)
{
    assert(cmd);
    assert(rsp);

    switch (cmd->common.cmd) {
        case RALL_COMMAND:
            assert(re);
            assert(re->tag);
            len = process_t1t_rall(&cmd->rall_cmd, consumed,
                                   re->tag->t.t1.raw.mem, &rsp->rall_rsp);
            break;
        case RID_COMMAND:
            assert(re);
            len = process_t1t_rid(re->tag, &cmd->rid_cmd, consumed, &rsp->rid_rsp);
            break;
        default:
            assert(0);
            break;
    }

    return len;
}

size_t
process_t2t(struct nfc_re* re, const union command_packet* cmd,
            size_t len, uint8_t* consumed, union response_packet* rsp)
{
    assert(cmd);
    assert(rsp);

    switch (cmd->common.cmd) {
        case READ_COMMAND:
            assert(re);
            assert(re->tag);

            len = process_t2t_read(&cmd->read_cmd, consumed,
                                   re->tag->t.t2.raw.mem, &rsp->read_rsp);
            break;
        default:
            assert(0);
            break;
    }

    return len;
}
