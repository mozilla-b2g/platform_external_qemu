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

#ifndef hw_ndef_h
#define hw_ndef_h

#include <stddef.h>
#include <stdint.h>

enum {
    NDEF_FLAG_MB = 0x80,
    NDEF_FLAG_ME = 0x40,
    NDEF_FLAG_CF = 0x20,
    NDEF_FLAG_SR = 0x10,
    NDEF_FLAG_IL = 0x08
};

#define NDEF_FLAG_BITS \
    (NDEF_FLAG_MB | \
     NDEF_FLAG_ME | \
     NDEF_FLAG_CF | \
     NDEF_FLAG_SR | \
     NDEF_FLAG_IL)

enum ndef_tnf {
    NDEF_TNF_EMPTY = 0x0,
    NDEF_TNF_WELL_KNOWN = 0x1,
    NDEF_TNF_MIME = 0x2,
    NDEF_TNF_URI = 0x3,
    NDEF_TNF_EXTERNAL = 0x4,
    NDEF_TNF_UNKNOWN = 0x5,
    NDEF_TNF_UNCHANGED = 0x6,
    NDEF_TNF_RESERVED = 0x7,
    NDEF_NUMBER_OF_TNFS
};

#define NDEF_TNF_BITS 0x07

struct ndef_rec {
    uint8_t flags;
    uint8_t data[0]; /* contains remaining header and payload */
} __attribute__((packed));

struct ndef_rec_fields {
    uint8_t tlen; /* type length */
    uint32_t plen; /* payload length; big endian */
    uint8_t ilen; /* id length; depends on IL flag */
} __attribute__((packed));

struct ndef_srec_fields {
    uint8_t tlen; /* type length */
    uint8_t plen; /* payload length */
    uint8_t ilen; /* id length; depends on IL flag */
} __attribute__((packed));

size_t
ndef_create_rec(struct ndef_rec* rec, uint8_t flags, enum ndef_tnf tnf,
                uint8_t tlen, uint32_t plen, uint8_t ilen);

size_t
ndef_rec_len(const struct ndef_rec* rec);

/*
 * Type lookup
 */

uint8_t
ndef_rec_type_len(const struct ndef_rec* ndef);

void
ndef_rec_set_type_len(struct ndef_rec* ndef, uint8_t tlen);

size_t
ndef_rec_type_off(const struct ndef_rec* ndef);

const uint8_t*
ndef_rec_const_type(const struct ndef_rec* ndef);

uint8_t*
ndef_rec_type(struct ndef_rec* ndef);

/*
 * Payload lookup
 */

uint32_t
ndef_rec_payload_len(const struct ndef_rec* ndef);

void
ndef_rec_set_payload_len(struct ndef_rec* ndef, uint8_t plen);

size_t
ndef_rec_payload_off(const struct ndef_rec* ndef);

const uint8_t*
ndef_rec_const_payload(const struct ndef_rec* ndef);

uint8_t*
ndef_rec_payload(struct ndef_rec* ndef);

/*
 * ID lookup
 */

uint8_t
ndef_rec_id_len(const struct ndef_rec* ndef);

void
ndef_rec_set_id_len(struct ndef_rec* ndef, uint8_t ilen);

size_t
ndef_rec_id_off(const struct ndef_rec* ndef);

const uint8_t*
ndef_rec_const_id(const struct ndef_rec* ndef);

uint8_t*
ndef_rec_id(struct ndef_rec* ndef);

#endif
