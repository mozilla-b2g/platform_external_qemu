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
#include "hw/android/goldfish/nfc.h"
#include "hw/nfc/debug.h"
#include "hw/nfc/nfc.h"
#include "hw/nfc/nci.h"
#include "hw/nfc/llcp.h"
#include "hw/nfc/re.h"

struct nfc_re nfc_res[3] = {
    INIT_NFC_RE([0], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, "deadbeaf0", nfc_res+0),
    INIT_NFC_RE([1], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, "deadbeaf1", nfc_res+1),
    INIT_NFC_RE([2], NCI_RF_PROTOCOL_T2T, NCI_RF_NFC_A_PASSIVE_LISTEN_MODE, "deadbeaf2", nfc_res+2)
};

struct create_nci_dta_param {
    ssize_t (*create)(void*, struct llcp_pdu*);
    void* data;
    struct nfc_re* re;
};

#define CREATE_NCI_DTA_PARAM_INIT(_create, _data, _re) \
    { \
        .create = (_create), \
        .data = (_data), \
        .re = (_re) \
    }

/* helper function wrap an LLCP PDU in an NCI packet */
static ssize_t
create_nci_dta(void* data, struct nfc_device* nfc, size_t maxlen,
               union nci_packet* dta)
{
    const struct create_nci_dta_param* param;
    ssize_t len;

    param = data;
    assert(param);

    len = param->create(param->data, (struct llcp_pdu*)dta->data.payload);
    return nfc_create_nci_dta(dta, NCI_PBF_END, param->re->connid, len);
}

/* Sends an LLCP PDU from the RE to the guest. Sending
 * means that the PDU is either generated and transmitted
 * directly or enqueued for later transmission.
 */
static int
send_pdu_from_re(ssize_t (*create)(void*, struct llcp_pdu*),
                 void* data, struct nfc_re* re)
{
    if (re->xmit_next) {
        /* it's our turn to xmit the next PDU; we do this
         * immediately and cancel the possible timer for the
         * SYMM PDU */
        struct create_nci_dta_param param =
            CREATE_NCI_DTA_PARAM_INIT(create, data, re);

        goldfish_nfc_send_dta(create_nci_dta, &param);
        re->xmit_next = 0;
        if (re->xmit_timer) {
            timer_del(re->xmit_timer);
        }
    } else {
        /* we're waiting for the host to send a SYMM PDU, so
         * we queue up PDUs for later delivery */
        struct llcp_pdu_buf* buf = llcp_alloc_pdu_buf();
        if (!buf) {
            return -1;
        }
        buf->len = create(data, (struct llcp_pdu*)buf->pdu);
        QTAILQ_INSERT_TAIL(&re->xmit_q, buf, entry);
    }
    return 0;
}

/* Fetches the next PDU from the RE's xmit queue and returns the PDU's
 * length. 0 is returned if the queue is empty.
 */
static ssize_t
fetch_pdu_from_re(struct llcp_pdu* llcp, struct nfc_re* re)
{
    ssize_t len;

    assert(llcp);
    assert(re);

    if (QTAILQ_EMPTY(&re->xmit_q)) {
        return 0;
    }

    struct llcp_pdu_buf* buf;
    buf = QTAILQ_FIRST(&re->xmit_q);
    len = buf->len;
    memcpy(llcp, buf->pdu, len);
    QTAILQ_REMOVE(&re->xmit_q, buf, entry);
    llcp_free_pdu_buf(buf);

    return len;
}

/* Transmits a PDU from the RE to the guest. If there is
 * no queued PDU, a SYMM PDU is generated to fulfill LLCP
 * requirements.
 */
static ssize_t
xmit_pdu_or_symm_from_re(struct llcp_pdu* llcp, struct nfc_re* re)
{
    ssize_t len;

    assert(re);

    /* either xmit a queued PDU or... */
    len = fetch_pdu_from_re(llcp, re);
    if (!len) {
        /* ...xmit a new SYMM PDU */
        len = llcp_create_pdu(llcp, LLCP_SAP_LM, LLCP_PTYPE_SYMM, LLCP_SAP_LM);
    }
    re->xmit_next = 0;

    return len;
}

/* When we're in charge of sending, we need to xmit something
 * before the link timeout expires.
 */
