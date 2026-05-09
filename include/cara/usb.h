// SPDX-License-Identifier: BSD-2-Clause
//
// USB protocol — wire-level structures and constants per USB 2.0 §9.
// Brand-namespace surface; consumed by the xHCI driver (UC.* control
// transfers) and the HID Gleas (Tier 3). This file is cleanroom from
// the USB 2.0 specification (`usb_20.pdf` §9 "USB Device Framework").

#ifndef CARA_USB_H
#define CARA_USB_H

#include <cara/types.h>

// ---- SETUP packet (USB 2.0 §9.3) ------------------------------------------
//
// Every standard USB control transfer begins with an 8-byte SETUP. The
// xHCI Setup Stage TRB carries this payload immediately (IDT=1).

struct CARA_PACKED UsbSetupPacket {
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
};

// bmRequestType bit layout (§9.3.1).
enum {
    USB_DIR_HOST_TO_DEVICE = 0x00,
    USB_DIR_DEVICE_TO_HOST = 0x80,
    USB_TYPE_STANDARD = 0x00,
    USB_TYPE_CLASS = 0x20,
    USB_TYPE_VENDOR = 0x40,
    USB_RECIP_DEVICE = 0x00,
    USB_RECIP_INTERFACE = 0x01,
    USB_RECIP_ENDPOINT = 0x02,
};

// Standard request codes (§9.4 / Table 9-4).
enum {
    USB_REQ_GET_STATUS = 0,
    USB_REQ_CLEAR_FEATURE = 1,
    USB_REQ_SET_FEATURE = 3,
    USB_REQ_SET_ADDRESS = 5,
    USB_REQ_GET_DESCRIPTOR = 6,
    USB_REQ_SET_DESCRIPTOR = 7,
    USB_REQ_GET_CONFIGURATION = 8,
    USB_REQ_SET_CONFIGURATION = 9,
    USB_REQ_GET_INTERFACE = 10,
    USB_REQ_SET_INTERFACE = 11,
    USB_REQ_SYNCH_FRAME = 12,
};

// ---- Standard descriptor types (§9.4 / Table 9-5) -------------------------

enum {
    USB_DT_DEVICE = 1,
    USB_DT_CONFIGURATION = 2,
    USB_DT_STRING = 3,
    USB_DT_INTERFACE = 4,
    USB_DT_ENDPOINT = 5,
    USB_DT_DEVICE_QUALIFIER = 6,
    USB_DT_OTHER_SPEED_CONFIG = 7,
    USB_DT_INTERFACE_POWER = 8,
    USB_DT_HID = 0x21,
    USB_DT_HID_REPORT = 0x22,
};

// ---- Standard descriptor structs (USB 2.0 §9.6) ---------------------------
//
// All multi-byte fields are little-endian on the wire (USB §8.1). The
// kernel runs little-endian RV64, so plain u16 reads/writes are correct.

struct CARA_PACKED UsbDeviceDescriptor {
    u8 bLength;         // = 18
    u8 bDescriptorType; // = USB_DT_DEVICE (1)
    u16 bcdUSB;         // BCD USB spec version (e.g. 0x0200)
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0; // EP0 max packet size: 8/16/32/64 (§9.6.1)
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer; // string descriptor index, 0 = none
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
};

constexpr u32 USB_DEVICE_DESCRIPTOR_BYTES = 18;

struct CARA_PACKED UsbConfigurationDescriptor {
    u8 bLength;         // = 9
    u8 bDescriptorType; // = USB_DT_CONFIGURATION (2)
    u16 wTotalLength;   // length of (config + interfaces + endpoints)
    u8 bNumInterfaces;
    u8 bConfigurationValue;
    u8 iConfiguration;
    u8 bmAttributes;
    u8 bMaxPower; // in 2 mA units
};

struct CARA_PACKED UsbInterfaceDescriptor {
    u8 bLength;         // = 9
    u8 bDescriptorType; // = USB_DT_INTERFACE (4)
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
};

enum {
    USB_CLASS_HID = 0x03,
    USB_HID_SUBCLASS_BOOT = 0x01,
    USB_HID_PROTOCOL_KEYBOARD = 0x01,
    USB_HID_PROTOCOL_MOUSE = 0x02,
};

// USB HID 1.11 §7.2 class-specific requests. Boot subclass devices
// also accept SET_PROTOCOL to switch between the canonical 8-byte
// boot-protocol report layout (value=0) and a HID Report Descriptor
// driven layout (value=1). Phase 1 forces Boot so we don't need a
// Report Descriptor parser yet.
enum {
    USB_HID_REQ_GET_REPORT = 0x01,
    USB_HID_REQ_GET_IDLE = 0x02,
    USB_HID_REQ_GET_PROTOCOL = 0x03,
    USB_HID_REQ_SET_REPORT = 0x09,
    USB_HID_REQ_SET_IDLE = 0x0A,
    USB_HID_REQ_SET_PROTOCOL = 0x0B,
    USB_HID_PROTOCOL_BOOT = 0x00,
    USB_HID_PROTOCOL_REPORT = 0x01,
};

struct CARA_PACKED UsbEndpointDescriptor {
    u8 bLength;          // = 7 (or 9 with audio extensions)
    u8 bDescriptorType;  // = USB_DT_ENDPOINT (5)
    u8 bEndpointAddress; // bits 3:0 = number, bit 7 = direction (1 = IN)
    u8 bmAttributes;     // bits 1:0 = transfer type
    u16 wMaxPacketSize;  // bits 10:0 = max packet size in bytes
    u8 bInterval;        // poll interval (interrupt/isoch)
};

enum {
    USB_EP_DIR_IN = 0x80,
    USB_EP_TYPE_CONTROL = 0x00,
    USB_EP_TYPE_ISOCH = 0x01,
    USB_EP_TYPE_BULK = 0x02,
    USB_EP_TYPE_INTERRUPT = 0x03,
    USB_EP_TYPE_MASK = 0x03,
};

#endif
