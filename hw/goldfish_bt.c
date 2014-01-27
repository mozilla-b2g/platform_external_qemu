/*
 * Goldfish Bluetooth device over HCI H4.
 *
 * Copyright (C) 2013 Mozilla Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file links various supported qemu Bluetooth backends into
 * Goldfish machine by directing HCI data stream into guest kernel
 * by HCI H4 protocol.
 *
 * In vl{,-android}.c, we parse user command line arguments and
 * prepare |struct HCIInfo| accordingly.  Later we enumerate these
 * infos and create an ABluetoothRec instance for each of them.  The
 * ABluetoothRec structure keeps necessary information to bridge HCI
 * packet stream to and from a CharDriverState, which is then bound
 * into an goldfish_tty device.  `hciattach` will binds this tty
 * device to HCI H4 and then BlueZ takes over.
 *
 * To redirect host physical or vhci devices into target OS, emulator
 * has to be recompiled with libbluetooth.
 *
 * The created serial devices for Bluetooth emulation can be found in
 * /proc/cmdline.  Each of them has an entry "android.bluetooth=?".
 */

#include "qemu-common.h"
#include "qemu-char.h"
#include "charpipe.h"
#include "android/utils/debug.h"
#include <assert.h>
#include "net.h"
#include "hw/bt.h"

//#define DEBUG 1

#if DEBUG
#  define  D(...)                                         \
    do {                                                  \
        if (VERBOSE_CHECK(bluetooth))                     \
            fprintf( stderr, "bluetooth: " __VA_ARGS__ ); \
    } while (0)
#else
#  define  D(...)   ((void)0)
#endif

typedef struct {
    struct HCIInfo*   hci;

    CharDriverState*  cs;
    char              in_buff[ 1024 ];
    int               in_pos;
} ABluetoothRec, *ABluetooth;

static ABluetoothRec _android_bluetooth[MAX_NICS];
static int cur_bt;

#if DEBUG
static void
goldfish_bt_dump(const uint8_t *data,
                 int len)
{
    const uint8_t *p = data, *end = data + len;
    while (p != end) {
        int n = (end -p) <= 8 ? (end - p) : 8;
        switch (n) {
            case 1:
                fprintf(stderr, "\t%02x\n", *p);
                break;
            case 2:
                fprintf(stderr, "\t%02x %02x\n", *p, *(p+1));
                break;
            case 3:
                fprintf(stderr, "\t%02x %02x %02x\n", *p, *(p+1), *(p+2));
                break;
            case 4:
                fprintf(stderr, "\t%02x %02x %02x %02x\n",
                        *p, *(p+1), *(p+2), *(p+3));
                break;
            case 5:
                fprintf(stderr, "\t%02x %02x %02x %02x %02x\n",
                        *p, *(p+1), *(p+2), *(p+3), *(p+4));
                break;
            case 6:
                fprintf(stderr, "\t%02x %02x %02x %02x %02x %02x\n",
                        *p, *(p+1), *(p+2), *(p+3), *(p+4), *(p+5));
                break;
            case 7:
                fprintf(stderr, "\t%02x %02x %02x %02x %02x %02x %02x\n",
                        *p, *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6));
                break;
            default:
                fprintf(stderr, "\t%02x %02x %02x %02x %02x %02x %02x %02x\n",
                        *p, *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
                break;
        }
        p += n;
    }
}
#else
#  define  goldfish_bt_dump(...)   ((void)0)
#endif

static void
goldfish_bt_hci_packet_event(void *opaque,
                             const uint8_t *data,
                             int len)
{
    ABluetooth bt = (ABluetoothRec *)opaque;
    static const uint8_t type = HCI_EVENT_PKT;

    D("goldfish_bt_hci_packet_event(%p, %p, %d)\n", opaque, data, len);
    goldfish_bt_dump(data, len);

    /* goldfish_bt is based on charpipe, which is an additional backend to
     * original qemu char device by AOSP.  Charpipe overrides `chr_write` with
     * a function that always returns the length of buffer data to be written.
     * It ensures this by allocating buffers internally when necessary.
     */
    qemu_chr_write(bt->cs, &type, sizeof(type));
    qemu_chr_write(bt->cs, data, len);
}

static void
goldfish_bt_hci_packet_acl(void *opaque,
                           const uint8_t *data,
                           int len)
{
    ABluetooth bt = (ABluetoothRec *)opaque;
    static const uint8_t type = HCI_ACLDATA_PKT;

    D("goldfish_bt_hci_packet_acl(%p, %p, %d)\n", opaque, data, len);
    goldfish_bt_dump(data, len);

    qemu_chr_write(bt->cs, &type, sizeof(type));
    qemu_chr_write(bt->cs, data, len);
}

