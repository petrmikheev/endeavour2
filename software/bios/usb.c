#include <endeavour2/defs.h>
#include <endeavour2/bios.h>

static void* next_alloc = (void*)0x80080000;

void* malloc(unsigned long size) {
  void* res = next_alloc;
  next_alloc += (size + 3) & ~3;
  return res;
}

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

volatile static struct OHCICtrl* ctrl = (void*)USB_OHCI_BASE;
volatile static struct ED *default_ed, *end_ed;
volatile static struct TD* default_tds;

extern volatile struct HCCA ohci_hcca;

void uwait(int microseconds) {
  microseconds *= 10;
  unsigned time, start_time;
  asm volatile("csrr %0, time" : "=r" (start_time));
  do {
    ctrl->HcInterruptStatus = -1;
    asm volatile("csrr %0, time" : "=r" (time));
  } while (time - start_time < microseconds);
}

void init_hub() {
  ctrl->HcControl = 0x0;  // reset hub 50 ms
  uwait(50000);

  const unsigned frame_interval = 11999; // 1ms frame
  ctrl->HcFmInterval = (1<<31 /*FIT=1*/) | (((frame_interval-210)*6/7) << 16 /*FSLargestDataPackage*/) | frame_interval;
  ctrl->HcPeriodicStart = 10799; // 90% of FrameInterval
  ctrl->HcInterruptDisable = -1; // disable all

  default_ed = malloc(16);
  end_ed = malloc(16);
  default_tds = malloc(16 * 4);

  end_ed->flags = 1 << 14;
  end_ed->nextED = 0;
  end_ed->head = end_ed->tail = (long)&default_tds[2];

  for (int i = 0; i < 32; ++i) ohci_hcca.interrupt_table[i] = 0;//(long)end_ed;
  ctrl->HcHCCA = (long)&ohci_hcca;

  default_ed->head = default_ed->tail = (long)default_tds;
  default_ed->nextED = 0;

  ctrl->HcControlHeadED = (long)default_ed;
  ctrl->HcBulkHeadED = (long)end_ed;

  ctrl->HcControl = 0x80 | (1<<4) /*| (1<<3) | (1<<2)*/;  // operational state, allow only control transfers

  uwait(1000);  // wait 1ms to update port connection status

  // disable ports
  ctrl->HcRhPortStatus[0] = 1;
  ctrl->HcRhPortStatus[1] = 1;
}

int usb_request(volatile struct ED* ed, const volatile struct UsbRequest* request, volatile void* data) {
  default_tds[0].flags = 0xf2000000;
  default_tds[0].nextTD = (void*)&default_tds[1];
  default_tds[0].buf_start = (void*)request;
  default_tds[0].buf_end = (char*)request + sizeof(struct UsbRequest)-1;

  int read = (request->bmRequestType & 0x80) || data == 0;
  default_tds[1].flags = 0xf3040000 | (read ? 2<<19 : 1<<19);
  default_tds[1].nextTD = (void*)&default_tds[2];
  default_tds[1].buf_start = (void*)data;
  default_tds[1].buf_end = (void*)data + request->wLength - 1;

  ed->head = (long)&default_tds[0];
  ed->tail = (long)&default_tds[2];

  //ctrl->HcDoneHead = (long)&default_tds[2];
  //hcca->done_head = (long)&default_tds[2];
  ctrl->HcCommandStatus = 2;

  uwait(100000);
  //while ((ctrl->HcDoneHead != (long)&default_tds[1] && ohci_hcca.done_head != (long)&default_tds[1]) || ctrl->HcCommandStatus != 0) uwait(10);

  if ((ctrl->HcCommandStatus&0xffff) == 0 && (ed->head & 1) == 0 && (default_tds[0].flags>>28) == 0 && (default_tds[1].flags>>28) == 0) {
    return 0;
  }

  bios_printf("ERR cstatus=%x ed.flags=%8x %d td[0].flags=%8x td[1].flags=%8x\n", ctrl->HcCommandStatus, ed->flags, (unsigned)ed->head&3, default_tds[0].flags, default_tds[1].flags);
  return -1;
}

