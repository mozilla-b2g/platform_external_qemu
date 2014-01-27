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
#include <string.h>
#include "goldfish_nfc.h"
#include "nfc-debug.h"
#include "nfc.h"
#include "nfc-nci.h"
#include "llcp.h"
#include "nfc-re.h"

struct nfc_re nfc_res[2] = {
    INIT_NFC_RE([0], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, "deadbeaf0"),
    INIT_NFC_RE([1], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, "deadbeaf1")
};

struct nfc_re*
nfc_get_re_by_id(uint8_t id)
{
    struct nfc_re* pos;
    const struct nfc_re* end;

    assert(id);
    assert(id < 255);

    pos = nfc_res;
    end = nfc_res + sizeof(nfc_res)/sizeof(nfc_res[0]);

    while (pos < end) {
        if (pos->id == id) {
            return pos;
        }
        ++pos;
    }

    return NULL;
}

size_t
nfc_re_write(struct nfc_re* re, size_t len, const void* data)
{
    size_t newsiz;

    assert(re);

    newsiz = re->wdtasiz + len;

    if (newsiz > re->wdtasiz) {
        return 0; /* not enough memory */
    }

    memcpy(re->wdta+re->wdtasiz, data, len);
    re->wdtasiz = newsiz;

    return len;
}

static size_t
create_symm_dta(void* data, struct nfc_device* nfc, union nci_packet* dta)
{
    struct nfc_re* re;
    size_t len;

    assert(data);

    re = data;

    len = llcp_create_packet((struct llcp_packet*)dta->data.payload,
                             0, LLCP_PTYPE_SYMM, 0);

    return nfc_create_nci_dta(dta, NCI_PBF_END, re->connid, len);
}

static void
send_symm_cb(void* opaque)
{
    goldfish_nfc_send_dta(create_symm_dta, opaque);
}

static size_t
process_ptype_symm(struct nfc_re* re, const struct llcp_packet* llcp,
                   size_t len, uint8_t* consumed, struct llcp_packet* rsp)
{
    assert(re);
    assert(llcp);
    assert(consumed);
    assert(rsp);

    if (!re->send_symm) {
        len = llcp_create_packet(rsp, llcp->dsap, LLCP_PTYPE_SYMM,
                                 llcp->ssap);
    } else {
        len = 0;
    }

    re->send_symm = !re->send_symm;

    *consumed = sizeof(*llcp);

    if (re->send_symm) {
        /* prepare timer for llcp symm */
        if (!re->send_symm_timer) {
            re->send_symm_timer =
                qemu_new_timer_ms(vm_clock, send_symm_cb, re);
            assert(re->send_symm_timer);
        }
        /* send symm in one second */
        qemu_mod_timer(re->send_symm_timer,
                       qemu_get_clock_ms(vm_clock)+2000);
    }

    return len;
}

static size_t
process_ptype_connect(struct nfc_re* re, const struct llcp_packet* llcp,
                      size_t len, uint8_t* consumed,
                      struct llcp_packet* rsp)
{
    struct llcp_connection_state* cs;
    const uint8_t* opt;

    assert(re);
    assert(llcp);
    assert(consumed);
    assert(rsp);

    cs = llcp_init_connection_state(re->llcp_cs[llcp->ssap] + llcp->dsap);

    *consumed = sizeof(*llcp);
    len -= *consumed;

    opt = ((const uint8_t*)llcp) + *consumed;

    while (len >= 2) {
        len -= 2;
        switch (opt[0]) {
            case LLCP_PARAM_MIUX:
                if ((opt[1] != 2) || (len < opt[1])) {
                    continue;
                }
                {
                  const struct llcp_param_miux* param =
                      (const struct llcp_param_miux*)(opt + 2);
                  cs->miu = param->miux - 128;
                }
                NFC_D("LLCP MIU size=%d", cs->miu);
                break;
            case LLCP_PARAM_RW:
                if ((opt[1] != 1) || (len < opt[1])) {
                    continue;
                }
                {
                  const struct llcp_param_rw* param =
                      (const struct llcp_param_rw*)(opt + 2);
                  cs->rw_r = param->rw;
                }
                NFC_D("LLCP remote RW size %d", cs->rw_r);
                break;
            case LLCP_PARAM_SN:
                if (len < opt[1]) {
                    continue;
                }
                NFC_D("requesting LLCP service %.*s\n", opt[1], (const char*)opt+2);
                break;
            default:
                NFC_D("Ignoring unknown LLCP parameter %d\n", opt[0]);
                break;
        }
    }

