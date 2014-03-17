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

#include "android/utils/debug.h"
#include "nfc.h"
#include "nfc-hci.h"
#include "nfc-nci.h"
#include "qemu_file.h"
#include "goldfish_device.h"
#include "goldfish_nfc.h"

enum {
    OFFSET_STATUS = 0x000, /* R/W, 8 bits */
    OFFSET_CTRL = 0x001, /* R/W, 8 bits */
    OFFSET_RESERVED0 = 0x002, /* R/W, 8 bits */
    OFFSET_RESERVED1 = 0x003, /* R/W, 8 bits */
    OFFSET_CMND = 0x004, /* R/W, 384 bytes. */
    OFFSET_RESP = 0x184, /* R/W, 384 bytes */
    OFFSET_NTFN = 0x304, /* R/W, 384 bytes */
    OFFSET_DATA = 0x484, /* R/W, 384 bytes */
    OFFSET_RESERVED2 = 0x604, /* R/W, 2556 bytes */
    END_OFFSET = 0x1000 /* no I/O, end at 4096 bytes */
};

/* Status register bits */
enum {
    STATUS_INTR = 0x01, /* IRQ pending */
    STATUS_NCI_CMND = 0x02, /* CMND loaded */
    STATUS_NCI_RESP = 0x04, /* RESP loaded */
    STATUS_NCI_NTFN = 0x08, /* NFTN loaded */
    STATUS_NCI_DATA = 0x10, /* DATA loaded */
    STATUS_HCI_CMND = 0x20, /* HCI CMND loaded */
    STATUS_HCI_RESP = 0x40 /* HCI RESP loaded */
};

/* Command register values */
enum {
    CTRL_INTR_ACK = 0x00, /* Acknowledge IRQ */
    CTRL_NCI_CMND_SNT = 0x01, /* NCI CMDN sent */
    CTRL_RESP_RCV = 0x02, /* RESP received */
    CTRL_NTFN_RCV = 0x03, /* NFTN received */
    CTRL_DATA_RCV = 0x04, /* DATA received */
    CTRL_HCI_CMND_SNT = 0x05 /* HCI CMDN sent */
};

struct nfc_state {
    struct goldfish_device dev;

    uint8_t status;
    uint8_t ctrl;
    uint8_t reserved0;
    uint8_t reserved1;
    uint8_t cmnd[384];
    uint8_t resp[384];
    uint8_t ntfn[384];
    uint8_t data[384];
    uint8_t reserved2[2556]; /* ceil to 4096 */

    struct nfc_device nfc;
};

/* update this each time you update the nfc_state struct */
#define  NCIDEV_STATE_SAVE_VERSION  1

#define  QFIELD_STRUCT  struct nfc_state
QFIELD_BEGIN(goldfish_nfc_fields)
    QFIELD_BYTE(status),
    QFIELD_BYTE(ctrl),
    QFIELD_BYTE(reserved0),
    QFIELD_BYTE(reserved1),
    QFIELD_BUFFER(cmnd),
    QFIELD_BUFFER(resp),
    QFIELD_BUFFER(ntfn),
    QFIELD_BUFFER(data),
    QFIELD_BUFFER(reserved2),
QFIELD_END
#undef  QFIELD_STRUCT

static void
goldfish_nfc_save(QEMUFile* f, void* opaque)
{
    struct nfc_state* s = opaque;

    qemu_put_struct(f, goldfish_nfc_fields, s);
}

static int
goldfish_nfc_load(QEMUFile* f, void* opaque, int version_id)
{
    struct nfc_state* s = opaque;

    if (version_id != NCIDEV_STATE_SAVE_VERSION)
        return -1;

    return qemu_get_struct(f, goldfish_nfc_fields, s);
}

static void
goldfish_nfc_process_ctrl(struct nfc_state* s)
{
    int res;

    if (s->ctrl == CTRL_INTR_ACK) {
        s->status ^= STATUS_INTR;
        goldfish_device_set_irq(&s->dev, 0, 0);

    } else if (s->ctrl == CTRL_RESP_RCV) {
        s->status &= ~(STATUS_NCI_RESP|STATUS_HCI_RESP);

    } else if (s->ctrl == CTRL_NTFN_RCV) {
        s->status ^= STATUS_NCI_NTFN;

    } else if (s->ctrl == CTRL_DATA_RCV) {
        s->status ^= STATUS_NCI_DATA;

    } else if (s->ctrl == CTRL_NCI_CMND_SNT) {
      if (s->status&(STATUS_NCI_RESP|STATUS_HCI_RESP))
          return; /* previous response still loaded, do nothing */

      s->status |= STATUS_NCI_CMND;

      memset(s->resp, 0, sizeof(s->resp));
      res = nfc_process_nci_msg((const union nci_packet*)s->cmnd, &s->nfc,
                                (union nci_packet*)s->resp);

      s->status &= ~STATUS_NCI_CMND;
      s->status |= STATUS_NCI_RESP * !!res;

      s->status |= STATUS_INTR;
      goldfish_device_set_irq(&s->dev, 0, 1);

    } else if (s->ctrl == CTRL_HCI_CMND_SNT) {
      if (s->status&(STATUS_NCI_RESP|STATUS_HCI_RESP))
          return; /* previous response still loaded, do nothing */

      s->status |= STATUS_HCI_CMND;

      memset(s->resp, 0, sizeof(s->resp));
      res = nfc_process_hci_cmd((const union hci_packet*)s->cmnd, &s->nfc,
                                (union hci_answer*)s->resp);

      s->status &= ~STATUS_HCI_CMND;
      s->status |= STATUS_HCI_RESP * !!res;

      s->status |= STATUS_INTR;
      goldfish_device_set_irq(&s->dev, 0, 1);
    }
}

