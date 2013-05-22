/*
 * Wrap Android Bluetooth HCI socket in a struct HCIInfo.
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

#include "qemu-common.h"
#include "qemu-char.h"
#include "charpipe.h"
#include "android/utils/assert.h"
#include "android/utils/debug.h"
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

static struct bt_l2cap_device_s *remote_bluetooth[MAX_NICS];
static int cur_rbt;
static struct bt_scatternet_s *net; // where local and remote devices coexist

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
    uint8_t type = HCI_EVENT_PKT;

    D("goldfish_bt_hci_packet_event(%p, %p, %d)\n", opaque, data, len);
    goldfish_bt_dump(data, len);

    qemu_chr_write(bt->cs, &type, 1);
    qemu_chr_write(bt->cs, data, len);
}

static void
goldfish_bt_hci_packet_acl(void *opaque,
                           const uint8_t *data,
                           int len)
{
    ABluetooth bt = (ABluetoothRec *)opaque;
    uint8_t type = HCI_ACLDATA_PKT;

    D("goldfish_bt_hci_packet_acl(%p, %p, %d)\n", opaque, data, len);
    goldfish_bt_dump(data, len);

    qemu_chr_write(bt->cs, &type, 1);
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
    const uint8_t* p = bt->in_buff;
    int available = bt->in_pos;
    int hsize, dsize;

    /* `size` should be determined previously by goldfish_bt_cs_can_read(). */
    AASSERT_INT_EQ(size, goldfish_bt_cs_can_read(opaque));

    D("goldfish_bt_cs_read(%p, %s, %d)\n", bt, buf, size);

    memcpy(p + available, buf, size);
    available += size;
    bt->in_pos = available;

    while (available >= 2) {
        switch (*p++) {
        case HCI_COMMAND_PKT:
            hsize = HCI_COMMAND_HDR_SIZE + 1;
            if (available >= hsize) {
                dsize = ((struct hci_command_hdr *) p)->plen;
                size = hsize + dsize;
                if (available >= size) {
                    bt->hci->cmd_send(bt->hci, p, size - 1);
                    available -= size;
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
                    available -= size;
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
                    available -= size;
                }
            }
            break;

        default:
            --available;
            break;
        }

        if (available == bt->in_pos) {
            break;
        }

        memmove(bt->in_buff, bt->in_buff + bt->in_pos - available, available);
        bt->in_pos = available;
        p = bt->in_buff;
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

const char*
goldfish_bt_get(const char *query, char *result)
{
    ABluetooth bt = &_android_bluetooth[0];
    return bt_hci_get(bt->hci, query, result);
}

void
goldfish_bt_radd(char *str)
{
    if (cur_rbt >= MAX_NICS) {
        D("goldfish_bt_radd: too many remote devices\n");
        return;
    }

    // Get scatternet at first time
    if (!cur_rbt) {
        ABluetooth bt = &_android_bluetooth[0];
        net = bt_hci_get_net(bt->hci);
    }

    remote_bluetooth[cur_rbt] = (struct bt_l2cap_device_s *)
                            qemu_mallocz(sizeof(struct bt_l2cap_device_s));

    // Parse str and add remote device to scatternet
    if (!bt_l2cap_radd(remote_bluetooth[cur_rbt], net, str)) {
        D("goldfish_bt_radd: free remote_bluetooth[%d]\r\n", cur_rbt);
        qemu_free(remote_bluetooth[cur_rbt]);
        return;
    }

    cur_rbt++;
}

void
goldfish_bt_rclr()
{
    int i;
    for (i = 0; i < cur_rbt; i++) {
        struct bt_device_s *dev = &remote_bluetooth[i]->device;
        if (dev->lmp_name)
            qemu_free((void *) dev->lmp_name);
        qemu_free(remote_bluetooth[i]);
    }
    D("goldfish_bt_rclr: removed %d remote devices\r\n", cur_rbt);
    cur_rbt = 0;

    // Keep only local device in the scatternet
    net->slave->next = NULL;
}
