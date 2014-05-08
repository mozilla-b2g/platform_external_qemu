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
#include <limits.h>
#include "osdep.h"
#include "bswap.h"
#include "nfc-debug.h"
#include "llcp.h"
#include "snep.h"
#include "llcp-snep.h"

static size_t
process_req_put(const struct snep* snep, struct llcp_data_link* dl,
                      struct snep* rsp)
{
    uint32_t sneplen;

    NFC_D("SNEP Put");

    sneplen = be32_to_cpu(snep->len);

    if (!(sneplen < sizeof(dl->rbuf))) {
        NFC_D("SNEP responding 'Excess Data'");
        return snep_create_rsp_excess_data(rsp);
    }

    /* copy information field into dl->rbuf */
    llcp_dl_write_rbuf(dl, sneplen, snep->info);

    return snep_create_rsp_success(rsp, 0);
}

static size_t
process_rsp_success(const struct snep* snep, struct llcp_data_link* dl,
                          struct snep* rsp)
{
    NFC_D("SNEP Success");

    return 0;
}

static int
version_is_supported(unsigned char major, unsigned char minor)
{
    return ((major << 4) | minor) <=
           ((SNEP_VERSION_MAJOR << 4) | SNEP_VERSION_MINOR);
}

static size_t
process_msg(const struct snep* snep, struct llcp_data_link* dl,
                  struct snep* rsp)
{
    static size_t (*const process[])(const struct snep*,
                                           struct llcp_data_link*,
                                           struct snep*) = {
        [SNEP_REQ_PUT]     = process_req_put,
        [SNEP_RSP_SUCCESS] = process_rsp_success
    };

    if (!version_is_supported(snep->ver.major, snep->ver.minor)) {
        NFC_D("SNEP responding 'Unsupported Version'");
        return snep_create_rsp_unsupported_version(rsp);
    }
    if ((snep->msg >= ARRAY_SIZE(process)) || !process[snep->msg]) {
        NFC_D("SNEP responding 'Not Implemented'");
        return snep_create_rsp_not_implemented(rsp);
    }
    return process[snep->msg](snep, dl, rsp);
}

size_t
llcp_sap_snep(struct llcp_data_link* dl, const uint8_t* info, size_t len,
              struct snep* rsp)
{
    const struct snep* snep;
    uint32_t sneplen;

    assert(dl);
    assert(info);
    assert(len >= 6);
    assert(rsp);

    if (len < sizeof(*snep)) {
        NFC_D("SNEP responding 'Bad Request'");
        return snep_create_rsp_bad_request(rsp);
    }

    snep = (struct snep*)info;
    sneplen = be32_to_cpu(snep->len);

    if (!(sneplen < (UINT32_MAX-sizeof(*snep))) ||
         (sneplen+sizeof(*snep) != len)) {
        NFC_D("SNEP responding 'Excess Data'");
        return snep_create_rsp_excess_data(rsp);
    }
    return process_msg(snep, dl, rsp);
}
