#include <endeavour2/raw/defs.h>

#include "bios_internal.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

struct TD {
  unsigned flags;
  void* buf_start;
  struct TD* nextTD;
  void* buf_end;
};

struct ED {
  unsigned flags;
  unsigned tail;
  unsigned head;
  struct ED *nextED;
};

struct HCCA {
  unsigned interrupt_table[32];
  short frame_number;
  short pad1;
  int done_head;
};

static volatile struct ED ed __attribute__((aligned(16)));
static volatile struct TD tds[2] __attribute__((aligned(16)));
extern volatile struct HCCA ohci_hcca;

static char* tmp_buf = (void*)(RAM_BASE + BIOS_SIZE);

struct UsbRequest {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
};

#pragma pack(1)
struct UsbDeviceDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
};

struct UsbConfigurationDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
};

struct UsbInterfaceDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
};

struct UsbEndpointDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};
#pragma pack()

static void init_hub() {
  USB_OHCI_REGS->HcControl = 0x0;  // reset hub 50 ms
  wait(500000);

  const unsigned frame_interval = 11999; // 1ms frame
  USB_OHCI_REGS->HcFmInterval = (1<<31 /*FIT=1*/) | (((frame_interval-210)*6/7) << 16 /*FSLargestDataPackage*/) | frame_interval;
  USB_OHCI_REGS->HcPeriodicStart = 10799; // 90% of FrameInterval
  USB_OHCI_REGS->HcInterruptDisable = -1; // disable all

  USB_OHCI_REGS->HcHCCA = (long)&ohci_hcca;  // already zeroed in bss
  USB_OHCI_REGS->HcControlHeadED = 0;
  USB_OHCI_REGS->HcBulkHeadED = 0;

  USB_OHCI_REGS->HcControl = 0x80 | (1<<4);

  wait(10000);  // wait 1ms to update port connection status

  // disable ports
  USB_OHCI_REGS->HcRhPortStatus[0] = 1;
  USB_OHCI_REGS->HcRhPortStatus[1] = 1;
}

static int process_tds(int td_count) {
  unsigned last = (long)&tds[td_count - 1];

  ed.nextED = 0;
  ed.head = (long)&tds[0];
  ed.tail = 0;

  wait(10);  // wait a bit to make sure that changes to ED are now visible to the controller
  USB_OHCI_REGS->HcControlHeadED = (long)&ed;
  USB_OHCI_REGS->HcCommandStatus = 2;

  unsigned start = time_100nsec();
  while (1) {
    wait(10);
    if ((USB_OHCI_REGS->HcInterruptStatus & 2) == 0) continue;
    unsigned done_head = ohci_hcca.done_head & ~15;
    USB_OHCI_REGS->HcInterruptStatus = 2;
    if (done_head == last || time_100nsec() - start > 5000000) break;
  }
  USB_OHCI_REGS->HcControlHeadED = 0;

  if ((tds[td_count-1].flags >> 28) == 0) return 0;

  //printf("ERR cstatus=%x ed.flags=%8x %d td[0].flags=%8x td[1].flags=%8x\n", USB_OHCI_REGS->HcCommandStatus, ed.flags, (unsigned)ed.head&3, tds[0].flags, tds[1].flags);
  return -1;
}

static int usb_request(const volatile struct UsbRequest* request, volatile void* data) {
  tds[0].flags = 0xf2000000;
  tds[0].nextTD = (void*)&tds[1];
  tds[0].buf_start = (void*)request;
  tds[0].buf_end = (char*)request + sizeof(struct UsbRequest)-1;

  int read = (request->bmRequestType & 0x80) || data == 0;
  tds[1].flags = 0xf3040000 | (read ? 2<<19 : 1<<19);
  tds[1].nextTD = 0;
  tds[1].buf_start = (void*)data;
  tds[1].buf_end = (void*)data + request->wLength - 1;

  return process_tds(2);
}

static unsigned keyboard_initialized = 0;

int get_keyboard_report(volatile struct KeyboardReport* data) {
  if (!keyboard_initialized) return -1;

  tds[0].flags = 0xf2000000 | (2<<19);
  tds[0].nextTD = 0;
  tds[0].buf_start = (void*)data;
  tds[0].buf_end = (void*)data + 7;

  return process_tds(1);
}


static int reset_port(int p) {
  USB_OHCI_REGS->HcRhPortStatus[p] = 1 << 4;
  wait(1000000);
  unsigned pstate = USB_OHCI_REGS->HcRhPortStatus[p];
  if ((pstate&3) != 3) return -1;

  // max 8 byte, get direction from TD, USB address = 0, endpoint number = 0
  if (pstate & (1<<9))
    ed.flags = (8 << 16) | (1 << 13); // low speed
  else
    ed.flags = (8 << 16); // full speed

  struct UsbRequest request;
  struct UsbDeviceDescriptor descr;

  request.bmRequestType = 0x80;
  request.bRequest = 6; // GET_DESCRIPTOR
  request.wValue = 1 << 8; // device descriptor
  request.wIndex = 0;
  request.wLength = 8;

  if (usb_request(&request, &descr) < 0) {
    USB_OHCI_REGS->HcRhPortStatus[p] = 1;
    return -1;
  }

  ed.flags = ((unsigned)descr.bMaxPacketSize0 << 16) | (ed.flags & 0xffff);
  return 0;
}

