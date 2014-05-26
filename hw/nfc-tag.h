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

enum nfc_tag_type {
    T1T = 0,
    T2T,
    T3T,
    T4T,
};

enum {
    MAXIMUM_SUPPORTED_TAG_SIZE = 1024
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
} __attribute__((packed));;

union nfc_t2t {
    struct nfc_t2t_raw raw;
    struct nfc_t2t_format format;
};

struct nfc_tag {
    enum nfc_tag_type type;
    union {
        union nfc_t2t t2;
    };
};

#define INIT_NFC_T2T(tag_, internal_, lock_, cc_) \
    tag_ = { \
        .type = T2T, \
        .t2.format.internal = internal_, \
        .t2.format.lock = lock_, \
        .t2.format.cc = cc_ \
    }

extern struct nfc_tag nfc_tags[1];

int
nfc_tag_set_data(const struct nfc_tag* tag, const uint8_t* ndef_msg, ssize_t len);

#endif