static void
prepare_xmit_timer(struct nfc_re* re, void (*xmit_next_cb)(void*))
{
    if (!re->xmit_timer) {
        re->xmit_timer = timer_new(QEMU_CLOCK_VIRTUAL, SCALE_MS, xmit_next_cb, re);
        assert(re->xmit_timer);
    }
    if (!timer_pending(re->xmit_timer)) {
        /* xmit PDU in two seconds */
        timer_mod(re->xmit_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)+2000);
    }
}

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

static ssize_t
write_buf(size_t* bufsiz, uint8_t* buf, size_t len, const void* data)
{
    assert(bufsiz);
    assert(buf || !*bufsiz);
    assert(data || !len);

    if (len > *bufsiz) {
        return -1; /* not enough memory */
    }

    memcpy(buf, data, len);
    *bufsiz += len;

    return len;
}

static ssize_t
read_buf(size_t* bufsiz, const uint8_t* buf, size_t len, void* data)
{
    assert(bufsiz);
    assert(buf || !*bufsiz);
    assert(data || !len);

    if (len < *bufsiz) {
        return -1; /* not enough memory */
    }

    len = *bufsiz;
    memcpy(data, buf, len);
    *bufsiz = 0;

    return len;
}

ssize_t
nfc_re_write_sbuf(struct nfc_re* re, size_t len, const void* data)
{
    assert(re);
    return write_buf(&re->sbufsiz, re->sbuf, len, data);
}

ssize_t
nfc_re_read_sbuf(struct nfc_re* re, size_t len, void* data)
{
    assert(re);
    return read_buf(&re->sbufsiz, re->sbuf, len, data);
}

ssize_t
nfc_re_write_rbuf(struct nfc_re* re, size_t len, const void* data)
{
    assert(re);
    return write_buf(&re->rbufsiz, re->rbuf, len, data);
}

ssize_t
nfc_re_read_rbuf(struct nfc_re* re, size_t len, void* data)
{
    assert(re);
    return read_buf(&re->rbufsiz, re->rbuf, len, data);
}

static ssize_t
create_dta(void* data, struct nfc_device* nfc, size_t maxlen,
           union nci_packet* dta)
{
    struct nfc_re* re;
    ssize_t len;

    re = data;
    assert(re);

    len = xmit_pdu_or_symm_from_re((struct llcp_pdu*)dta->data.payload, re);
    return nfc_create_nci_dta(dta, NCI_PBF_END, re->connid, len);
}

static void
xmit_next_cb(void* opaque)
{
    goldfish_nfc_send_dta(create_dta, opaque);
}

static size_t
process_ptype_symm(struct nfc_re* re, const struct llcp_pdu* llcp,
                   size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    assert(re);
    assert(llcp);
    assert(consumed);
    assert(rsp);

    *consumed = sizeof(*llcp);

    return 0;
}

static size_t
process_ptype_connect(struct nfc_re* re, const struct llcp_pdu* llcp,
                      size_t len, uint8_t* consumed,
                      struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;
    const uint8_t* opt;

    assert(re);
    assert(llcp);
    assert(consumed);
    assert(rsp);

    dl = llcp_init_data_link(re->llcp_dl[llcp->ssap] + llcp->dsap);
    dl->status = LLCP_DATA_LINK_CONNECTED;

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
                  dl->miu = param->miux - 128;
                }
                NFC_D("LLCP MIU size=%d", dl->miu);
                break;
            case LLCP_PARAM_RW:
                if ((opt[1] != 1) || (len < opt[1])) {
                    continue;
                }
                {
                  const struct llcp_param_rw* param =
                      (const struct llcp_param_rw*)(opt + 2);
                  dl->rw_r = param->rw;
                }
                NFC_D("LLCP remote RW size %d", dl->rw_r);
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

    /* switch DSAP and SSAP in outgoing PDU */
    return llcp_create_pdu(rsp, llcp->ssap, LLCP_PTYPE_CC, llcp->dsap);
}

