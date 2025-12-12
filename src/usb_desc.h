#ifndef USB_DESC_H
#define USB_DESC_H

#include "CH58x_common.h"

// Standard HID IDs
#define USBD_VID 0x1A86  // WCH Vendor ID
#define USBD_PID 0xFE07  // Generic HID Product ID

extern const uint8_t MyDevDescr[];
extern const uint8_t MyCfgDescr[];
extern const uint8_t KeyRepDesc[];

#endif