int get_report(volatile struct ED* ed, volatile void* data) {
  default_tds[3].flags = 0xf2000000 | (2<<19);
  default_tds[3].nextTD = (void*)&default_tds[2];
  default_tds[3].buf_start = (void*)data;
  default_tds[3].buf_end = (void*)data + 7;

  ed->nextED = 0;//(void*)end_ed;
  ed->head = (long)&default_tds[3];
  ed->tail = (long)&default_tds[2];

  //for (int i=0;i<32;++i) hcca->interrupt_table[i] = (long)ed;
  ctrl->HcControlHeadED = (long)ed;
  ctrl->HcCommandStatus = 2;
  //hcca->interrupt_table[0] = (long)ed;
  //ctrl->HcHCCA = (long)hcca;
  //while ((default_tds[0].flags>>28) == 0xf) uwait(10);
uwait(1000000);
  /*ctrl->HcDoneHead = (long)&default_tds[2];
  hcca->done_head = (long)&default_tds[2];
  ctrl->HcCommandStatus = 2;
  while ((ctrl->HcDoneHead != (long)&default_tds[0] && hcca->done_head != (long)&default_tds[0]) || ctrl->HcCommandStatus != 0) uwait(10);*/

  /*if ((ed->head & 1) == 0 && (default_tds[0].flags>>28) == 0) {
    return 0;
  }*/
  bios_printf("R cstatus=%x ed.flags=%8x head=%8x tail=%8x td.flags=%8x\n", ctrl->HcCommandStatus, ed->flags, ed->head, ed->tail, default_tds[3].flags);
  return -1;
}

int reset_port(int p) {
  ctrl->HcRhPortStatus[p] = 1 << 4;
  uwait(100000);
  unsigned pstate = ctrl->HcRhPortStatus[p];
  if ((pstate&3) != 3) return -1;

  // max 8 byte, get direction from TD, USB address = 0, endpoint number = 0
  if (pstate & (1<<9))
    default_ed->flags = (8 << 16) | (1 << 13); // low speed
  else
    default_ed->flags = (8 << 16); // full speed

  struct UsbRequest request;
  struct UsbDeviceDescriptor descr;

  request.bmRequestType = 0x80;
  request.bRequest = 6; // GET_DESCRIPTOR
  request.wValue = 1 << 8; // device descriptor
  request.wIndex = 0;
  request.wLength = 8;

  if (usb_request(default_ed, &request, &descr) < 0) {
    ctrl->HcRhPortStatus[p] = 1;
    return -1;
  }

  default_ed->flags = ((unsigned)descr.bMaxPacketSize0 << 16) | (default_ed->flags & 0xffff);
  return 0;
}

