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

#ifndef nfc_re_h
#define nfc_re_h

#include "qemu-timer.h"
#include "llcp.h"
#include "nfc-rf.h"

union nci_packet;
struct nfc_tag;
struct ndef_rec;
struct snep;

/* [DIGITAL], Sec 4.6.3 SENS_RES */
enum {
    SREN_RES_NFCID_4_BYTES = 0x00,
    SREN_RES_NFCID_7_BYTES = 0x40,
    SREN_RES_NFCID_10_BYTES = 0x80,
    SREN_RES_NFCID_RFU = 0xC0
};

enum {
    SREN_RES_BIT_FRAME_SDD_T1T = 0x00,
    SREN_RES_BIT_FRAME_SDD_OTHER_TAGS = 0x1F
};

enum {
    SREN_RES_T1T = 0x0C,
    SREN_RES_OTHER_TAGS = 0x00
};

/* [DIGITAL], Sec 4.8.2 SEL_RES */
/* Assume NFCID1 complete */
enum {
    SEL_RES_T2T = 0x00,
    SEL_RES_NFC_NFC_DEP = 0x60,
    SEL_RES_OTHER_TAGS = 0x10
};

/* NFC Remote Endpoint */
struct nfc_re {
    enum nci_rf_protocol rfproto;
    enum nci_rf_tech_mode mode;
    char nfcid1[10];
    char nfcid3[10];
    uint8_t id;
    struct nfc_tag* tag;
    /* outer array is always remote SAP, inner array is local, emulated SAP */
    struct llcp_data_link llcp_dl[LLCP_NUMBER_OF_SAPS][LLCP_NUMBER_OF_SAPS];
    enum llcp_sap last_dsap; /* last remote SAP */
    enum llcp_sap last_ssap; /* last local SAP */
    int xmit_next; /* true if we are supposed to send the next PDU */
    QEMUTimer *xmit_timer;
    struct llcp_pdu_queue xmit_q;
    uint8_t connid;
    size_t sbufsiz;
    size_t rbufsiz;
    uint8_t sbuf[1024]; /* data written by NFC driver */
    uint8_t rbuf[1024]; /* data for reading from RE */
};

#define INIT_NFC_RE(re_, rfproto_, mode_, tag_, nfcid_, addr_) \
    re_ = { \
        .rfproto = rfproto_, \
        .mode = mode_, \
        .tag = tag_, \
        .nfcid1 = nfcid_, \
        .nfcid3 = nfcid_, \
        .id = 0, \
        .xmit_next = 0, \
        .xmit_timer = NULL, \
        .xmit_q = QTAILQ_HEAD_INITIALIZER((addr_)->xmit_q), \
        .connid = 0, \
        .sbufsiz = 0, \
        .rbufsiz = 0 \
    }

/* predefined NFC Remote Endpoints */
extern struct nfc_re nfc_res[3];

struct nfc_re*
nfc_get_re_by_id(uint8_t id);

void
nfc_clear_re(struct nfc_re* re);

ssize_t
nfc_re_write_sbuf(struct nfc_re* re, size_t len, const void* data);

ssize_t
nfc_re_read_sbuf(struct nfc_re* re, size_t len, void* data);

ssize_t
nfc_re_write_rbuf(struct nfc_re* re, size_t len, const void* data);

ssize_t
nfc_re_read_rbuf(struct nfc_re* re, size_t len, void* data);

size_t
nfc_re_process_data(struct nfc_re* re, const union nci_packet* dta,
                    union nci_packet* rsp);

size_t
nfc_re_create_rf_intf_activated_ntf_tech(enum nci_rf_tech_mode mode,
                                         struct nfc_re* re, uint8_t* act);

size_t
nfc_re_create_rf_intf_activated_ntf_act(struct nfc_re* re, uint8_t* act);

size_t
nfc_re_create_dta_act(struct nfc_re* re, const void* data,
                      size_t len, uint8_t* act);

int
nfc_re_send_llcp_connect(struct nfc_re* re, unsigned char dsap,
                         unsigned char ssap);

int
nfc_re_send_snep_put(struct nfc_re* re,
                     enum llcp_sap dsap, enum llcp_sap ssap,
                     ssize_t (*create_snep)(void*, size_t, struct snep*),
                     void* data);

int
nfc_re_recv_snep_put(struct nfc_re* re,
                     enum llcp_sap dsap, enum llcp_sap ssap,
                     ssize_t (*process_ndef)(void*, size_t, const struct ndef_rec*),
                     void* data);

#endif
