/*
 * BCM2712 MOP (Memory Output Port) - stub
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"

#define TYPE_BCM2712_MOP "bcm2712-mop"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712MOPState, BCM2712_MOP)

struct BCM2712MOPState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

static void bcm2712_mop_realize(DeviceState *dev, Error **errp)
{
    BCM2712MOPState *s = BCM2712_MOP(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &unimplemented_device_ops,
                          s, "bcm2712-mop", 0x28);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void bcm2712_mop_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_mop_realize;
}

static const TypeInfo bcm2712_mop_type_info = {
    .name = TYPE_BCM2712_MOP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712MOPState),
    .class_init = bcm2712_mop_class_init,
};

static void bcm2712_mop_register_types(void)
{
    type_register_static(&bcm2712_mop_type_info);
}

type_init(bcm2712_mop_register_types);