/*
 * BCM2712 SoC emulation (Raspberry Pi 5)
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/bcm2712.h"
#include "hw/sysbus.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bcm2712_peripherals.h"
#include "hw/boards.h"
#include "trace.h"
#include "target/arm/cpu.h"

/* GIC-400 interrupt assignments — defined in bcm2712_peripherals.h */
#define GIC_SPI_INTERRUPT_TIMER0 64
#define GIC_SPI_INTERRUPT_TIMER1 65
#define GIC_SPI_INTERRUPT_TIMER2 66
#define GIC_SPI_INTERRUPT_TIMER3 67
#define GIC_SPI_INTERRUPT_DWC2 73
#define GIC_SPI_INTERRUPT_I2C 117
#define GIC_SPI_INTERRUPT_SPI 118

/* PPI assignments for Cortex-A76 */
#define PPI_TIMER 13
#define PPI_VTIMER 14
#define PPI_HYP_TIMER 15
#define PPI_MAINTENANCE 9

/* SCB bus base address (high peripherals) */
#define BCM2712_SCB_BASE 0x107c000000ULL

static void bcm2712_gic_set_irq(void *opaque, int irq, int level)
{
    BCM2712State *s = (BCM2712State *)opaque;
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->gic), irq), level);
}

static void bcm2712_init(Object *obj)
{
    BCM2712State *s = BCM2712(obj);

    object_initialize_child(obj, "peripherals", &s->peripherals,
                            TYPE_BCM2712_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev");
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size");
    object_property_add_alias(obj, "vcram-base", OBJECT(&s->peripherals),
                              "vcram-base");
    object_property_add_alias(obj, "command-line", OBJECT(&s->peripherals),
                              "command-line");
    /* Forward "ram" link from SoC to peripherals (needed by framebuffer DMA).
     * Done in realize() not init() — at realize time the SoC's "ram"
     * link has been set by raspi_base_machine_init. */

    object_initialize_child(obj, "gic", &s->gic, TYPE_ARM_GIC);

    memory_region_init(&s->sysbus_mr, obj, "bcm2712-scb-bus", 0x10000000);
}

static void bcm2712_realize(DeviceState *dev, Error **errp)
{
    BCM2712State *s = BCM2712(dev);
    BCM2712PeripheralState *ps = BCM2712_PERIPHERALS(&s->peripherals);
    BCMSocPeripheralBaseState *ps_base = BCM_SOC_PERIPHERALS_BASE(&s->peripherals);
    DeviceState *gicdev = NULL;
    int i;
    Error *local_err = NULL;

    /* Forward "ram" link from SoC to peripherals (needed by framebuffer DMA).
     * Done in realize() not init() — at realize time the SoC's "ram"
     * link has been set by raspi_base_machine_init. */
    {
        Object *ram_obj = object_property_get_link(OBJECT(dev), "ram",
                                                    &error_abort);
        object_property_add_const_link(OBJECT(&s->peripherals), "ram",
                                       ram_obj);
    }

    /* Realize peripherals container */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->peripherals), errp)) {
        return;
    }

    /* Add sd-bus alias after peripherals realize */
    object_property_add_alias(OBJECT(dev), "sd-bus",
                              OBJECT(&s->peripherals), "sd-bus");

    /* Unmap the peripherals container from system_memory if sysbus
     * auto-mapped it, then remap at the correct BCM2712 address. */
    MemoryRegion *parent_peri_mr = sysbus_mmio_get_region(
        SYS_BUS_DEVICE(&s->peripherals), 0);
    if (parent_peri_mr && parent_peri_mr->container) {
        memory_region_del_subregion(parent_peri_mr->container, parent_peri_mr);
    }

    /* Map SCB bus region (0x107c000000+) */
    memory_region_add_subregion(get_system_memory(), BCM2712_SCB_BASE,
                                &s->sysbus_mr);

    /* Map UART0 at legacy address 0x7D001000 for Pi 5 (0xFE201000 is Pi 4) */
    {
        MemoryRegion *uart0_mr = sysbus_mmio_get_region(
            SYS_BUS_DEVICE(&ps_base->uart0), 0);
        MemoryRegion *uart0_alias = g_new0(MemoryRegion, 1);
        memory_region_init_alias(uart0_alias, OBJECT(s),
                                 "bcm2712-uart0-legacy", uart0_mr, 0,
                                 memory_region_size(uart0_mr));
        memory_region_add_subregion(get_system_memory(), 0x7d001000ULL,
                                    uart0_alias);
    }

    /* Map peripherals at 0x7c000000 (legacy/low alias) */
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->peripherals), 0,
                             BCM2712_PERI_BASE, 1);

    /* TODO: Re-enable SCB peripheral alias and IPA40 dummy after
     * resolving memory region container assertion. */

    /* Add dummy region at top of 40-bit IPA space (1TB - 4KB) to force
     * AddressSpace flat map to cover full 40-bit IPA range.
     * This prevents iotlb_to_section assertion failure for high addresses. */
    {
        MemoryRegion *ipa40_dummy = g_new0(MemoryRegion, 1);
        memory_region_init_ram(ipa40_dummy, OBJECT(s), "ipa40-dummy",
                               0x1000, &error_fatal);  /* 4KB at 1TB - 4KB */
        memory_region_add_subregion(get_system_memory(),
                                    0xFFFFFFFFFF000ULL,
                                    ipa40_dummy);
    }

    /* Initialize GIC-400 */
    if (!object_property_set_uint(OBJECT(&s->gic), "revision", 2, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    if (!object_property_set_uint(OBJECT(&s->gic), "num-cpu", 4, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    if (!object_property_set_uint(OBJECT(&s->gic), "num-irq",
                                  GIC_NUM_IRQS + GIC_INTERNAL, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    if (!object_property_set_bool(OBJECT(&s->gic),
                                  "has-virtualization-extensions", true,
                                  &local_err)) {
        error_propagate(errp, local_err);
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    /* Map GIC-400 registers */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 0, BCM2712_GIC_DIST_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 1, BCM2712_GIC_CPU_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 2, BCM2712_GIC_HYPERVISOR_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gic), 3, BCM2712_GIC_VIRTUAL_BASE);

    gicdev = DEVICE(&s->gic);

    /* Initialize and connect CPUs */
    BCM283XBaseState *s_base = BCM283X_BASE(dev);
    for (i = 0; i < 4; i++) {
        ARMCPU *cpu = ARM_CPU(&s_base->cpu[i].core);

        object_property_set_int(OBJECT(cpu), "mp-affinity", i, &error_abort);
        object_property_set_int(OBJECT(cpu), "reset-cbar",
                                BCM2712_GIC_CPU_BASE, &error_abort);

        /* Start all CPUs directly (PSCI CPU_ON not working yet) */
        object_property_set_bool(OBJECT(cpu), "start-powered-off",
                                 false, &error_abort);

        if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
            return;
        }

        /* Connect CPU IRQ/FIQ/VIRQ/VFIQ to GIC */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 4,
                           qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 8,
                           qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 12,
                           qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VFIQ));

        /* Connect PPI maintenance */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + 16,
                           qdev_get_gpio_in(gicdev, PPI(i, PPI_MAINTENANCE)));

        /* Connect timers */
        qdev_connect_gpio_out(DEVICE(cpu), GTIMER_PHYS,
                              qdev_get_gpio_in(gicdev, PPI(i, PPI_TIMER)));
        qdev_connect_gpio_out(DEVICE(cpu), GTIMER_VIRT,
                              qdev_get_gpio_in(gicdev, PPI(i, PPI_VTIMER)));
        qdev_connect_gpio_out(DEVICE(cpu), GTIMER_HYP,
                              qdev_get_gpio_in(gicdev, PPI(i, PPI_HYP_TIMER)));
    }

    /* Connect peripherals to GIC */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->uart0), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_UART0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->aux), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_UART1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->sdhost), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_SDHOST));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->mboxes), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_MBOX));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->systmr), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_TIMER0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->systmr), 1,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_TIMER1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->systmr), 2,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_TIMER2));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->systmr), 3,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_TIMER3));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->dwc2), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_DWC2));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->i2c[0]), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_I2C));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->spi[0]), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_SPI));

    /* EMMC2 */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps->emmc2), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_EMMC2));

    /* XHCI USB 3.0 controller interrupt - connected to GIC */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps->xhci), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_XHCI));

    /* RP1 UARTs */
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps->rp1.uart0), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_RP1_UART0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&ps->rp1.uart1), 0,
                       qdev_get_gpio_in(gicdev, GIC_SPI_INTERRUPT_RP1_UART1));

    /* RP1 MIP MSI Controller 0 - 64 SPIs starting at SPI 0x80 (128) */
    for (i = 0; i < 64; i++) {
        qdev_connect_gpio_out(DEVICE(&ps->rp1.mip_msi0), i,
                              qdev_get_gpio_in(gicdev, 0x80 + i));
    }

    /* RP1 MIP MSI Controller 1 - 8 SPIs starting at SPI 0xF7 (247) with offset 8 */
    for (i = 0; i < 8; i++) {
        qdev_connect_gpio_out(DEVICE(&ps->rp1.mip_msi1), i,
                              qdev_get_gpio_in(gicdev, 0xF7 + i));
    }

    /* Map MSI controllers at their AXI bus addresses (from DT) */
    /* msi-controller@130000: reg = <0x10 0x130000 0x00 0xc0> -> 0x1000130000 */
    memory_region_add_subregion(get_system_memory(), 0x1000130000ULL,
                                &ps->rp1.mip_msi0.mmio);
    /* msi-controller@131000: reg = <0x10 0x131000 0x00 0xc0> -> 0x1000131000 */
    memory_region_add_subregion(get_system_memory(), 0x1000131000ULL,
                                &ps->rp1.mip_msi1.mmio);

    /* DMA 0-15 to GPU interrupt controller */
    for (i = 0; i < 16; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&ps_base->dma), i,
                           qdev_get_gpio_in_named(DEVICE(&ps_base->ic),
                                                  BCM2835_IC_GPU_IRQ,
                                                  GPU_INTERRUPT_DMA0 + i));
    }

    /* Pass through inbound GPIO lines to GIC */
    qdev_init_gpio_in(dev, bcm2712_gic_set_irq, GIC_NUM_IRQS);

    /* Pass through outbound IRQ lines from the GIC */
    qdev_pass_gpios(DEVICE(&s->gic), DEVICE(&s->peripherals), NULL);
}

static void bcm2712_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc_base = BCM283X_BASE_CLASS(oc);

    bc_base->cpu_type = ARM_CPU_TYPE_NAME("cortex-a76");
    bc_base->core_count = 4;
    bc_base->peri_base = BCM2712_PERI_BASE;
    bc_base->ctrl_base = 0;
    bc_base->clusterid = 0x0;
    dc->realize = bcm2712_realize;
    dc->desc = "BCM2712 SoC (Raspberry Pi 5)";
}

static const TypeInfo bcm2712_type = {
    .name           = TYPE_BCM2712,
    .parent         = TYPE_BCM283X_BASE,
    .instance_size  = sizeof(BCM2712State),
    .instance_init  = bcm2712_init,
    .class_size     = sizeof(BCM2712Class),
    .class_init     = bcm2712_class_init,
};

static void bcm2712_register_types(void)
{
    type_register_static(&bcm2712_type);
}

type_init(bcm2712_register_types);