    /* switch DSAP and SSAP in outgoing packet */
    return llcp_create_packet(rsp, llcp->ssap, LLCP_PTYPE_CC, llcp->dsap);
}

static size_t
process_ptype_disc(struct nfc_re* re, const struct llcp_packet* llcp,
                   size_t len, uint8_t* consumed,
                   struct llcp_packet* rsp)
{
  /* Theres nothing to do on disconnects. We
     just have to handle the PDU. */
  return 0;
}

static size_t
process_llcp(struct nfc_re* re, const struct llcp_packet* llcp,
             size_t len, uint8_t* consumed, struct llcp_packet* rsp)
{
    static size_t (* const process[16])
        (struct nfc_re*, const struct llcp_packet*,
         size_t, uint8_t*, struct llcp_packet*) = {
        [LLCP_PTYPE_SYMM] = process_ptype_symm,
        [LLCP_PTYPE_CONNECT] = process_ptype_connect,
        [LLCP_PTYPE_DISC] = process_ptype_disc
    };

    unsigned char ptype = llcp_ptype(llcp);

    NFC_D("LLCP dsap=%x ptype=%x ssap=%x", llcp->dsap, ptype, llcp->ssap);

    assert(process[ptype]);

    return process[ptype](re, llcp, len, consumed, rsp);
}

size_t
nfc_re_process_data(struct nfc_re* re, const union nci_packet* dta,
                    union nci_packet* rsp)
{
    size_t len;
    uint8_t off;

    assert(re);
    assert(dta);
    assert(rsp);

    /* consume llcp */

    re->connid = dta->data.connid;

    switch (re->rfproto) {
        case NCI_RF_PROTOCOL_NFC_DEP:
            len = process_llcp(re,
                (const struct llcp_packet*)dta->data.payload,
                dta->data.l, &off, (struct llcp_packet*)rsp->data.payload);
            if (len) {
                len = nfc_create_nci_dta(rsp, NCI_PBF_END, re->connid, len);
            }
            break;
        default:
            assert(0); /* TODO: support other RF protocols */
            len = 0;
            off = 0;
            break;
    }

    /* payload gets stored in RE buffer */
    nfc_re_write(re, dta->data.l-off, dta->data.payload+off);

    return len;
}

enum {
    NFC_DEP_REQ = 0xd4,
    NFC_DEP_RES = 0xd5,
    NFC_DEP_PP_G = 0x02
};

size_t
nfc_re_create_rf_intf_activated_ntf_act(struct nfc_re* re, uint8_t* act)
{
    uint8_t* p;

    assert(re);

    p = act+1;

    /* [DIGITAL], Sec 14.6 */
    /* ATR_REQ/ATR_RES command */
    memcpy(p, re->nfcid3, sizeof(re->nfcid3));
    p += 10;
    *p++ = 0; /* DID */
    *p++ = 0; /* BS */
    *p++ = 0; /* BR */

    switch (re->mode) {
        case NCI_RF_NFC_A_PASSIVE_LISTEN_MODE:
        case NCI_RF_NFC_B_PASSIVE_LISTEN_MODE:
        case NCI_RF_NFC_F_PASSIVE_LISTEN_MODE:
            *p++ = 14; /* TO, set to WTmax */
            break;
        default:
            break;
    }

    *p++ = NFC_DEP_PP_G; /* PP */

    /* LLCP */
    p += llcp_create_param_tail(p);

    /* ATR_REQ length */
    act[0] = (p-act)-1;

    return p-act;
}

size_t
nfc_re_create_dta_act(struct nfc_re* re, const void* data,
                      size_t len, uint8_t* act)
{
  size_t llcp_len;

  assert(act);

  llcp_len = llcp_create_packet((struct llcp_packet*)act, 0, LLCP_PTYPE_UI, 0);

  memcpy(act+len, data, len);

  return llcp_len + len;
}
