/*
 * BCM2712 peripherals emulation
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/arm/bcm2712_peripherals.h"
#include "hw/arm/bcm2838_peripherals.h"
#include "hw/display/bcm2712_fb.h"
#include "hw/misc/bcm2835_mbox.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/pci/pci.h"

#define CLOCK_ISP_OFFSET        0xc11000
#define CLOCK_ISP_SIZE          0x100

/* Lower peripheral base address on the VC (GPU) system bus */
#define BCM2712_VC_PERI_LOW_BASE 0x7c000000

/* Capabilities for SD controller: no DMA, high-speed, default clocks etc. */
#define BCM2835_SDHC_CAPAREG 0x52134b4

static void bcm2712_peripherals_init(Object *obj)
{
    BCM2712PeripheralState *s = BCM2712_PERIPHERALS(obj);
    BCM2838PeripheralState *s_2838 = BCM2838_PERIPHERALS(obj);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(obj);
    BCM2712PeripheralClass *bc = BCM2712_PERIPHERALS_GET_CLASS(obj);

    /* Call parent init first (BCM2838 peripherals) */
    bcm2838_peripherals_init(obj);

    /* Internal memory region for mailbox-addressable peripherals */
    memory_region_init(&s_base->mbox_mr, obj, "bcm2835-mbox",
                       MBOX_CHAN_COUNT << MBOX_AS_CHAN_SHIFT);

    /* Lower memory region for peripheral devices (exported to the Soc) */
    memory_region_init(&s->peri_low_mr, obj, "bcm2712-peripherals",
                       bc->peri_low_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->peri_low_mr);

    /* Pixelvalve v2 display controllers */
    object_initialize_child(obj, "pixelvalve0", &s->pixelvalve0,
                            TYPE_BCM2712_PIXELVALVE);
    object_initialize_child(obj, "pixelvalve1", &s->pixelvalve1,
                            TYPE_BCM2712_PIXELVALVE);
    object_initialize_child(obj, "mop", &s->mop,
                            TYPE_BCM2712_MOP);
    object_initialize_child(obj, "moplet", &s->moplet,
                            TYPE_BCM2712_MOPLET);
    object_initialize_child(obj, "disp_intr", &s->disp_intr,
                            TYPE_BCM2711_L2_INTC);

    /* DVP (Digital Video Port) */
    object_initialize_child(obj, "dvp", &s->dvp,
                            TYPE_BCM2711_DVP);

    /* HDMI controllers */
    object_initialize_child(obj, "hdmi0", &s->hdmi0,
                            TYPE_BCM2712_HDMI);
    object_initialize_child(obj, "hdmi1", &s->hdmi1,
                            TYPE_BCM2712_HDMI);

    /* I2C for HDMI DDC */
    object_initialize_child(obj, "ddc0", &s->ddc0,
                            TYPE_I2C_BCM2711);
    object_initialize_child(obj, "ddc1", &s->ddc1,
                            TYPE_I2C_BCM2711);
    object_initialize_child(obj, "bsc_irq", &s->bsc_irq,
                            TYPE_BCM7271_L2_INTC);
    object_initialize_child(obj, "main_irq", &s->main_irq,
                            TYPE_BCM7271_L2_INTC);

    /* GIO_AON GPIO */
    object_initialize_child(obj, "gio_aon", &s->gio_aon,
                            TYPE_BCM7445_GPIO);

    /* AON interrupt controller */
    object_initialize_child(obj, "aon_intr", &s->aon_intr,
                            TYPE_BCM2711_L2_INTC);

    /* L2 interrupt controllers */
    object_initialize_child(obj, "l2_intc0", &s->l2_intc0,
                            TYPE_BCM7271_L2_INTC);
    object_initialize_child(obj, "l2_intc1", &s->l2_intc1,
                            TYPE_BCM2711_L2_INTC);
    object_initialize_child(obj, "bsc_intc", &s->bsc_intc,
                            TYPE_BCM7271_L2_INTC);

    /* PCIe controllers */
    object_initialize_child(obj, "pcie0", &s->pcie0,
                            TYPE_BCM2712_PCIE);
    object_initialize_child(obj, "pcie1", &s->pcie1,
                            TYPE_BCM2712_PCIE);
    object_initialize_child(obj, "pcie2", &s->pcie2,
                            TYPE_BCM2712_PCIE);
    object_initialize_child(obj, "mip0", &s->mip0,
                            TYPE_BCM2712_MIP);
    object_initialize_child(obj, "mip1", &s->mip1,
                            TYPE_BCM2712_MIP);
    object_initialize_child(obj, "pcie_rescal", &s->pcie_rescal,
                            TYPE_BCM7216_PCIE_SATA_RESCAL);
    object_initialize_child(obj, "bcm_reset", &s->bcm_reset,
                            TYPE_BCM_BRCMSTB_RESET);

    /* Mailboxes */
    object_initialize_child(obj, "mbox", &s->mboxes, TYPE_BCM2835_MBOX);
    object_property_add_const_link(OBJECT(&s->mboxes), "mbox-mr",
                                   OBJECT(&s_base->mbox_mr));

    /* Framebuffer */
    object_initialize_child(obj, "fb", &s->fb, TYPE_BCM2712_FB);
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->fb), "vcram-size");
    object_property_add_alias(obj, "vcram-base", OBJECT(&s->fb), "vcram-base");
    object_property_add_const_link(OBJECT(&s->fb), "dma-mr",
                                   OBJECT(&s_base->gpu_bus_mr));

    /* Property channel */
    object_initialize_child(obj, "property", &s->property,
                            TYPE_BCM2835_PROPERTY);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->property),
                              "board-rev");
    object_property_add_alias(obj, "command-line", OBJECT(&s->property),
                              "command-line");
    object_property_add_const_link(OBJECT(&s->property), "fb",
                                   OBJECT(&s->fb));
    object_property_add_const_link(OBJECT(&s->property), "dma-mr",
                                   OBJECT(&s_base->gpu_bus_mr));
    object_property_add_const_link(OBJECT(&s->property), "otp",
                                   OBJECT(&s->otp));
}

