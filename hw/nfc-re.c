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
#include "nfc-tag.h"
#include "llcp.h"
#include "snep.h"
#include "llcp-snep.h"
#include "nfc-re.h"

struct nfc_re nfc_res[3] = {
    INIT_NFC_RE([0], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, NULL, "deadbeaf0", nfc_res+0),
    INIT_NFC_RE([1], NCI_RF_PROTOCOL_NFC_DEP, NCI_RF_NFC_F_PASSIVE_LISTEN_MODE, NULL, "deadbeaf1", nfc_res+1),
    INIT_NFC_RE([2], NCI_RF_PROTOCOL_T2T, NCI_RF_NFC_A_PASSIVE_LISTEN_MODE, nfc_tags+0, "deadbeaf2", nfc_res+2)
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
            qemu_del_timer(re->xmit_timer);
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
        re->xmit_timer = qemu_new_timer_ms(vm_clock, xmit_next_cb, re);
        assert(re->xmit_timer);
    }
    if (!qemu_timer_pending(re->xmit_timer)) {
        /* xmit PDU in two seconds */
        qemu_mod_timer(re->xmit_timer, qemu_get_clock_ms(vm_clock)+2000);
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

void
nfc_clear_re(struct nfc_re* re)
{
    size_t dsap, ssap;

    assert(re);

    for (dsap = 0; dsap < ARRAY_SIZE(re->llcp_dl); ++dsap) {
        for (ssap = 0; ssap < ARRAY_SIZE(re->llcp_dl[dsap]); ++ssap) {
            llcp_init_data_link(re->llcp_dl[dsap]+ssap);
        }
    }

    re->last_dsap = LLCP_SAP_LM;
    re->last_ssap = LLCP_SAP_LM;
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

/*
 * LLCP support
 */

static void
update_last_saps(struct nfc_re* re, enum llcp_sap dsap, enum llcp_sap ssap)
{
    assert(re);
    assert(dsap < LLCP_NUMBER_OF_SAPS);
    assert(ssap < LLCP_NUMBER_OF_SAPS);

    re->last_dsap = dsap;
    re->last_ssap = ssap;
}

static size_t (* const llcp_sap_cb[LLCP_NUMBER_OF_SAPS])(struct llcp_data_link*,
                                                   const uint8_t*, size_t,
                                                         struct snep*) = {
    [LLCP_SAP_SNEP] = llcp_sap_snep
};

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

    update_last_saps(re, llcp->ssap, llcp->dsap);

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

    update_last_saps(re, llcp->ssap, llcp->dsap);

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

    /* move DL's pending PDUs to global xmit queue */
    while (!QTAILQ_EMPTY(&dl->xmit_q)) {
        struct llcp_pdu_buf* buf = QTAILQ_FIRST(&dl->xmit_q);
        QTAILQ_REMOVE(&dl->xmit_q, buf, entry);
        QTAILQ_INSERT_TAIL(&re->xmit_q, buf, entry);
    }

    update_last_saps(re, llcp->ssap, llcp->dsap);

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

    update_last_saps(re, llcp->ssap, llcp->dsap);

    /* consume 1 extra byte for 'reason' field */
    *consumed = sizeof(*llcp) + 1;

    return 0;
}

static size_t
process_ptype_frmr(struct nfc_re* re, const struct llcp_pdu* llcp,
                size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    unsigned int flags = (llcp->info[0] >> 4) & 0xf;
    unsigned int ptype =  llcp->info[0] & 0xf;
    unsigned int v_s = (llcp->info[2] >> 4) & 0xf;
    unsigned int v_r =  llcp->info[2] & 0xf;
    unsigned int v_sa = (llcp->info[3] >> 4) & 0xf;
    unsigned int v_ra =  llcp->info[3] & 0xf;

    NFC_D("LLCP FRMR flags=%x ptype=%u sequence=%u v_s=%u v_r=%u v_sa=%u "
          "v_sr=%u\n", flags, ptype, llcp->info[1], v_s, v_r, v_sa, v_ra);

    update_last_saps(re, llcp->ssap, llcp->dsap);

    *consumed = sizeof(*llcp) + 4;

    return 0;
}

static size_t
process_ptype_i(struct nfc_re* re, const struct llcp_pdu* llcp,
                size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    const uint8_t* info;
    struct llcp_data_link* dl;
    ssize_t res;

    dl = re->llcp_dl[llcp->ssap] + llcp->dsap;
    dl->v_r = (dl->v_r + 1) % 16;

    /* I PDUs transfer messages (i.e., 'Service Data Units' in LLCP
     * speak) over LLCP connections. In our case we hand over the
     * payload to the next higher-layered protocol, or simply put
     * it into the RE's send buffer.
     */

    update_last_saps(re, llcp->ssap, llcp->dsap);

    /* consume llcp header and sequence numbers */
    *consumed = sizeof(*llcp) + 1;
    len -= *consumed;

    info = ((const uint8_t*)llcp) + *consumed;
    if (llcp_sap_cb[llcp->dsap]) {
        /* there's a handler for this SAP, call it and build an LLCP
         * header if there is a response */
        res = llcp_sap_cb[llcp->dsap](dl, info, len,
                                      (struct snep*)rsp->info+1);
        if (res) {
            res += llcp_create_pdu_i(rsp, llcp->ssap, llcp->dsap,
                                     dl->v_s, dl->v_r);
        }
    } else {
        /* copy information field into re->sbuf */
        llcp_dl_write_rbuf(dl, len, info);
        res = 0;
    }

    return res;
}

static size_t
process_ptype_rr(struct nfc_re* re, const struct llcp_pdu* llcp,
                size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;
    unsigned int nr;

    nr = llcp->info[0] & 0xf;

    NFC_D("LLCP RR N(R)=%d", nr);

    dl = re->llcp_dl[llcp->ssap] + llcp->dsap;
    dl->v_sa = nr;

    update_last_saps(re, llcp->ssap, llcp->dsap);

    *consumed = sizeof(*llcp) + 1;
    return 0;
}

static size_t
process_ptype_rnr(struct nfc_re* re, const struct llcp_pdu* llcp,
                  size_t len, uint8_t* consumed, struct llcp_pdu* rsp)
{
    struct llcp_data_link* dl;
    unsigned int nr;

    nr = llcp->info[0] & 0xf;

    NFC_D("LLCP RNR N(R)=%d", nr);

    dl = re->llcp_dl[llcp->ssap] + llcp->dsap;
    dl->v_sa = nr;

    update_last_saps(re, llcp->ssap, llcp->dsap);

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
        [LLCP_PTYPE_FRMR] = process_ptype_frmr,
        [LLCP_PTYPE_I] = process_ptype_i,
        [LLCP_PTYPE_RR] = process_ptype_rr,
        [LLCP_PTYPE_RNR] = process_ptype_rnr
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

static size_t
nfc_re_create_activated_ntf_tech_nfca_poll(struct nfc_re* re, uint8_t* act)
{
    uint8_t* p;
    enum nci_rf_protocol protocol;
    size_t cid1_len;

    assert(re);

    p = act;
    protocol = re->rfproto;
    cid1_len = ARRAY_SIZE(re->nfcid1);

    /* [NCI] Table54. */
    /* [DIGITAL], Sec 4.6.3 SENS_RES */
    if (cid1_len == 4) {
        *p = SREN_RES_NFCID_4_BYTES;
    } else if (cid1_len == 7) {
        *p = SREN_RES_NFCID_7_BYTES;
    } else if (cid1_len == 10) {
        *p = SREN_RES_NFCID_10_BYTES;
    } else {
        assert(0);
    }

    if (protocol == NCI_RF_PROTOCOL_T1T) {
        *p++ = *p | SREN_RES_BIT_FRAME_SDD_T1T;   /* Byte1 of SENS_RES */
        *p++ = SREN_RES_T1T;                      /* Byte2 of SENS_RES */
        *p++ = 0;                                 /* NFCID1 Length */
        *p++ = 0;
    } else {
        *p++ = *p | SREN_RES_BIT_FRAME_SDD_OTHER_TAGS; /* Byte1 of SENS_RES */
        *p++ = SREN_RES_OTHER_TAGS;                    /* Byte2 of SENS_RES */
        *p++ = cid1_len;                               /* NFCID1 Length */
        memcpy(p, re->nfcid1, cid1_len);               /* NFCID1 */
        p += cid1_len;
        *p++ = 1;                                      /* SEL_RES Response Length */
        /* [DIGITAL], Sec 4.8.2 */
        if (protocol == NCI_RF_PROTOCOL_T2T) {
            *p++ = SEL_RES_T2T;
        } else if (protocol == NCI_RF_PROTOCOL_NFC_DEP) {
            *p++ = SEL_RES_NFC_NFC_DEP;
        } else {
            *p++ = SEL_RES_OTHER_TAGS;
        }
    }

    return p-act;
}

size_t
nfc_re_create_rf_intf_activated_ntf_tech(enum nci_rf_tech_mode mode,
                                         struct nfc_re* re, uint8_t* act)
{
    switch (mode) {
        case NCI_RF_NFC_A_PASSIVE_POLL_MODE:
            return nfc_re_create_activated_ntf_tech_nfca_poll(re, act);
        default:
            return 0;
    }
}

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
  struct llcp_data_link* dl;
  size_t llcp_len;

  assert(act);

  dl = re->llcp_dl[LLCP_SAP_SNEP] + LLCP_SAP_SNEP;
  llcp_len = llcp_create_pdu_i((struct llcp_pdu*)act,
                               LLCP_SAP_SNEP, LLCP_SAP_SNEP,
                               dl->v_s, dl->v_r);
  dl->v_s = (dl->v_s+1) % 16;
  dl->v_r = (dl->v_r+1) % 16;

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

/*
 * SNEP PUT
 */

struct llcp_i_param {
    struct nfc_re* re;
    unsigned char dsap;
    unsigned char ssap;
    ssize_t (*create_snep)(void*, size_t, struct snep*);
    void* data;
};

#define LLCP_I_PARAM_INIT(_re, _dsap, _ssap, _create_snep, _data) \
    { \
        .re = (_re), \
        .dsap = (_dsap), \
        .ssap = (_ssap), \
        .create_snep = (_create_snep), \
        .data =  (_data) \
    }

static ssize_t
create_i_pdu(void* data, struct llcp_pdu* llcp)
{
    const struct llcp_i_param* param;
    struct llcp_data_link* dl;
    size_t len;
    struct snep* snep;
    ssize_t res;

    param = data;
    assert(param);

    dl = param->re->llcp_dl[param->dsap] + param->ssap;
    len = llcp_create_pdu_i(llcp, param->dsap, param->ssap, dl->v_s, dl->v_r);

    snep = (struct snep*)(llcp->info + (len-sizeof(*llcp)));
    res = param->create_snep(param->data, 200-len, snep);
    if (res < 0) {
        return -1;
    }
    dl->v_s = (dl->v_s + 1) % 16;

    return len + res;
}

static int
send_snep_over_llcp(struct nfc_re* re,
                    enum llcp_sap dsap, enum llcp_sap ssap,
                    ssize_t (*create)(void*, size_t, struct snep*),
                    void* data)
{
    int res;
    struct llcp_data_link* dl;
    struct llcp_i_param i_param =
        LLCP_I_PARAM_INIT(re, dsap, ssap, create, data);

    res = 0;
    dl = re->llcp_dl[dsap] + ssap;

    if (dl->status == LLCP_DATA_LINK_DISCONNECTED) {
        /* enqueue request for later delivery and connect first */
        struct llcp_pdu_buf* buf;
        struct llcp_connect_param connect_param =
            LLCP_CONNECT_PARAM_INIT(re, dsap, ssap);
        buf = llcp_alloc_pdu_buf();
        if (!buf) {
            return -1;
        }
        buf->len = create_i_pdu(&i_param, (struct llcp_pdu*)buf->pdu);
        QTAILQ_INSERT_TAIL(&dl->xmit_q, buf, entry);
        /* on connecting successfully, pending packets will be delivered */
        res = send_pdu_from_re(create_connect_dta, &connect_param, re);
    } else if (dl->status == LLCP_DATA_LINK_CONNECTING) {
        /* connecting in process; only enqueue request for later delivery */
        struct llcp_pdu_buf* buf = llcp_alloc_pdu_buf();
        if (!buf) {
            return -1;
        }
        buf->len = create_i_pdu(&i_param, (struct llcp_pdu*)buf->pdu);
        QTAILQ_INSERT_TAIL(&dl->xmit_q, buf, entry);
    } else if (dl->status == LLCP_DATA_LINK_CONNECTED) {
        /* normal operation; send a SNEP request */
        res = send_pdu_from_re(create_i_pdu, &i_param, re);
    } else {
        /* don't send a request for disconnecting links */
        assert(dl->status == LLCP_DATA_LINK_DISCONNECTING);
    }
    return res;
}

int
nfc_re_send_snep_put(struct nfc_re* re,
                     enum llcp_sap dsap, enum llcp_sap ssap,
                     ssize_t (*create_snep)(void*, size_t, struct snep*),
                     void* data)
{
    int res;

    assert(re);

    switch (re->rfproto) {
        case NCI_RF_PROTOCOL_NFC_DEP:
            /* send SNEP over LLCP */
            res = send_snep_over_llcp(re, dsap, ssap, create_snep, data);
            break;
        default:
            /* TODO: support over protocols */
            return -1;
    }
    return res;
}

static int
recv_snep_over_llcp(struct nfc_re* re,
                    enum llcp_sap dsap, enum llcp_sap ssap,
                    ssize_t (*process)(void*, size_t, const struct ndef_rec*),
                    void* data)
{
    struct llcp_data_link* dl;
    ssize_t res;

    dl = re->llcp_dl[dsap] + ssap;

    /* normal operation; process last received SNEP request */
    assert(dl->status == LLCP_DATA_LINK_CONNECTED);

    res = process(data, dl->rlen, (const struct ndef_rec*)dl->rbuf);
    if (res < 0) {
        return -1;
    }

    return 0;
}

int
nfc_re_recv_snep_put(struct nfc_re* re,
                     enum llcp_sap dsap, enum llcp_sap ssap,
                     ssize_t (*process_ndef)(void*, size_t,
                                       const struct ndef_rec*),
                     void* data)
{
    int res;

    assert(re);

    switch (re->rfproto) {
        case NCI_RF_PROTOCOL_NFC_DEP:
            /* send SNEP over LLCP */
            res = recv_snep_over_llcp(re, dsap, ssap, process_ndef, data);
            break;
        default:
            /* TODO: support over protocols */
            return -1;
    }
    return res;
}
