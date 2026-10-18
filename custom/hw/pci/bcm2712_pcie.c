/*
 * BCM2712 PCIe Root Complex - stub
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"

#define TYPE_BCM2712_PCIE "bcm2712-pcie"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712PCIEState, BCM2712_PCIE)

struct BCM2712PCIEState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    PCIHostState host;
    PCIBus *bus;
};

static void bcm2712_pcie_realize(DeviceState *dev, Error **errp)
{
    BCM2712PCIEState *s = BCM2712_PCIE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &unimplemented_device_ops,
                          s, "bcm2712-pcie", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    pci_host_init(&s->host, dev, &bcm2712_pcie_ops, 0, 0x100000000ULL);
    s->bus = pci_register_bus(dev, "bcm2712-pcie", &s->host, 0, NULL, NULL);
}

static void bcm2712_pcie_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_pcie_realize;
}

static void bcm2712_pcie_register_types(void)
{
    static const TypeInfo type_info = {
        .name = TYPE_BCM2712_PCIE,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(BCM2712PCIEState),
        .class_init = bcm2712_pcie_class_init,
    };
    type_register_static(&type_info);
}

type_init(bcm2712_pcie_register_types);