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
#include "qemu_file.h"
#include "goldfish_device.h"
#include "android/utils/debug.h"

//#define DEBUG 1

#if DEBUG
#  define  D(...)                                      \
    do {                                               \
        if (VERBOSE_CHECK(bluetooth))                  \
            fprintf( stderr, "rfkill: " __VA_ARGS__ ); \
    } while (0)
#else
#  define  D(...)   ((void)0)
#endif

enum {
    OFFSET_INT_MASK = 0x00, /* W, 32 bits.   */
    OFFSET_INT      = 0x04, /* R/W, 32 bits. */
    OFFSET_HWBLOCK  = 0x08, /* R, 32 bits.   */
    OFFSET_BLOCK    = 0x0C, /* W, 32 bits.   */
};

enum {
    INT_RFKILL = 0x01,
};

struct rfkill_state {
    struct goldfish_device dev;

    uint32_t int_mask;
    uint32_t inta;

    uint32_t hw_block;
    uint32_t blocking;
};

/* update this each time you update the rfkill_state struct */
#define  RFKILL_STATE_SAVE_VERSION  1

#define  QFIELD_STRUCT  struct rfkill_state
QFIELD_BEGIN(goldfish_rfkill_fields)
    QFIELD_INT32(int_mask),
    QFIELD_INT32(inta),
    QFIELD_INT32(hw_block),
    QFIELD_INT32(blocking),
QFIELD_END
#undef  QFIELD_STRUCT

static struct rfkill_state _rfkill_states[1];

static void
goldfish_rfkill_save(QEMUFile* f,
                     void*     opaque)
{
    struct rfkill_state*  s = opaque;

    qemu_put_struct(f, goldfish_rfkill_fields, s);
}

static int
goldfish_rfkill_load(QEMUFile* f,
                     void*     opaque,
                     int       version_id)
{
    struct rfkill_state*  s = opaque;

    if (version_id != RFKILL_STATE_SAVE_VERSION)
        return -1;

    return qemu_get_struct(f, goldfish_rfkill_fields, s);
}

static void
goldfish_rfkill_update_irq(struct rfkill_state *s)
{
    /* Pull down IRQ line if no active interrupt source. */
    int level = (s->int_mask & s->inta) ? 1 : 0;
    D("irq level: %d\n", level);
    goldfish_device_set_irq(&s->dev, 0, level);
}

static uint32_t
goldfish_rfkill_read(void *opaque,
                     target_phys_addr_t offset)
{
    struct rfkill_state *s = opaque;

    switch(offset) {
        case OFFSET_INT:
            D("read inta 0x%08x\n", s->inta);
            return s->inta;

        case OFFSET_HWBLOCK:
            D("read hw_block 0x%08x\n", s->hw_block);
            return s->hw_block;

        default:
            cpu_abort (cpu_single_env, "goldfish_rfkill_read: bad offset %x\n", offset);
            return 0;
    }
}

static void
goldfish_rfkill_write(void *opaque,
                      target_phys_addr_t offset,
                      uint32_t value)
{
    struct rfkill_state *s = opaque;

    switch(offset) {
        case OFFSET_INT_MASK:
            D("write int_mask 0x%08x, current 0x%08x\n", value, s->int_mask);
            if (s->int_mask != value) {
                s->int_mask = value;
                goldfish_rfkill_update_irq(s);
            }
            break;

        case OFFSET_INT:
            D("write inta 0x%08x, current 0x%08x\n", value, s->inta);
            value &= s->inta;  /* Erase possible ineffective bits. */
            s->inta &= ~value; /* Clear acked active interrupts. */
            goldfish_rfkill_update_irq(s);
            break;

        case OFFSET_BLOCK:
            D("write blocking 0x%08x, current 0x%08x, then 0x%08x\n",
              value, s->blocking, value | s->hw_block);
            s->blocking = value | s->hw_block;
            break;

        default:
            cpu_abort(cpu_single_env, "goldfish_rfkill_write: bad offset %x\n", offset);
            break;
    }
}

static CPUReadMemoryFunc *goldfish_rfkill_readfn[] = {
    goldfish_rfkill_read,
    goldfish_rfkill_read,
    goldfish_rfkill_read
};

static CPUWriteMemoryFunc *goldfish_rfkill_writefn[] = {
    goldfish_rfkill_write,
    goldfish_rfkill_write,
    goldfish_rfkill_write
};

void
goldfish_rfkill_init()
{
    struct rfkill_state *s = &_rfkill_states[0];

    s->dev.name = "goldfish_rfkill";
    s->dev.base = 0;  /* Will be allocated dynamically. */
    s->dev.size = 0x1000;
    s->dev.irq_count = 1;

    s->int_mask = 0;
    s->inta = 0;
    s->hw_block = 0;
    s->blocking = 0;

    goldfish_device_add(&s->dev, goldfish_rfkill_readfn, goldfish_rfkill_writefn, s);

    register_savevm( "rfkill_state", 0, RFKILL_STATE_SAVE_VERSION,
                     goldfish_rfkill_save, goldfish_rfkill_load, s);
}

uint32_t
android_rfkill_get_blocking()
{
    struct rfkill_state *s = &_rfkill_states[0];
    return s->blocking;
}

uint32_t
android_rfkill_get_hardware_block()
{
    struct rfkill_state *s = &_rfkill_states[0];
    return s->hw_block;
}

void
android_rfkill_set_hardware_block(uint32_t hw_block)
{
    struct rfkill_state *s = &_rfkill_states[0];
    uint32_t diff;

    if (hw_block == s->hw_block) {
        return;
    }

    diff = (~hw_block & s->hw_block) | (hw_block & ~s->hw_block);
    D("set hw_block (0x%08x -> 0x%08x), inta (0x%08x -> 0x%08x)\n",
      s->hw_block, hw_block, s->inta, s->inta | diff);
    s->hw_block = hw_block;

    s->inta |= diff;
    goldfish_rfkill_update_irq(s);
}
