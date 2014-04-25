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

#ifndef nfc_hci_h
#define nfc_hci_h

#include <stdint.h>

struct nfc_device;

enum hci_service {
    HCI_SERVICE_BCM2079x = 0x27
};

enum hci_message_type {
    HCI_MESSAGE_CMD = 0x00,
    HCI_MESSAGE_ANS = 0x40,
    HCI_MESSAGE_EVT = 0x80,
    HCI_MESSAGE_RFU = 0xc0
};

enum hci_command {
    HCI_BCM2079x_EVT_CMD_COMPLETE = 0x0e,
    HCI_BCM2079x_CMD_WRITE_SLEEP_MODE = 0x3c
};

enum hci_status_code {
    HCI_STATUS_OK = 0
};

struct hci_common_packet {
    uint8_t service;
    uint8_t cmd;
    uint8_t l;
};

struct hci_control_packet {
    uint8_t service;
    uint8_t cmd;
    uint8_t l;
    uint8_t payload[256];
};

union hci_packet {
    struct hci_common_packet common;
    struct hci_control_packet control;
};

struct hci_bcm2079x_common_evt {
    uint8_t cmd;
    uint8_t l;
};

/* HCI_BRCM2079x_WRITE_SLEEP_MODE */

struct hci_bcm2079x_evt_cmd_complete {
    struct hci_bcm2079x_common_evt common;
    uint8_t npackets;
    uint8_t service;
    uint8_t cmd;
    uint8_t status;
};

struct hci_bcm2079x_write_sleep_mode_cmd {
    uint8_t snooze_mode;
    uint8_t idle_threashold_dh;
    uint8_t idle_threashold_nfcc;
    uint8_t nfc_wake_active_mode;
    uint8_t dh_wake_active_mode;
    uint8_t rfu[7];
};

union hci_event {
    struct hci_bcm2079x_common_evt common;
    struct hci_bcm2079x_evt_cmd_complete cmd_complete;
};

union hci_answer {
    union hci_event evt;
};

int
nfc_process_hci_cmd(const union hci_packet* cmd, struct nfc_device* nfc,
                    union hci_answer* rsp);

#endif
