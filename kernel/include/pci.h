#ifndef DENOS_PCI_H
#define DENOS_PCI_H
#include "types.h"
typedef struct pci_device {
    u8 bus, slot, function;
    u16 vendor_id, device_id;
    u8 class_code, subclass, programming_interface, revision;
    u32 bar[6];
} pci_device_t;
int pci_find_device(u16 vendor, u16 device, pci_device_t *out);
int pci_find_class(u8 class_code, u8 subclass, pci_device_t *out);
int pci_enable_bus_master(const pci_device_t *device);
#endif
