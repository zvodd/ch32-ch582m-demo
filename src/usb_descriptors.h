#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include "usb_defs.h"

// --- USB Descriptors (Adapted for CH582M) ---
// Note: We are switching back to a standard 8-byte Keyboard Report Descriptor.

const uint8_t MyDevDescr[] = {
    0x12,       // bLength
    0x01,       // bDescriptorType = Device
    0x10, 0x01, // bcdUSB = 1.10
    0x00,       // bDeviceClass (defined per-interface)
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    DevEP0SIZE, // bMaxPacketSize0 (usually 8 or 64)
    0x34, 0x12, // idVendor (example generic VID 0x1234)
    0x78, 0x56, // idProduct (example generic PID 0x5678)
    0x00, 0x01, // bcdDevice
    0x01,       // iManufacturer
    0x02,       // iProduct
    0x03,       // iSerialNumber
    0x01        // bNumConfigurations
};

// Configuration Descriptor (Keyboard with 1 endpoint)
const uint8_t MyCfgDescr[] = {
    // --- Configuration Header ---
    0x09,       // bLength
    0x02,       // bDescriptorType = Configuration
    0x22, 0x00, // wTotalLength = 34 bytes
    0x01,       // bNumInterfaces
    0x01,       // bConfigurationValue
    0x00,       // iConfiguration
    0xA0,       // bmAttributes = Bus powered + Remote Wakeup
    0x32,       // bMaxPower = 100 mA

    // --- Interface 0: HID Boot Keyboard ---
    0x09,       // bLength
    0x04,       // bDescriptorType = Interface
    0x00,       // bInterfaceNumber
    0x00,       // bAlternateSetting
    0x01,       // bNumEndpoints = 1
    0x03,       // bInterfaceClass = HID
    0x01,       // bInterfaceSubClass = Boot
    0x01,       // bInterfaceProtocol = Keyboard
    0x00,       // iInterface

    // --- HID Descriptor ---
    0x09,       // bLength
    0x21,       // bDescriptorType = HID
    0x11, 0x01, // bcdHID = 1.11
    0x00,       // bCountryCode = Not localized
    0x01,       // bNumDescriptors
    0x22,       // bDescriptorType = Report
    0x3F, 0x00, // wDescriptorLength = 63 bytes (fixed below)

    // --- Endpoint Descriptor (IN interrupt) ---
    0x07,       // bLength
    0x05,       // bDescriptorType = Endpoint
    0x81,       // bEndpointAddress = IN endpoint #1
    0x03,       // bmAttributes = Interrupt
    0x08, 0x00, // wMaxPacketSize = 8 bytes
    0x0A        // bInterval = 10 ms
};

// Standard HID Keyboard Report Descriptor (8-byte report)
const uint8_t MyHIDReportDescr[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Keyboard)(Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Var, Abs) ; Modifier byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Const) ; Reserved byte
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (1)
    0x29, 0x05,  //   Usage Maximum (5)
    0x91, 0x02,  //   Output (Data, Var, Abs) ; 5 LEDs
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Const) ; LED padding
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Keyboard)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) ; 6 keycodes
    0xC0         // End Collection
};

// String Descriptors
const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 }; // Language 0x0409 (US English)
const uint8_t MyManuInfo[] = { 0x10, 0x03,'G',0,'e',0,'n',0,'e',0,'r',0,'i',0,'c',0 };

const uint8_t MyProdInfo[] = { 0x12, 0x03,'U',0,'S',0,'B',0,' ',0,'K',0,'e',0,'y',0,'b',0,'d',0 };

#endif