void init_usb() {
  init_hub();

  for (int port = 0; port < 2; ++port) {
    bios_printf("USB%u: ", port + 1);
    if (!(ctrl->HcRhPortStatus[port] & 1)) {
      bios_printf("not connected\n");
      continue;
    }

    if (reset_port(port) < 0) {
      goto test_port_error;
    }

    struct UsbRequest request;
    struct UsbDeviceDescriptor ddev;

    request.bmRequestType = 0x0;
    request.bRequest = 5; // SET_ADDRESS
    request.wValue = 1; // new address
    request.wIndex = 0;
    request.wLength = 0;

    if (usb_request(default_ed, &request, 0) < 0) goto test_port_error;

    default_ed->flags |= 1; // new address

    request.bmRequestType = 0x80;
    request.bRequest = 6; // GET_DESCRIPTOR
    request.wValue = 1 << 8; // device descriptor
    request.wIndex = 0;
    request.wLength = sizeof(struct UsbDeviceDescriptor);

    if (usb_request(default_ed, &request, &ddev) < 0) goto test_port_error;

    char dstr[256];

    request.wValue = (3 << 8 /*string descriptor*/) | ddev.iManufacturer;
    request.wLength = 1;
    if (usb_request(default_ed, &request, &dstr) < 0) goto test_port_error;
    request.wLength = dstr[0];
    if (usb_request(default_ed, &request, &dstr) < 0) goto test_port_error;
    for (int i = 2; i < request.wLength; i += 2) bios_putchar(dstr[i]);
    bios_putchar(' ');

    request.wValue = (3 << 8 /*string descriptor*/) | ddev.iProduct;
    request.wLength = 1;
    if (usb_request(default_ed, &request, &dstr) < 0) goto test_port_error;
    request.wLength = dstr[0];
    if (usb_request(default_ed, &request, &dstr) < 0) goto test_port_error;
    for (int i = 2; i < request.wLength; i += 2) bios_putchar(dstr[i]);
    bios_putchar('\n');
#if 0
    bios_printf("bDeviceClass=%02x bDeviceSubClass=%02x bDeviceProtocol=%02x bNumConf=%02x\n", ddev.bDeviceClass, ddev.bDeviceSubClass, ddev.bDeviceProtocol, ddev.bNumConfigurations);

    struct UsbConfigurationDescriptor dconf;

    request.bmRequestType = 0x80;
    request.bRequest = 6; // GET_DESCRIPTOR
    request.wValue = (2 << 8 /* configuration*/);
    request.wIndex = 0;
    request.wLength = sizeof(struct UsbConfigurationDescriptor);

    if (usb_request(default_ed, &request, &dconf) < 0) goto test_port_error;

    bios_printf("len=%d wTotalLength=%d bNumInterfaces=%d bConfVal=%d iConf=%d\n", dconf.bLength, dconf.wTotalLength, dconf.bNumInterfaces, dconf.bConfigurationValue, dconf.iConfiguration);
    bios_printf("bmAttributes=0x%02x bMaxPower=%d ma\n", dconf.bmAttributes, dconf.bMaxPower * 2);

    request.wLength = dconf.wTotalLength;
    char* cdata = malloc(dconf.wTotalLength);
    if (usb_request(default_ed, &request, cdata) < 0) goto test_port_error;

    for (int i = 0; i < dconf.wTotalLength; ++i) {
      bios_printf("%02x ", cdata[i]);
      if ((i&7)==7) bios_putchar('\n');
    }
    bios_putchar('\n');

    char* cend = cdata + dconf.wTotalLength;
    cdata += dconf.bLength;

    for (int infid=0; infid<dconf.bNumInterfaces; ++infid) {
      while (cdata < cend && cdata[1] != 4) {
        bios_printf("Unknown size=%d dt=%d\n", cdata[0], cdata[1]);
        cdata += cdata[0];
      }
      if (cdata >= cend) break;
      struct UsbInterfaceDescriptor* inf = (void*)cdata;
      bios_printf("\tInterface %d: len=%d dt=%d class=%d:%d protocol=%d str=%d\n", infid, inf->bLength, inf->bDescriptorType, inf->bInterfaceClass, inf->bInterfaceSubClass, inf->bInterfaceProtocol, inf->iInterface);
      cdata += inf->bLength;

      for (int ei=0; ei<inf->bNumEndpoints; ++ei) {
        while (cdata < cend && cdata[1] != 5) {
          bios_printf("\t\tUnknown size=%d dt=%d\n", cdata[0], cdata[1]);
          cdata += cdata[0];
        }
        struct UsbEndpointDescriptor* enp = (void*)cdata;
        bios_printf("\t\tEndpoint %d: len=%d dt=%d eaddr=%d psize=%d attrs=0x%02x interval=%d\n", ei, enp->bLength, enp->bDescriptorType, enp->bEndpointAddress, enp->wMaxPacketSize, enp->bmAttributes, enp->bInterval);
        cdata += enp->bLength;
      }
    }
bios_printf("set_conf\n");
    request.bmRequestType = 0x0;
    request.bRequest = 9; // SET_CONFIGURATION
    request.wValue = dconf.bConfigurationValue; // new configuration
    request.wIndex = 0;
    request.wLength = 0;

    if (usb_request(default_ed, &request, 0) < 0) goto test_port_error;
bios_printf("set_protocol\n");
    request.bmRequestType = 0x21;
    request.bRequest = 0xb; // SET_PROTOCOL
    request.wValue = 0; // boot protocol
    request.wIndex = 0;
    request.wLength = 0;

    if (usb_request(default_ed, &request, 0) < 0) goto test_port_error;

    /*request.bmRequestType = 0xa1;
    request.bRequest = 0x2; // GET_IDLE
    request.wValue = 0;
    request.wIndex = 0;
    request.wLength = 1;
    unsigned char idle_rate;

    if (usb_request(default_ed, &request, &idle_rate) < 0) goto test_port_error;
    bios_printf("idle rate: %u\n", idle_rate);*/

    /*request.bmRequestType = 0x21;
    request.bRequest = 0xa; // SET_IDLE
    request.wValue = 0x100; // 4ms
    request.wIndex = 0;
    request.wLength = 0;
    if (usb_request(default_ed, &request, 0) < 0) goto test_port_error;*/

    request.bmRequestType = 0xa1;
    request.bRequest = 1; // GET_REPORT
    request.wValue = 0x101; //0x100; // 0x101
    request.wIndex = 0;
    request.wLength = 8;
bios_printf("get report\n");
#if 1
uwait(1000000);
    //volatile struct ED* in_ed = malloc(sizeof(struct ED));
    //in_ed->flags = (8 << 16/*max 8 bytes*/) /*| (2<<11)*/ | (default_ed->flags & (1 << 13)/*speed*/) | 1 /*addr*/ | (1 << 7 /*endpoint 1*/);
    volatile char report[8];
    for (int i = 0; i < 5; ++i) {
      for (int j = 0; j < 8; ++j) report[j] = 0xff;
      //get_report(in_ed, report);
      uwait(1000000);

      if (usb_request(default_ed, &request, report) < 0) goto test_port_error;
      //get_report(in_ed, report);// == 0 || 1) {
        for (int j = 0; j < 8; ++j) {
          bios_printf("%02x ", report[j]);
        }
        bios_putchar('\n');
      //}
    }
#endif
#endif
    goto test_port_ok;
test_port_error:
    bios_printf("error\n");
test_port_ok:
    ctrl->HcRhPortStatus[port] = 1;  // disable
  }
}