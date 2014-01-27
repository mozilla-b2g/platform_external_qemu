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
#include "llcp.h"

/* magic numbers for bcm2079x */
enum {
    LLCP_HEADER0 = 0x46,
    LLCP_HEADER1 = 0x66,
    LLCP_HEADER2 = 0x6d
};

size_t
llcp_create_packet(struct llcp_packet* llcp, unsigned char dsap,
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

unsigned char
llcp_ptype(const struct llcp_packet* llcp)
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

struct llcp_connection_state*
llcp_init_connection_state(struct llcp_connection_state* cs)
{
    assert(cs);

    cs->v_s = 0;
    cs->v_sa = 0;
    cs->v_r = 0;
    cs->v_ra = 0;
    cs->miu = 128;
    cs->rw_l = 1;
    cs->rw_r = 1;

    return cs;
}
