#ifndef GOLDFISH_BT_H
#define GOLDFISH_BT_H

#include "bt-host.h" // struct HCIInfo;

enum RfkillTypes {
    RFKILL_TYPE_WLAN = 0,
    RFKILL_TYPE_BLUETOOTH,
    RFKILL_TYPE_UWB,
    RFKILL_TYPE_WIMAX,
    RFKILL_TYPE_WWAN,

    RFKILL_TYPE_MAX
};

/* hw/goldfish_bt.c */
CharDriverState* goldfish_bt_new_cs (struct HCIInfo *hci);
const char* goldfish_bt_get(const char *query, char *result);
void goldfish_bt_radd(char *str);
void goldfish_bt_rclr();

/* hw/goldfish_rfkill.c */
uint32_t android_rfkill_get_blocking();
uint32_t android_rfkill_get_hardware_block();
void     android_rfkill_set_hardware_block(uint32_t hw_block);

#endif // GOLDFISH_BT_H
