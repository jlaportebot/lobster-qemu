/*
 * BCM2712 MIP MSI Controller - minimal implementation
 *
 * Provides a simple MSI controller with configurable SPI base
 * and GPIO outputs to the GIC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "hw/arm/bcm2712_mip_msi.h"

static const MemoryRegionOps bcm2712_mip_msi_ops = {
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void bcm2712_mip_msi_init(Object *obj)
{
    BCM2712MIPMSIState *s = BCM2712_MIP_MSI(obj);

    memory_region_init_io(&s->mmio, obj, &bcm2712_mip_msi_ops,
                          s, "bcm2712-mip-msi", 0xc0);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void bcm2712_mip_msi_realize(DeviceState *dev, Error **errp)
{
    /* Initialize GPIO outputs to GIC */
    qemu_irq *irqs = g_new0(qemu_irq, 64);
    qdev_init_gpio_out(dev, irqs, 64);
}

static const Property bcm2712_mip_msi_props[] = {
    DEFINE_PROP_UINT32("spi-base", BCM2712MIPMSIState, spi_base, 0),
    DEFINE_PROP_UINT32("num-spis", BCM2712MIPMSIState, numSpi, 64),
    DEFINE_PROP_UINT32("spi-offset", BCM2712MIPMSIState, spi_offset, 0),
    DEFINE_PROP_UINT32("pci-addr", BCM2712MIPMSIState, pci_addr, 0),
};

static void bcm2712_mip_msi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_mip_msi_realize;
    device_class_set_props(dc, bcm2712_mip_msi_props);
}

static const TypeInfo bcm2712_mip_msi_type = {
    .name           = TYPE_BCM2712_MIP_MSI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(BCM2712MIPMSIState),
    .instance_init  = bcm2712_mip_msi_init,
    .class_init     = bcm2712_mip_msi_class_init,
};

static void bcm2712_mip_msi_register_types(void)
{
    type_register_static(&bcm2712_mip_msi_type);
}

type_init(bcm2712_mip_msi_register_types);