static size_t
process_ptype_disc(struct nfc_re* re, const struct llcp_pdu* llcp,
                   size_t len, uint8_t* consumed,
                   struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;

    dl = re->llcp_dl[llcp->ssap] + llcp->dsap;
    dl->status = LLCP_DATA_LINK_DISCONNECTED;

    *consumed = sizeof(*llcp);

    /* switch DSAP and SSAP in outgoing PDU */
    return llcp_create_pdu_dm(rsp, llcp->ssap, llcp->dsap, 0);
}

static size_t
process_ptype_cc(struct nfc_re* re, const struct llcp_pdu* llcp,
                 size_t len, uint8_t* consumed,
                 struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;

    dl = llcp_clear_data_link(re->llcp_dl[llcp->ssap] + llcp->dsap);
    assert(dl->status == LLCP_DATA_LINK_CONNECTING);
    dl->status = LLCP_DATA_LINK_CONNECTED;

    *consumed = sizeof(*llcp) + 1;

    return 0;
}

static size_t
process_ptype_dm(struct nfc_re* re, const struct llcp_pdu* llcp,
                 size_t len, uint8_t* consumed,
                 struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;

    NFC_D("LLCP DM, reason=%d\n", llcp->info[0]);

    dl = re->llcp_dl[llcp->ssap] + llcp->dsap;
    dl->status = LLCP_DATA_LINK_DISCONNECTED;

    /* consume 1 extra byte for 'reason' field */
    *consumed = sizeof(*llcp) + 1;

    return 0;
}

static size_t
process_llcp(struct nfc_re* re, const struct llcp_pdu* llcp,
             size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    static size_t (* const process[16])
        (struct nfc_re*, const struct llcp_pdu*,
         size_t, uint8_t*, struct llcp_pdu*) = {
        [LLCP_PTYPE_SYMM] = process_ptype_symm,
        [LLCP_PTYPE_CONNECT] = process_ptype_connect,
        [LLCP_PTYPE_DISC] = process_ptype_disc,
        [LLCP_PTYPE_CC] = process_ptype_cc,
        [LLCP_PTYPE_DM] = process_ptype_dm,
    };

    unsigned char ptype;

    ptype = llcp_ptype(llcp);

    NFC_D("LLCP dsap=%x ptype=%x ssap=%x", llcp->dsap, ptype, llcp->ssap);

    assert(process[ptype]);

    len = process[ptype](re, llcp, len, consumed, rsp);

    /* we implicitely received send permission */
    re->xmit_next = 1;

    /* prepare timer for LLCP SYMM */
    prepare_xmit_timer(re, xmit_next_cb);

    return len;
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
                (const struct llcp_pdu*)dta->data.payload,
                dta->data.l, &off, (struct llcp_pdu*)rsp->data.payload);
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

    /* payload gets stored in RE send buffer */
    nfc_re_write_sbuf(re, dta->data.l-off, dta->data.payload+off);
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

  llcp_len = llcp_create_pdu((struct llcp_pdu*)act, LLCP_SAP_SNEP,
                             LLCP_PTYPE_UI, LLCP_SAP_SNEP);
  memcpy(act+len, data, len);

  return llcp_len + len;
}

/*
 * LLCP CONNECT
 */

struct llcp_connect_param {
    struct nfc_re* re;
    unsigned char dsap;
    unsigned char ssap;
};

#define LLCP_CONNECT_PARAM_INIT(_re, _dsap, _ssap) \
    { \
        .re = (_re), \
        .dsap = (_dsap), \
        .ssap = (_ssap) \
    }

static ssize_t
create_connect_dta(void* data, struct llcp_pdu* llcp)
{
    const struct llcp_connect_param* param;
    struct llcp_data_link* dl;

    param = data;
    assert(param);

    dl = param->re->llcp_dl[param->dsap] + param->ssap;

    assert(dl->status == LLCP_DATA_LINK_DISCONNECTED);
    dl->status = LLCP_DATA_LINK_CONNECTING;

    return llcp_create_pdu(llcp, param->dsap, LLCP_PTYPE_CONNECT, param->ssap);
}

int
nfc_re_send_llcp_connect(struct nfc_re* re, unsigned char dsap, unsigned char ssap)
{
    struct llcp_connect_param param = LLCP_CONNECT_PARAM_INIT(re, dsap, ssap);
    return send_pdu_from_re(create_connect_dta, &param, re);
}
