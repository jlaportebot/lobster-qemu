/*
 * BCM2712 HDMI - stub
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"

#define TYPE_BCM2712_HDMI "bcm2712-hdmi"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712HDMIState, BCM2712_HDMI)

struct BCM2712HDMIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

static void bcm2712_hdmi_realize(DeviceState *dev, Error **errp)
{
    BCM2712HDMIState *s = BCM2712_HDMI(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &unimplemented_device_ops,
                          s, "bcm2712-hdmi", 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mmio); /* dummy */
}

static void bcm2712_hdmi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_hdmi_realize;
}

static const TypeInfo bcm2712_hdmi_type_info = {
    .name = TYPE_BCM2712_HDMI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712HDMIState),
    .class_init = bcm2712_hdmi_class_init,
};

static void bcm2712_hdmi_register_types(void)
{
    type_register_static(&bcm2712_hdmi_type_info);
}

type_init(bcm2712_hdmi_register_types);