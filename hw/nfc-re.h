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

/* NFC Remote Endpoint */
struct nfc_re {
    enum nci_rf_protocol rfproto;
    enum nci_rf_tech_mode mode;
    char nfcid3[10];
    uint8_t id;
    /* outer array is always remote SAP, inner array is local, emulated SAP */
    struct llcp_connection_state llcp_cs[LLCP_NUMBER_OF_SAPS][LLCP_NUMBER_OF_SAPS];
    int send_symm;
    QEMUTimer *send_symm_timer;
    uint8_t connid;
    size_t sbufsiz;
    size_t rbufsiz;
    uint8_t sbuf[1024]; /* data written by NFC driver */
    uint8_t rbuf[1024]; /* data for reading from RE */
};

#define INIT_NFC_RE(re_, rfproto_, mode_, nfcid3_) \
    re_ = { \
        .rfproto = rfproto_, \
        .mode = mode_, \
        .nfcid3 = nfcid3_, \
        .id = 0, \
        .send_symm = 0, \
        .send_symm_timer = NULL, \
        .connid = 0, \
        .sbufsiz = 0, \
        .rbufsiz = 0 \
    }

/* predefined NFC Remote Endpoints */
extern struct nfc_re nfc_res[3];

struct nfc_re*
nfc_get_re_by_id(uint8_t id);

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
nfc_re_create_rf_intf_activated_ntf_act(struct nfc_re* re, uint8_t* act);

size_t
nfc_re_create_dta_act(struct nfc_re* re, const void* data,
                      size_t len, uint8_t* act);

#endif
