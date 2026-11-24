#include "pci.h"
#include "io.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
static u32 pci_read32(u8 bus, u8 slot, u8 fn, u8 offset) {
    u32 address = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
                  ((u32)fn << 8) | ((u32)offset & 0xFCu);
    outl(PCI_ADDR, address);
    return inl(PCI_DATA);
}
static void pci_write32(u8 bus, u8 slot, u8 fn, u8 offset, u32 value) {
    u32 address = 0x80000000u | ((u32)bus << 16) | ((u32)slot << 11) |
                  ((u32)fn << 8) | ((u32)offset & 0xFCu);
    outl(PCI_ADDR, address); outl(PCI_DATA, value);
}
static int pci_read_device(u8 bus, u8 slot, u8 fn, pci_device_t *out) {
    u32 id = pci_read32(bus, slot, fn, 0);
    u32 classreg;
    if ((id & 0xFFFFu) == 0xFFFFu) return 0;
    classreg = pci_read32(bus, slot, fn, 8);
    out->bus=bus; out->slot=slot; out->function=fn;
    out->vendor_id=(u16)id; out->device_id=(u16)(id>>16);
    out->revision=(u8)classreg; out->programming_interface=(u8)(classreg>>8);
    out->subclass=(u8)(classreg>>16); out->class_code=(u8)(classreg>>24);
    for (u8 i=0;i<6;i++) out->bar[i]=pci_read32(bus,slot,fn,(u8)(0x10+i*4));
    return 1;
}
static int pci_scan(u16 vendor,u16 device,int by_class,u8 class_code,u8 subclass,pci_device_t *out) {
    if (!out) return -1;
    for (u16 bus=0;bus<256;bus++) for (u8 slot=0;slot<32;slot++) {
        pci_device_t d;
        if (!pci_read_device((u8)bus,slot,0,&d)) continue;
        u32 header=pci_read32((u8)bus,slot,0,0x0C);
        u8 functions=((header>>16)&0x80u)?8:1;
        for (u8 fn=0;fn<functions;fn++) {
            if (fn && !pci_read_device((u8)bus,slot,fn,&d)) continue;
            if (by_class ? (d.class_code==class_code && d.subclass==subclass)
                         : (d.vendor_id==vendor && d.device_id==device)) { *out=d; return 0; }
        }
    }
    return -1;
}
int pci_find_device(u16 vendor,u16 device,pci_device_t *out) { return pci_scan(vendor,device,0,0,0,out); }
int pci_find_class(u8 class_code,u8 subclass,pci_device_t *out) { return pci_scan(0,0,1,class_code,subclass,out); }
int pci_enable_bus_master(const pci_device_t *d) {
    u32 value;
    if (!d) return -1;
    value=pci_read32(d->bus,d->slot,d->function,4);
    value = (value & 0xFFFFu) | (1u<<1) | (1u<<2); /* avoid writing 1s to PCI status W1C bits */
    pci_write32(d->bus,d->slot,d->function,4,value);
    return 0;
}
