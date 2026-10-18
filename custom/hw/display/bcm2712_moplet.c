/*
 * BCM2712 MOPLET - stub
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"

#define TYPE_BCM2712_MOPLET "bcm2712-moplet"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712MOPLETState, BCM2712_MOPLET)

struct BCM2712MOPLETState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

static void bcm2712_moplet_realize(DeviceState *dev, Error **errp)
{
    BCM2712MOPLETState *s = BCM2712_MOPLET(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &unimplemented_device_ops,
                          s, "bcm2712-moplet", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void bcm2712_moplet_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_moplet_realize;
}

static const TypeInfo bcm2712_moplet_type_info = {
    .name = TYPE_BCM2712_MOPLET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712MOPLETState),
    .class_init = bcm2712_moplet_class_init,
};

static void bcm2712_moplet_register_types(void)
{
    type_register_static(&bcm2712_moplet_type_info);
}

type_init(bcm2712_moplet_register_types);