static void bcm2712_peripherals_realize(DeviceState *dev, Error **errp)
{
    BCM2712PeripheralState *s = BCM2712_PERIPHERALS(dev);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(dev);
    int n;

    /* Realize parent (BCM2838) peripherals first */
    bcm2838_peripherals_realize(dev, errp);
    if (error_is_set(errp)) {
        return;
    }

    /* Map lower peripherals into the GPU address space */
    memory_region_init_alias(&s->peri_low_mr_alias, OBJECT(s),
                             "bcm2712-peripherals", &s->peri_low_mr, 0,
                             memory_region_size(&s->peri_low_mr));
    memory_region_add_subregion_overlap(&s_base->gpu_bus_mr,
                                        BCM2712_VC_PERI_LOW_BASE,
                                        &s->peri_low_mr_alias, 1);

    /* Pixelvalve0 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pixelvalve0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, PIXELVALVE0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pixelvalve0), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pixelvalve0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->disp_intr), "in", 1));

    /* Pixelvalve1 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pixelvalve1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, PIXELVALVE1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pixelvalve1), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pixelvalve1), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->disp_intr), "in", 9));

    /* MOP */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mop), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, MOP_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mop), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mop), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->disp_intr), "in", 2));

    /* MOPLET */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->moplet), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, MOPLET_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->moplet), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->moplet), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->disp_intr), "in", 0));

    /* Display interrupt controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->disp_intr), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, DISP_INTR_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->disp_intr), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->disp_intr), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_DISP));

    /* DVP */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dvp), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, DVP_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dvp), 0));

    /* HDMI0 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hdmi0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, HDMI0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->hdmi0), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 1,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 2));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 2,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 3));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 3,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 7));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 4,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 8));

    /* HDMI1 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hdmi1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, HDMI1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->hdmi1), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 11));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 1,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 12));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 2,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 13));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 3,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 14));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 4,
                       qdev_get_gpio_in_named(DEVICE(&s->aon_intr), "in", 15));

    /* DDC0 I2C for HDMI0 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddc0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, DDC0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ddc0), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ddc0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->bsc_irq), "in", 1));

    /* DDC1 I2C for HDMI1 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ddc1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, DDC1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ddc1), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ddc1), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->bsc_irq), "in", 2));

    /* BSC interrupt controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bsc_irq), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, BSC_IRQ_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->bsc_irq), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->bsc_irq), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_BSC));

    /* Main interrupt controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->main_irq), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, MAIN_IRQ_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->main_irq), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->main_irq), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_MAIN));

    /* GIO_AON GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gio_aon), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, GIO_AON_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gio_aon), 0));

    /* AON interrupt controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aon_intr), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, AON_INTR_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->aon_intr), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aon_intr), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_AON));

    /* L2 interrupt controllers */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->l2_intc0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, L2_INTC0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->l2_intc0), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->l2_intc0), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_L2_0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->l2_intc1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, L2_INTC1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->l2_intc1), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->l2_intc1), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_L2_1));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bsc_intc), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, BSC_INTC_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->bsc_intc), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->bsc_intc), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->ic), GIC_SPI_INTERRUPT_BSC2));

    /* PCIe controllers - for now create unimplemented placeholders */
    create_unimp(s_base, &s->pcie0, "bcm2712-pcie0", PCIE0_OFFSET, 0x10000);
    create_unimp(s_base, &s->pcie1, "bcm2712-pcie1", PCIE1_OFFSET, 0x10000);
    create_unimp(s_base, &s->pcie2, "bcm2712-pcie2", PCIE2_OFFSET, 0x10000);

    /* MSI controllers */
    create_unimp(s_base, &s->mip0, "bcm2712-mip0", MIP0_OFFSET, 0x1000);
    create_unimp(s_base, &s->mip1, "bcm2712-mip1", MIP1_OFFSET, 0x1000);

    /* PCIe reset controllers */
    create_unimp(s_base, &s->pcie_rescal, "bcm7216-pcie-sata-rescal", PCIE_RESCAL_OFFSET, 0x10);
    create_unimp(s_base, &s->bcm_reset, "brcm,brcmstb-reset", BCM_RESET_OFFSET, 0x30);

    /* Unimplemented placeholders */
    create_unimp(s_base, &s->clkisp, "bcm2835-clkisp", CLOCK_ISP_OFFSET,
                 CLOCK_ISP_SIZE);
    create_unimp(s_base, &s->asb, "bcm2838-asb", BRDG_OFFSET, 0x24);

    /* Mailboxes */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mboxes), errp)) {
        return;
    }

    memory_region_add_subregion(&s_base->peri_mr, ARMCTRL_0_SBM_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mboxes), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mboxes), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_ARM_IRQ,
                               INTERRUPT_ARM_MAILBOX));

    /* Framebuffer */
    vcram_size = object_property_get_uint(OBJECT(s), "vcram-size", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    vcram_base = object_property_get_uint(OBJECT(s), "vcram-base", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (vcram_base == 0) {
        vcram_base = ram_size - vcram_size;
    }
    vcram_base = MIN(vcram_base, UPPER_RAM_BASE - vcram_size);

    if (!object_property_set_uint(OBJECT(&s->fb), "vcram-base", vcram_base,
                                  errp)) {
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fb), errp)) {
        return;
    }

    memory_region_add_subregion(&s_base->mbox_mr, MBOX_CHAN_FB << MBOX_AS_CHAN_SHIFT,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->fb), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fb), 0,
                       qdev_get_gpio_in(DEVICE(&s->mboxes), MBOX_CHAN_FB));

    /* Property channel */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->property), errp)) {
        return;
    }

    memory_region_add_subregion(&s_base->mbox_mr,
                MBOX_CHAN_PROPERTY << MBOX_AS_CHAN_SHIFT,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->property), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->property), 0,
                      qdev_get_gpio_in(DEVICE(&s->mboxes), MBOX_CHAN_PROPERTY));
}

static void bcm2712_peripherals_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM2712PeripheralClass *bc = BCM2712_PERIPHERALS_CLASS(oc);
    BCMSocPeripheralBaseClass *bc_base = BCM_SOC_PERIPHERALS_BASE_CLASS(oc);

    bc->peri_low_size = 0x2000000;
    bc_base->peri_size = 0x1800000;
    dc->realize = bcm2712_peripherals_realize;
}

static const TypeInfo bcm2712_peripherals_type_info = {
    .name = TYPE_BCM2712_PERIPHERALS,
    .parent = TYPE_BCM2838_PERIPHERALS,
    .instance_size = sizeof(BCM2712PeripheralState),
    .instance_init = bcm2712_peripherals_init,
    .class_size = sizeof(BCM2712PeripheralClass),
    .class_init = bcm2712_peripherals_class_init,
};

static void bcm2712_peripherals_register_types(void)
{
    type_register_static(&bcm2712_peripherals_type_info);
}

type_init(bcm2712_peripherals_register_types);