static int
goldfish_bt_cs_can_read(void *opaque)
{
    ABluetooth bt = (ABluetoothRec *)opaque;

    return (int)(sizeof(bt->in_buff) - bt->in_pos);
}

static void
goldfish_bt_cs_read(void *opaque,
                   const uint8_t *buf,
                   int size)
{
    ABluetooth bt = (ABluetoothRec *)opaque;
    const uint8_t* p;
    int available, consumed, hsize, dsize;

    /* `size` should be constrained previously by goldfish_bt_cs_can_read(). */
    assert(size <= goldfish_bt_cs_can_read(opaque));

    D("goldfish_bt_cs_read(%p, %s, %d)\n", bt, buf, size);

    memcpy(bt->in_buff + bt->in_pos, buf, size);
    bt->in_pos += size;

    available = bt->in_pos;
    while (available >= 2) {
        p = bt->in_buff + bt->in_pos - available;
        consumed = 0;

        switch (*p++) {
        case HCI_COMMAND_PKT:
            hsize = HCI_COMMAND_HDR_SIZE + 1;
            if (available >= hsize) {
                dsize = ((struct hci_command_hdr *) p)->plen;
                size = hsize + dsize;
                if (available >= size) {
                    bt->hci->cmd_send(bt->hci, p, size - 1);
                    consumed = size;
                }
            }
            break;

        case HCI_ACLDATA_PKT:
            hsize = HCI_ACL_HDR_SIZE + 1;
            if (available >= hsize) {
                dsize = le16_to_cpu(((struct hci_acl_hdr *) p)->dlen);
                size = hsize + dsize;
                if (available >= size) {
                    bt->hci->acl_send(bt->hci, p, size - 1);
                    consumed = size;
                }
            }
            break;

        case HCI_SCODATA_PKT:
            hsize = HCI_SCO_HDR_SIZE + 1;
            if (available >= hsize) {
                dsize = ((struct hci_sco_hdr *) p)->dlen;
                size = hsize + dsize;
                if (available >= size) {
                    bt->hci->sco_send(bt->hci, p, size - 1);
                    consumed = size;
                }
            }
            break;

        default:
            consumed = 1;
            break;
        }

        if (!consumed)
            break;

        available -= consumed;
    }

    if (available != bt->in_pos) {
        memmove(bt->in_buff, bt->in_buff + bt->in_pos - available, available);
        bt->in_pos = available;
    }
}

CharDriverState*
goldfish_bt_new_cs(struct HCIInfo *hci)
{
    CharDriverState*  cs;

    if (cur_bt >= MAX_NICS) {
        D("goldfish_bt_new_cs: too many devices\n");
        return NULL;
    }

    ABluetooth bt = &_android_bluetooth[cur_bt];

    if (qemu_chr_open_charpipe(&bt->cs, &cs) < 0) {
        D("goldfish_bt_new_cs: cannot open charpipe device\n");
        return NULL;
    }
    bt->in_pos = 0;
    qemu_chr_add_handlers( bt->cs,
                           goldfish_bt_cs_can_read,
                           goldfish_bt_cs_read,
                           NULL,
                           bt );

    bt->hci = hci;
    bt->hci->opaque = bt;
    bt->hci->evt_recv = goldfish_bt_hci_packet_event;
    bt->hci->acl_recv = goldfish_bt_hci_packet_acl;

    cur_bt++;

    return cs;
}

bool goldfish_bt_add_remote(char *address)
{
    ABluetooth bt = &_android_bluetooth[0];
    return bt_add_remote(bt->hci, address);
}

void goldfish_bt_remove_remote(char *address)
{
    ABluetooth bt = &_android_bluetooth[0];

    if (!strcmp(address, "all")) {
        bt_remove_all_remotes(bt->hci);
    } else {
        bt_remove_remote(bt->hci, address);
    }
}

bool goldfish_bt_set_remote_property(char *address, char *property, char *value)
{
    ABluetooth bt = &_android_bluetooth[0];
    return bt_set_remote_property(bt->hci, address, property, value);
}

bool goldfish_bt_get_property(char *address, char *property, char *ret)
{
    ABluetooth bt = &_android_bluetooth[0];

    if (!strcmp(address, "local")) {
        return bt_get_local_property(bt->hci, property, ret);
    } else {
        return bt_get_remote_property(bt->hci, address, property, ret);
    }
}