static int print_string_descriptor(unsigned id) {
  struct UsbRequest request;
  request.bmRequestType = 0x80;
  request.bRequest = 6; // GET_DESCRIPTOR
  request.wIndex = 0;
  request.wValue = (3 << 8 /*string descriptor*/) | id;
  request.wLength = 1;
  if (usb_request(&request, tmp_buf) < 0) return -1;
  request.wLength = tmp_buf[0];
  if (usb_request(&request, tmp_buf) < 0) return -1;
  for (int i = 2; i < request.wLength; i += 2) putchar(tmp_buf[i]);
  return 0;
}

void init_usb_keyboard() {
  init_hub();

  unsigned keyboard_in_endpoint_flags = 0;

  for (int port = 0; port < 2; ++port) {
    printf("USB%u: ", port + 1);
    if (!(USB_OHCI_REGS->HcRhPortStatus[port] & 1)) {
      printf("not connected\n");
      continue;
    }

    if (reset_port(port) < 0) {
      goto port_error;
    }

    struct UsbRequest request;
    struct UsbDeviceDescriptor ddev;

    request.bmRequestType = 0x0;
    request.bRequest = 5; // SET_ADDRESS
    request.wValue = port + 1; // new address
    request.wIndex = 0;
    request.wLength = 0;
    if (usb_request(&request, 0) < 0) goto port_error;

    ed.flags |= port + 1; // new address

    request.bmRequestType = 0x80;
    request.bRequest = 6; // GET_DESCRIPTOR
    request.wValue = 1 << 8; // device descriptor
    request.wIndex = 0;
    request.wLength = sizeof(struct UsbDeviceDescriptor);
    if (usb_request(&request, &ddev) < 0) goto port_error;

    print_string_descriptor(ddev.iManufacturer);
    putchar(' ');
    print_string_descriptor(ddev.iProduct);

    struct UsbConfigurationDescriptor dconf;

    request.bmRequestType = 0x80;
    request.bRequest = 6; // GET_DESCRIPTOR
    request.wValue = (2 << 8 /* configuration*/);
    request.wIndex = 0;
    request.wLength = sizeof(struct UsbConfigurationDescriptor);

    if (usb_request(&request, &dconf) < 0) goto port_error;

    request.wLength = dconf.wTotalLength;
    char* cdata = tmp_buf;
    if (usb_request(&request, cdata) < 0) goto port_error;

    char* cend = cdata + dconf.wTotalLength;
    cdata += dconf.bLength;

    int use_as_keyboard = 0;

    for (int infid=0; infid<dconf.bNumInterfaces; ++infid) {
      while (cdata < cend && cdata[1] != 4) cdata += cdata[0];
      if (cdata >= cend) break;
      struct UsbInterfaceDescriptor* inf = (void*)cdata;
      int boot_keyboard = inf->bInterfaceClass == 3 && inf->bInterfaceSubClass == 1 && inf->bInterfaceProtocol == 1;

      //printf("\tInterface %d: len=%d dt=%d class=%d:%d protocol=%d str=%d\n", infid, inf->bLength, inf->bDescriptorType,
      //        inf->bInterfaceClass, inf->bInterfaceSubClass, inf->bInterfaceProtocol, inf->iInterface);
      cdata += inf->bLength;

      for (int ei=0; ei<inf->bNumEndpoints; ++ei) {
        while (cdata < cend && cdata[1] != 5) cdata += cdata[0];
        struct UsbEndpointDescriptor* enp = (void*)cdata;
        if (keyboard_in_endpoint_flags == 0 && boot_keyboard && (enp->bmAttributes&3)==3 && (enp->bEndpointAddress&128)) {
          use_as_keyboard = 1;
          keyboard_in_endpoint_flags = ((unsigned)enp->wMaxPacketSize << 16) /*| (2<<11)*/
              | (ed.flags & (1 << 13)/*speed*/) | (port+1 /*addr*/) | ((enp->bEndpointAddress&15) << 7);
        }

        //printf("\t\tEndpoint %d: len=%d dt=%d eaddr=%d psize=%d attrs=0x%02x interval=%d\n", ei, enp->bLength,
        //       enp->bDescriptorType, enp->bEndpointAddress, enp->wMaxPacketSize, enp->bmAttributes, enp->bInterval);
        cdata += enp->bLength;
      }
    }

    if (use_as_keyboard) {
      request.bmRequestType = 0x0;
      request.bRequest = 9; // SET_CONFIGURATION
      request.wValue = dconf.bConfigurationValue; // new configuration
      request.wIndex = 0;
      request.wLength = 0;
      if (usb_request(&request, 0) < 0) goto port_error;

      request.bmRequestType = 0x21;
      request.bRequest = 0xb; // SET_PROTOCOL
      request.wValue = 0; // boot protocol
      request.wIndex = 0;
      request.wLength = 0;
      if (usb_request(&request, 0) < 0) goto port_error;

      request.bmRequestType = 0x21;
      request.bRequest = 0xa; // SET_IDLE
      request.wValue = 0x400; // 16 ms
      request.wIndex = 0;
      request.wLength = 0;
      if (usb_request(&request, 0) < 0) goto port_error;

      printf(" [active keyboard]\n");
      keyboard_initialized = 1;
    } else {
      putchar('\n');
      USB_OHCI_REGS->HcRhPortStatus[port] = 1;  // disable port
    }
    continue;

port_error:
    printf("error\n");
    USB_OHCI_REGS->HcRhPortStatus[port] = 1;  // disable port
  }

  if (keyboard_initialized) ed.flags = keyboard_in_endpoint_flags;
}
