#ifndef GOLDFISH_BT_H
#define GOLDFISH_BT_H

#include "bt-host.h" // struct HCIInfo;

typedef enum {
    RFKILL_TYPE_WLAN = 0,
    RFKILL_TYPE_BLUETOOTH,
    RFKILL_TYPE_UWB,
    RFKILL_TYPE_WIMAX,
    RFKILL_TYPE_WWAN,

    RFKILL_TYPE_MAX
} RfkillTypes;

#define RFKILL_TYPE_BIT(type) (0x01UL << (type))

/* hw/goldfish_bt.c */
CharDriverState* goldfish_bt_new_cs (struct HCIInfo *hci);
bool             goldfish_bt_add_remote (char *address);
void             goldfish_bt_remove_remote (char *address);
bool             goldfish_bt_set_remote_property (
                     char *address, char *property, char *value);
bool             goldfish_bt_get_property (
                     char *address, char *property, char *ret);

/* hw/goldfish_rfkill.c */
uint32_t android_rfkill_get_blocking();
uint32_t android_rfkill_get_hardware_block();
void     android_rfkill_set_hardware_block(uint32_t hw_block);

#endif // GOLDFISH_BT_H
