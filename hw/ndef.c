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

#include <assert.h>
#include "bswap.h"
#include "ndef.h"

static size_t
ndef_hdr_len(const struct ndef_rec* ndef)
{
    return sizeof(*ndef) + 2 + /* flags/tlen/plen bytes */
           (!(ndef->flags&NDEF_FLAG_SR) * 3) + /* 3 extra bytes for long plen */
           !!(ndef->flags&NDEF_FLAG_IL); /* 1 extra byte for ilen */
}

size_t
ndef_create_rec(struct ndef_rec* ndef, uint8_t flags, enum ndef_tnf tnf,
                uint8_t tlen, uint32_t plen, uint8_t ilen)
{
    assert(ndef);
    assert(!(flags&0x07)); /* no flags in TNF bits */
    assert(!(tnf&0xf8)); /* no TNF in flag bits */

    ndef->flags = flags | tnf;

    if (flags & NDEF_FLAG_SR) {
        struct ndef_rec_fields* rf = (struct ndef_rec_fields*)ndef->data;
        rf->tlen = tlen;
        rf->plen = cpu_to_be32(plen);
        if (flags & NDEF_FLAG_IL) {
            rf->ilen = ilen;
        }
    } else {
        struct ndef_srec_fields* srf = (struct ndef_srec_fields*)ndef->data;
        srf->tlen = tlen;
        srf->plen = plen;
        if (flags & NDEF_FLAG_IL) {
            srf->ilen = ilen;
        }
    }
    return ndef_hdr_len(ndef);
}

size_t
ndef_rec_len(const struct ndef_rec* ndef)
{
    return ndef_hdr_len(ndef) +
           ndef_rec_type_len(ndef) +
           ndef_rec_payload_len(ndef) +
           ndef_rec_id_len(ndef);
}

/*
 * Type lookup
 */

uint8_t
ndef_rec_type_len(const struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data[0];
}

void
ndef_rec_set_type_len(struct ndef_rec* ndef, uint8_t tlen)
{
    assert(ndef);
    ndef->data[0] = tlen;
}

size_t
ndef_rec_type_off(const struct ndef_rec* ndef)
{
    assert(ndef);

    return sizeof(struct ndef_rec_fields) - /* start with regular records */
           (!!(ndef->flags & NDEF_FLAG_SR)) * 3 - /* -3 for short records */
             !(ndef->flags & NDEF_FLAG_IL); /* -1 if ilen not present */
}

const uint8_t*
ndef_rec_const_type(const struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_type_off(ndef);
}

uint8_t*
ndef_rec_type(struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_type_off(ndef);
}

/*
 * Payload lookup
 */

uint32_t
ndef_rec_payload_len(const struct ndef_rec* ndef)
{
    uint32_t plen;

    assert(ndef);

    if (ndef->flags & NDEF_FLAG_SR) {
        const struct ndef_srec_fields* srf =
            (const struct ndef_srec_fields*)(ndef->data);
        plen = srf->plen;
    } else {
        const struct ndef_rec_fields* rf =
            (const struct ndef_rec_fields*)(ndef->data);
        plen = be32_to_cpu(rf->plen);

    }
    return plen;
}

void
ndef_rec_set_payload_len(struct ndef_rec* ndef, uint8_t plen)
{
    assert(ndef);

    if (ndef->flags & NDEF_FLAG_SR) {
        struct ndef_srec_fields* srf = (struct ndef_srec_fields*)(ndef->data);
        srf->plen = plen;
    } else {
        struct ndef_rec_fields* rf = (struct ndef_rec_fields*)(ndef->data);
        rf->plen = cpu_to_be32(plen);

    }
}

size_t
ndef_rec_payload_off(const struct ndef_rec* ndef)
{
    return ndef_rec_type_off(ndef) + ndef_rec_type_len(ndef);
}

const uint8_t*
ndef_rec_const_payload(const struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_payload_off(ndef);
}

uint8_t*
ndef_rec_payload(struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_payload_off(ndef);
}

/*
 * ID lookup
 */

uint8_t
ndef_rec_id_len(const struct ndef_rec* ndef)
{
    uint8_t ilen;

    assert(ndef);

    if (!(ndef->flags & NDEF_FLAG_IL)) {
        /* no IL flags, no ilen */
        ilen = 0;
    } else if (ndef->flags & NDEF_FLAG_SR) {
        const struct ndef_srec_fields* srf =
            (const struct ndef_srec_fields*)(ndef->data);
        ilen = srf->ilen;
    } else {
        const struct ndef_rec_fields* rf =
            (const struct ndef_rec_fields*)(ndef->data);
        ilen = rf->ilen;

    }
    return ilen;
}

void
ndef_rec_set_id_len(struct ndef_rec* ndef, uint8_t ilen)
{
    assert(ndef);
    assert(ndef->flags & NDEF_FLAG_IL);

    if (ndef->flags & NDEF_FLAG_SR) {
        struct ndef_srec_fields* srf = (struct ndef_srec_fields*)(ndef->data);
        srf->ilen = ilen;
    } else {
        struct ndef_rec_fields* rf = (struct ndef_rec_fields*)(ndef->data);
        rf->ilen = ilen;
    }
}

size_t
ndef_rec_id_off(const struct ndef_rec* ndef)
{
    return ndef_rec_payload_off(ndef) + ndef_rec_payload_len(ndef);
}

const uint8_t*
ndef_rec_const_id(const struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_id_off(ndef);
}

uint8_t*
ndef_rec_id(struct ndef_rec* ndef)
{
    assert(ndef);
    return ndef->data + ndef_rec_id_off(ndef);
}
