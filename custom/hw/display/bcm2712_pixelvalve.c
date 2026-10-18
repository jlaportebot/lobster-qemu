/*
 * BCM2712 Pixelvalve v2 display controller
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/display/bcm2712_fb.h"

#define TYPE_BCM2712_PIXELVALVE "bcm2712-pixelvalve"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712PixelValveState, BCM2712_PIXELVALVE)

struct BCM2712PixelValveState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

static void bcm2712_pixelvalve_realize(DeviceState *dev, Error **errp)
{
    BCM2712PixelValveState *s = BCM2712_PIXELVALVE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &bcm2712_pixelvalve_ops,
                          s, "bcm2712-pixelvalve", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void bcm2712_pixelvalve_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_pixelvalve_realize;
}

static const TypeInfo bcm2712_pixelvalve_type_info = {
    .name = TYPE_BCM2712_PIXELVALVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712PixelValveState),
    .class_init = bcm2712_pixelvalve_class_init,
};

static void bcm2712_pixelvalve_register_types(void)
{
    type_register_static(&bcm2712_pixelvalve_type_info);
}

type_init(bcm2712_pixelvalve_register_types);