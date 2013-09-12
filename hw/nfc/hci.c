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
#include "qemu/bswap.h"
#include "hw/nfc/debug.h"
#include "hw/nfc/nfc.h"
#include "hw/nfc/hci.h"

static size_t
create_control_rsp(struct hci_answer* rsp, enum hci_service service,
                   unsigned char l)
{
    assert(rsp);

    rsp->service = service;
    rsp->l = l;

    return sizeof(*rsp) + l;
}

static size_t
create_control_status_rsp(struct hci_answer* rsp, enum hci_service service,
                          enum hci_status_code status)
{
    assert(rsp);

    rsp->payload[0] = status;

    return create_control_rsp(rsp, service, 1);
}

/* IDLE state */

static size_t
idle_process_bcm2079x_write_sleep_mode_cmd(const union hci_packet* cmd,
                                           struct nfc_device* nfc,
                                           struct hci_answer* rsp)
{
    rsp->payload[0] = HCI_BCM2079x_CMD_WRITE_SLEEP_MODE;
    memcpy(rsp->payload+1, cmd, 2);

    return create_control_rsp(rsp, HCI_BCM2079x_EVT_CMD_COMPLETE, 3);
}

static size_t
idle_process_cmd(const union hci_packet* cmd, struct nfc_device* nfc,
                 struct hci_answer* rsp)
{
    size_t len = 0;

    assert(cmd);

    if (cmd->control.service == HCI_SERVICE_BCM2079x) {
        switch (cmd->control.cmd) {
            case HCI_MESSAGE_RFU|HCI_BCM2079x_CMD_WRITE_SLEEP_MODE:
                len = idle_process_bcm2079x_write_sleep_mode_cmd(cmd, nfc, rsp);
                break;
            default:
                NFC_D("unknown command");
                return 0;
        }
    }
    NFC_D("result length=%ld", (long)len);

    return len;
}

/* RESET state */

static size_t
reset_process_cmd(const union hci_packet* cmd, struct nfc_device* nfc,
                  struct hci_answer* rsp)
{
    assert(0);

    return 0;
}

/* INITIALIZED state */

static size_t
init_process_bcm2079x_write_sleep_mode_cmd(const union hci_packet* cmd,
                                           struct nfc_device* nfc,
                                           struct hci_answer* rsp)
{
    rsp->payload[0] = HCI_BCM2079x_CMD_WRITE_SLEEP_MODE;
    memcpy(rsp->payload+1, cmd, 2);

    return create_control_rsp(rsp, HCI_BCM2079x_EVT_CMD_COMPLETE, 3);
}

static size_t
init_process_cmd(const union hci_packet* cmd, struct nfc_device* nfc,
                 struct hci_answer* rsp)
{
    size_t len = 0;

    assert(cmd);

    if (cmd->control.service == HCI_SERVICE_BCM2079x) {
        switch (cmd->control.cmd) {
            case HCI_MESSAGE_RFU|HCI_BCM2079x_CMD_WRITE_SLEEP_MODE:
                len = init_process_bcm2079x_write_sleep_mode_cmd(cmd, nfc, rsp);
                break;
            default:
                NFC_D("unknown command");
                return 0;
        }
    }
    NFC_D("result length=%ld", (long)len);

    return len;
}

int
nfc_process_hci_cmd(const union hci_packet* cmd, struct nfc_device* nfc,
                    struct hci_answer* rsp)
{
    static size_t (* const process[NUMBER_OF_NFC_FSM_STATES])
      (const union hci_packet*, struct nfc_device*, struct hci_answer*) = {
        [NFC_FSM_STATE_IDLE] = idle_process_cmd,
        [NFC_FSM_STATE_RESET] = reset_process_cmd,
        [NFC_FSM_STATE_INITIALIZED] = init_process_cmd
    };

    NFC_D("HCI service=%x cmd=%x; NFC state=%d",
          cmd->common.service, cmd->common.cmd, nfc->state);

    assert(nfc->state < NUMBER_OF_NFC_FSM_STATES);
    assert(process[nfc->state]);

    return process[nfc->state](cmd, nfc, rsp);
}
