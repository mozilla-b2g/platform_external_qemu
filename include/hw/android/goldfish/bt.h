#ifndef GOLDFISH_BT_H
#define GOLDFISH_BT_H

#include "hw/bt.h" // struct HCIInfo;

/* hw/goldfish_bt.c */
CharDriverState* goldfish_bt_new_cs (struct HCIInfo *hci);

#endif // GOLDFISH_BT_H
