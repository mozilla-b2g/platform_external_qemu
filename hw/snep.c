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
#include "snep.h"

size_t
snep_create_msg(struct snep* snep, enum snep_msg msg, uint32_t len)
{
    assert(snep);

    snep->ver.major = SNEP_VERSION_MAJOR;
    snep->ver.minor = SNEP_VERSION_MINOR;
    snep->msg = msg;
    snep->len = cpu_to_be32(len);

    return sizeof(*snep) + len;
}

size_t
snep_create_req_put(struct snep* snep, uint32_t len)
{
    return snep_create_msg(snep, SNEP_REQ_PUT, len);
}

size_t
snep_create_rsp_success(struct snep* snep, uint32_t len)
{
    return snep_create_msg(snep, SNEP_RSP_SUCCESS, len);
}

size_t
snep_create_rsp_excess_data(struct snep* snep)
{
    return snep_create_msg(snep, SNEP_RSP_EXCESS_DATA, 0);
}

size_t
snep_create_rsp_bad_request(struct snep* snep)
{
    return snep_create_msg(snep, SNEP_RSP_BAD_REQUEST, 0);
}

size_t
snep_create_rsp_not_implemented(struct snep* snep)
{
    return snep_create_msg(snep, SNEP_RSP_NOT_IMPLEMENTED, 0);
}

size_t
snep_create_rsp_unsupported_version(struct snep* snep)
{
    return snep_create_msg(snep, SNEP_RSP_UNSUPPORTED_VERSION, 0);
}
