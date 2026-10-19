/*
 * RP1 I/O Controller emulation (Raspberry Pi 5)
 *
 * The RP1 is a custom microcontroller on Pi 5 that handles GPIO, UART, I2C, SPI, SD
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/sd/sdhci.h"
#include "hw/char/pl011.h"
#include "hw/arm/bcm2712_rp1.h"
#include "hw/arm/bcm2712_peripherals.h"
#include "hw/gpio/bcm2712_rp1_gpio.h"
#include "hw/misc/unimp.h"
#include "qemu/log.h"
#include "trace.h"

/* MIP (Microcontroller Interrupt Processor) register operations */
static MemTxResult mip_read(void *opaque, hwaddr offset, uint64_t *data, unsigned size, MemTxAttrs attrs)
{
    BCM2712RP1State *s = opaque;
    uint64_t val = 0;

    switch (offset) {
    case RP1_MIP_IRQ_ENABLE_L:
        val = s->mip_irq_enable & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_ENABLE_H:
        val = (s->mip_irq_enable >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_STATUS_L:
        val = s->mip_irq_status & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_STATUS_H:
        val = (s->mip_irq_status >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_MASK_L:
        val = s->mip_irq_mask & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_MASK_H:
        val = (s->mip_irq_mask >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_PENDING_L:
        val = s->mip_irq_pending & 0xFFFFFFFF;
        break;
    case RP1_MIP_IRQ_PENDING_H:
        val = (s->mip_irq_pending >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_ENABLE_L:
        val = s->mip_fiq_enable & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_ENABLE_H:
        val = (s->mip_fiq_enable >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_STATUS_L:
        val = s->mip_fiq_status & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_STATUS_H:
        val = (s->mip_fiq_status >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_MASK_L:
        val = s->mip_fiq_mask & 0xFFFFFFFF;
        break;
    case RP1_MIP_FIQ_MASK_H:
        val = (s->mip_fiq_mask >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_MSI_CTRL:
        val = s->mip_msi_ctrl;
        break;
    case RP1_MIP_MSI_ADDR_LO:
        val = s->mip_msi_addr & 0xFFFFFFFF;
        break;
    case RP1_MIP_MSI_ADDR_HI:
        val = (s->mip_msi_addr >> 32) & 0xFFFFFFFF;
        break;
    case RP1_MIP_MSI_DATA:
        val = s->mip_msi_data;
        break;
    case RP1_MIP_MSI_STATUS:
        val = s->mip_msi_status;
        break;
    case RP1_MIP_MSI_ENABLE:
        val = s->mip_msi_enable;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "MIP: Bad read at offset 0x%lx\n", offset);
        break;
    }
    *data = val;
    return MEMTX_OK;
}

static MemTxResult mip_write(void *opaque, hwaddr offset, uint64_t value, unsigned size, MemTxAttrs attrs)
{
    BCM2712RP1State *s = opaque;

    switch (offset) {
    case RP1_MIP_IRQ_ENABLE_L:
        s->mip_irq_enable = (s->mip_irq_enable & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        break;
    case RP1_MIP_IRQ_ENABLE_H:
        s->mip_irq_enable = (s->mip_irq_enable & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
        break;
    case RP1_MIP_IRQ_MASK_L:
        s->mip_irq_mask = (s->mip_irq_mask & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        break;
    case RP1_MIP_IRQ_MASK_H:
        s->mip_irq_mask = (s->mip_irq_mask & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
        break;
    case RP1_MIP_FIQ_ENABLE_L:
        s->mip_fiq_enable = (s->mip_fiq_enable & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        break;
    case RP1_MIP_FIQ_ENABLE_H:
        s->mip_fiq_enable = (s->mip_fiq_enable & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
        break;
    case RP1_MIP_FIQ_MASK_L:
        s->mip_fiq_mask = (s->mip_fiq_mask & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        break;
    case RP1_MIP_FIQ_MASK_H:
        s->mip_fiq_mask = (s->mip_fiq_mask & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
        break;
    case RP1_MIP_MSI_CTRL:
        s->mip_msi_ctrl = value;
        break;
    case RP1_MIP_MSI_ADDR_LO:
        s->mip_msi_addr = (s->mip_msi_addr & 0xFFFFFFFF00000000ULL) | (value & 0xFFFFFFFF);
        break;
    case RP1_MIP_MSI_ADDR_HI:
        s->mip_msi_addr = (s->mip_msi_addr & 0xFFFFFFFF) | ((value & 0xFFFFFFFF) << 32);
        break;
    case RP1_MIP_MSI_DATA:
        s->mip_msi_data = value;
        if ((s->mip_msi_enable & 1) && (s->mip_msi_ctrl & 1)) {
            /* Send MSI to GIC via the MSI interrupt output */
            uint32_t msi_irq = value & 0xFF;
            if (msi_irq < 64 && s->mip_irq_outputs[msi_irq]) {
                qemu_set_irq(s->mip_irq_outputs[msi_irq], 1);
                s->mip_irq_pending |= (1ULL << msi_irq);
                s->mip_irq_status |= (1ULL << msi_irq);
            }
        }
        break;
    case RP1_MIP_MSI_ENABLE:
        s->mip_msi_enable = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "MIP: Bad write at offset 0x%lx\n", offset);
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps mip_ops = {
    .read_with_attrs = mip_read,
    .write_with_attrs = mip_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void bcm2712_rp1_init(Object *obj)
{
    BCM2712RP1State *s = BCM2712_RP1(obj);

    memory_region_init(&s->rp1_mr, obj, "bcm2712-rp1", 0x1000000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->rp1_mr);

    /* GPIO - RP1 GPIO controller */
    object_initialize_child(obj, "gpio", &s->gpio, TYPE_BCM2712_RP1_GPIO);

    /* UART0 - PL011 compatible */
    object_initialize_child(obj, "uart0", &s->uart0, TYPE_PL011);
    memory_region_init(&s->uart0.iomem, OBJECT(&s->uart0), "uart0", RP1_UART_SIZE);

    /* UART1 - PL011 compatible */
    object_initialize_child(obj, "uart1", &s->uart1, TYPE_PL011);
    memory_region_init(&s->uart1.iomem, OBJECT(&s->uart1), "uart1", RP1_UART_SIZE);

    /* SD0 */
    object_initialize_child(obj, "sd0", &s->sd0, TYPE_SYSBUS_SDHCI);
    memory_region_init(&s->sd0_mr, OBJECT(&s->sd0), "sd0", RP1_SD_SIZE);

    /* SD1 */
    object_initialize_child(obj, "sd1", &s->sd1, TYPE_SYSBUS_SDHCI);
    memory_region_init(&s->sd1_mr, OBJECT(&s->sd1), "sd1", RP1_SD_SIZE);

    /* I2C placeholders */
    for (int i = 0; i < 6; i++) {
        char name[16];
        snprintf(name, sizeof(name), "i2c%d", i);
        object_initialize_child(obj, name, &s->i2c0 + i, TYPE_UNIMPLEMENTED_DEVICE);
    }

    /* SPI placeholders */
    for (int i = 0; i < 6; i++) {
        char name[16];
        snprintf(name, sizeof(name), "spi%d", i);
        object_initialize_child(obj, name, &s->spi0 + i, TYPE_UNIMPLEMENTED_DEVICE);
    }

    /* MIP (Microcontroller Interrupt Processor) - register block only, interrupt routing handled by GIC */
    memory_region_init_io(&s->mip_mr, obj, &mip_ops, s, "bcm2712-rp1-mip", RP1_MIP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mip_mr);
}

static void bcm2712_rp1_realize(DeviceState *dev, Error **errp)
{
    BCM2712RP1State *s = BCM2712_RP1(dev);

    /* GPIO - RP1 GPIO controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    memory_region_add_subregion(&s->rp1_mr, RP1_GPIO_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0));

    /* UART0 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart0), errp)) {
        return;
    }
    memory_region_add_subregion(&s->rp1_mr, RP1_UART0_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart0), 0));

    /* UART1 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart1), errp)) {
        return;
    }
    memory_region_add_subregion(&s->rp1_mr, RP1_UART1_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart1), 0));

    /* SD0 */
    object_property_set_uint(OBJECT(&s->sd0), "sd-spec-version", 3, &error_abort);
    object_property_set_uint(OBJECT(&s->sd0), "capareg", BCM2835_SDHC_CAPAREG, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sd0), errp)) {
        return;
    }
    memory_region_add_subregion(&s->rp1_mr, RP1_SD0_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sd0), 0));

    /* SD1 */
    object_property_set_uint(OBJECT(&s->sd1), "sd-spec-version", 3, &error_abort);
    object_property_set_uint(OBJECT(&s->sd1), "capareg", BCM2835_SDHC_CAPAREG, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sd1), errp)) {
        return;
    }
    memory_region_add_subregion(&s->rp1_mr, RP1_SD1_BASE,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sd1), 0));

    /* I2C placeholders */
    for (int i = 0; i < 6; i++) {
        hwaddr base = RP1_I2C0_BASE + i * RP1_I2C_SIZE;
        char name[32];
        snprintf(name, sizeof(name), "bcm2712-rp1-i2c%d", i);
        object_initialize_child(OBJECT(dev), name, &s->i2c0 + i, TYPE_UNIMPLEMENTED_DEVICE);
        qdev_prop_set_string(DEVICE(&s->i2c0 + i), "name", name);
        qdev_prop_set_uint64(DEVICE(&s->i2c0 + i), "size", RP1_I2C_SIZE);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c0 + i), errp)) {
            return;
        }
        memory_region_add_subregion(&s->rp1_mr, base,
                                    sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i2c0 + i), 0));
    }

    /* SPI placeholders */
    for (int i = 0; i < 6; i++) {
        hwaddr base = RP1_SPI0_BASE + i * RP1_SPI_SIZE;
        char name[32];
        snprintf(name, sizeof(name), "bcm2712-rp1-spi%d", i);
        object_initialize_child(OBJECT(dev), name, &s->spi0 + i, TYPE_UNIMPLEMENTED_DEVICE);
        qdev_prop_set_string(DEVICE(&s->spi0 + i), "name", name);
        qdev_prop_set_uint64(DEVICE(&s->spi0 + i), "size", RP1_SPI_SIZE);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi0 + i), errp)) {
            return;
        }
        memory_region_add_subregion(&s->rp1_mr, base,
                                    sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->spi0 + i), 0));
    }

    /* MIP (Microcontroller Interrupt Processor) - register block only */
    memory_region_add_subregion(&s->rp1_mr, RP1_MIP_BASE, &s->mip_mr);

    /* MIP MSI Controller 0 (msi-controller@130000) - 64 SPIs at base 0x80 */
    object_initialize_child(OBJECT(dev), "mip-msi0", &s->mip_msi0, TYPE_BCM2712_MIP_MSI);
    object_property_set_uint(OBJECT(&s->mip_msi0), "spi-base", 0x80, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi0), "num-spis", 0x40, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi0), "spi-offset", 0x00, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi0), "pci-addr", 0xFFFFF000, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mip_msi0), errp)) {
        return;
    }
    /* MSI controller 0 mapped at SoC level (0x100130000) */

    /* MIP MSI Controller 1 (msi-controller@131000) - 8 SPIs at base 0xF7 with offset 0x8 */
    object_initialize_child(OBJECT(dev), "mip-msi1", &s->mip_msi1, TYPE_BCM2712_MIP_MSI);
    object_property_set_uint(OBJECT(&s->mip_msi1), "spi-base", 0xF7, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi1), "num-spis", 0x08, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi1), "spi-offset", 0x08, &error_abort);
    object_property_set_uint(OBJECT(&s->mip_msi1), "pci-addr", 0xFFFFE000, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mip_msi1), errp)) {
        return;
    }
    /* MSI controller 1 mapped at SoC level (0x100131000) */

    /* Connect MSI controllers to GIC via the RP1 interrupt controller */
    /* The RP1 interrupt controller (MIP) routes interrupts to the GIC */
    /* The MIP MSI controllers are children of the RP1 interrupt controller */

    /* Export SD bus for board-level connections */
    object_property_add_const_link(OBJECT(s), "sd-bus", OBJECT(&s->sd0.sdbus));
}

static void bcm2712_rp1_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = bcm2712_rp1_realize;
}

static const TypeInfo bcm2712_rp1_type_info = {
    .name = TYPE_BCM2712_RP1,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712RP1State),
    .instance_init = bcm2712_rp1_init,
    .class_size = sizeof(BCM2712RP1Class),
    .class_init = bcm2712_rp1_class_init,
};

static void bcm2712_rp1_register_types(void)
{
    type_register_static(&bcm2712_rp1_type_info);
}

type_init(bcm2712_rp1_register_types)