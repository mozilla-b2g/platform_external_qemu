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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "nfc-debug.h"
#include "llcp.h"

/* magic numbers for bcm2079x */
enum {
    LLCP_HEADER0 = 0x46,
    LLCP_HEADER1 = 0x66,
    LLCP_HEADER2 = 0x6d
};

size_t
llcp_create_pdu(struct llcp_pdu* llcp, unsigned char dsap,
                unsigned char ptype, unsigned char ssap)
{
    assert(llcp);

    llcp->dsap = dsap;
#if BITORDER_MSB_FIRST
    llcp->ptype = ptype;
#else
    llcp->ptype0 = ptype>>2;
    llcp->ptype1 = ptype&0x3;
#endif
    llcp->ssap = ssap;

    return sizeof(*llcp);
}

size_t
llcp_create_pdu_dm(struct llcp_pdu* llcp, unsigned char dsap,
                   unsigned char ssap, unsigned char reason)
{
  assert(llcp);

  llcp->info[0] = reason;

  return llcp_create_pdu(llcp, dsap, LLCP_PTYPE_DM, ssap) + 1;
}

size_t
llcp_create_pdu_i(struct llcp_pdu* llcp, unsigned char dsap,
                  unsigned char ssap, unsigned char ns, unsigned char nr)
{
  assert(llcp);

  llcp->info[0] = ((ns&0x0f) << 4) | (nr&0x0f);

  return llcp_create_pdu(llcp, dsap, LLCP_PTYPE_I, ssap) + 1;
}

unsigned char
llcp_ptype(const struct llcp_pdu* llcp)
{
    assert(llcp);

#if BITORDER_MSB_FIRST
    return llcp->ptype;
#else
    return (llcp->ptype0 << 2) | llcp->ptype1;
#endif
}

size_t
llcp_create_param_tail(uint8_t* p)
{
    const uint8_t *beg = p;

    assert(p);

    /* header bytes for bcm2079x */
    *p++ = LLCP_HEADER0;
    *p++ = LLCP_HEADER1;
    *p++ = LLCP_HEADER2;

    /* version */
    *p++ = LLCP_PARAM_VERSION;
    *p++ = 1;
    *p++ = (LLCP_VERSION_MAJOR<<4)|LLCP_VERSION_MINOR;

    /* SYMM timeout */
    *p++ = LLCP_PARAM_LTO;
    *p++ = 1;
    *p++ = 250;

    return p-beg;
}

/*
 * LLCP PDU handling
 */

struct llcp_pdu_buf*
llcp_alloc_pdu_buf(void)
{
    struct llcp_pdu_buf* buf;

    buf = malloc(sizeof(*buf));
    if (!buf) {
        NFC_D("malloc failed: %d (%s)", errno, strerror(errno));
        return NULL;
    }
    buf->entry.tqe_next = NULL;
    buf->entry.tqe_prev = NULL;
    buf->len = 0;
    return buf;
}

void
llcp_free_pdu_buf(struct llcp_pdu_buf* buf)
{
    free(buf);
}

/*
 * Data links
 */

struct llcp_data_link*
llcp_clear_data_link(struct llcp_data_link* dl)
{
    assert(dl);

    dl->v_s = 0;
    dl->v_sa = 0;
    dl->v_r = 0;
    dl->v_ra = 0;
    dl->miu = 128;
    dl->rw_l = 1;
    dl->rw_r = 1;
    dl->rlen = 0;

    return dl;
}

struct llcp_data_link*
llcp_init_data_link(struct llcp_data_link* dl)
{
    assert(dl);

    dl->status = LLCP_DATA_LINK_DISCONNECTED;
    QTAILQ_INIT(&dl->xmit_q);

    return llcp_clear_data_link(dl);
}

size_t
llcp_dl_write_rbuf(struct llcp_data_link* dl, size_t len, const void* data)
{
    assert(dl);
    assert(len < sizeof(dl->rbuf));
    assert(data || !len);

    dl->rlen = len;
    memcpy(dl->rbuf, data, len);

    return dl->rlen;
}

size_t
llcp_dl_read_rbuf(const struct llcp_data_link* dl, size_t len, void* data)
{
    assert(dl);
    assert(len < sizeof(dl->rbuf));

    len = len < dl->rlen ? len : dl->rlen;
    memcpy(data, dl->rbuf, len);

    return len;
}