static uint32_t
goldfish_nfc_read8(void* opaque, target_phys_addr_t offset)
{
    struct nfc_state* s = opaque;

    switch (offset) {
        case OFFSET_STATUS:
            return s->status;
        case OFFSET_CTRL:
            return s->ctrl;
        case OFFSET_RESERVED0:
            return s->reserved0;
        case OFFSET_RESERVED1:
            return s->reserved1;
        default:
            if (offset < OFFSET_RESP)
                return s->cmnd[offset-OFFSET_CMND];
            else if (offset < OFFSET_NTFN)
                return s->resp[offset-OFFSET_RESP];
            else if (offset < OFFSET_DATA)
                return s->ntfn[offset-OFFSET_NTFN];
            else if (offset < OFFSET_RESERVED2)
                return s->data[offset-OFFSET_DATA];
            else if (offset < END_OFFSET)
                return s->reserved2[offset-OFFSET_RESERVED2];

            cpu_abort(cpu_single_env, "goldfish_nfc_read8: bad offset %x\n", offset);
    }

    return 0;
}

static void
goldfish_nfc_write8(void* opaque, target_phys_addr_t offset,
                       uint32_t value)
{
    struct nfc_state* s = opaque;

    switch (offset) {
        case OFFSET_STATUS:
            break;
        case OFFSET_CTRL:
            s->ctrl = value;
            goldfish_nfc_process_ctrl(s);
            s->ctrl = 0;
            break;
        case OFFSET_RESERVED0:
            s->reserved0 = value;
            break;
        case OFFSET_RESERVED1:
            s->reserved1 = value;
            break;
        default:
            if (offset < OFFSET_RESP)
                s->cmnd[offset-OFFSET_CMND] = value;
            else if (offset < OFFSET_NTFN)
                s->resp[offset-OFFSET_RESP] = value;
            else if (offset < OFFSET_DATA)
                s->ntfn[offset-OFFSET_NTFN] = value;
            else if (offset < OFFSET_RESERVED2)
                s->data[offset-OFFSET_DATA] = value;
            else if (offset < END_OFFSET)
                s->reserved2[offset-OFFSET_RESERVED2] = value;
            else
                cpu_abort(cpu_single_env,
                          "goldfish_nfc_write8: bad offset %x\n", offset);
            break;
    }
}

static CPUReadMemoryFunc* goldfish_nfc_readfn[] = {
    goldfish_nfc_read8
};

static CPUWriteMemoryFunc* goldfish_nfc_writefn[] = {
    goldfish_nfc_write8
};

static struct nfc_state _nfc_states[1];

void
goldfish_nfc_init()
{
    struct nfc_state* s = &_nfc_states[0];

    nfc_device_init(&s->nfc);

    s->dev.name = "goldfish_nfc";
    s->dev.base = 0;  /* allocated dynamically. */
    s->dev.size = 4096; /* overall size of I/O buffers */
    s->dev.irq = 0; /* allocated dynamically */
    s->dev.irq_count = 1;

    goldfish_device_add(&s->dev, goldfish_nfc_readfn,
                        goldfish_nfc_writefn, s);

    register_savevm("nfc_state", 0, NCIDEV_STATE_SAVE_VERSION,
                    goldfish_nfc_save, goldfish_nfc_load, s);
}

int
goldfish_nfc_send_dta(ssize_t (*create)(void*, struct nfc_device*,
                                        union nci_packet*), void* data)
{
    struct nfc_state* s;
    size_t res;

    assert(create);

    s = &_nfc_states[0];

    memset(s->data, 0, sizeof(s->data));
    res = create(data, &s->nfc, (union nci_packet*)s->data);
    if (res < 0)
      return -1;

    s->status |= STATUS_NCI_DATA * !!res;
    s->status |= STATUS_INTR;
    goldfish_device_set_irq(&s->dev, 0, 1);

    return 0;
}

int
goldfish_nfc_send_ntf(ssize_t (*create)(void*, struct nfc_device*,
                                        union nci_packet*), void* data)
{
    struct nfc_state* s;
    size_t res;

    assert(create);

    s = &_nfc_states[0];

    memset(s->ntfn, 0, sizeof(s->ntfn));
    res = create(data, &s->nfc, (union nci_packet*)s->ntfn);
    if (res < 0)
      return -1;

    s->status |= STATUS_NCI_NTFN * !!res;
    s->status |= STATUS_INTR;
    goldfish_device_set_irq(&s->dev, 0, 1);

    return 0;
}
