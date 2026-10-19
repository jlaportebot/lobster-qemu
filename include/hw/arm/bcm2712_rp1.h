/*
 * RP1 I/O Controller emulation (Raspberry Pi 5)
 *
 * The RP1 is a custom microcontroller on Pi 5 that handles GPIO, UART, I2C, SPI, SD
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_RP1_H
#define BCM2712_RP1_H

#include "hw/sysbus.h"
#include "hw/sd/sdhci.h"
#include "hw/char/pl011.h"
#include "hw/arm/bcm2712_mip_msi.h"
#include "hw/misc/unimp.h"
#include "hw/gpio/bcm2712_rp1_gpio.h"

/* RP1 Register offsets */
#define RP1_GPIO_BASE       0x000000
#define RP1_GPIO_SIZE       0x10000
#define RP1_UART0_BASE      0x100000
#define RP1_UART1_BASE      0x110000
#define RP1_UART_SIZE       0x10000
#define RP1_I2C0_BASE       0x200000
#define RP1_I2C1_BASE       0x210000
#define RP1_I2C2_BASE       0x220000
#define RP1_I2C3_BASE       0x230000
#define RP1_I2C4_BASE       0x240000
#define RP1_I2C5_BASE       0x250000
#define RP1_I2C_SIZE        0x10000
#define RP1_SPI0_BASE       0x300000
#define RP1_SPI1_BASE       0x310000
#define RP1_SPI2_BASE       0x320000
#define RP1_SPI3_BASE       0x330000
#define RP1_SPI4_BASE       0x340000
#define RP1_SPI5_BASE       0x350000
#define RP1_SPI_SIZE        0x10000
#define RP1_SD0_BASE        0x400000
#define RP1_SD1_BASE        0x410000
#define RP1_SD_SIZE         0x10000

/* RP1 MIP (Microcontroller Interrupt Processor) register offsets */
#define RP1_MIP_BASE            0x500000
#define RP1_MIP_SIZE            0x10000

#define RP1_MIP_IRQ_ENABLE_L    0x0000  /* Interrupt enable low (0-31) */
#define RP1_MIP_IRQ_ENABLE_H    0x0004  /* Interrupt enable high (32-63) */
#define RP1_MIP_IRQ_STATUS_L    0x0008  /* Interrupt status low (0-31) */
#define RP1_MIP_IRQ_STATUS_H    0x000C  /* Interrupt status high (32-63) */
#define RP1_MIP_IRQ_MASK_L      0x0010  /* Interrupt mask low (0-31) */
#define RP1_MIP_IRQ_MASK_H      0x0014  /* Interrupt mask high (32-63) */
#define RP1_MIP_IRQ_PENDING_L   0x0018  /* Interrupt pending low (0-31) */
#define RP1_MIP_IRQ_PENDING_H   0x001C  /* Interrupt pending high (32-63) */
#define RP1_MIP_FIQ_ENABLE_L    0x0020  /* FIQ enable low (0-31) */
#define RP1_MIP_FIQ_ENABLE_H    0x0024  /* FIQ enable high (32-63) */
#define RP1_MIP_FIQ_STATUS_L    0x0028  /* FIQ status low (0-31) */
#define RP1_MIP_FIQ_STATUS_H    0x002C  /* FIQ status high (32-63) */
#define RP1_MIP_FIQ_MASK_L      0x0030  /* FIQ mask low (0-31) */
#define RP1_MIP_FIQ_MASK_H      0x0034  /* FIQ mask high (32-63) */
#define RP1_MIP_MSI_CTRL        0x0100  /* MSI control register */
#define RP1_MIP_MSI_ADDR_LO     0x0104  /* MSI address low */
#define RP1_MIP_MSI_ADDR_HI     0x0108  /* MSI address high */
#define RP1_MIP_MSI_DATA        0x010C  /* MSI data register */
#define RP1_MIP_MSI_STATUS      0x0110  /* MSI status register */
#define RP1_MIP_MSI_ENABLE      0x0114  /* MSI enable register */

#define TYPE_BCM2712_RP1 "bcm2712-rp1"
OBJECT_DECLARE_TYPE(BCM2712RP1State, BCM2712RP1Class, BCM2712_RP1)

struct BCM2712RP1State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion rp1_mr;

    /* GPIO - RP1 GPIO controller */
    BCM2712RP1GPIOState gpio;

    /* UARTs - PL011 compatible */
    PL011State uart0;
    PL011State uart1;

    /* I2C - placeholders */
    UnimplementedDeviceState i2c0;
    UnimplementedDeviceState i2c1;
    UnimplementedDeviceState i2c2;
    UnimplementedDeviceState i2c3;
    UnimplementedDeviceState i2c4;
    UnimplementedDeviceState i2c5;

    /* SPI - placeholders */
    UnimplementedDeviceState spi0;
    UnimplementedDeviceState spi1;
    UnimplementedDeviceState spi2;
    UnimplementedDeviceState spi3;
    UnimplementedDeviceState spi4;
    UnimplementedDeviceState spi5;

    /* SD controllers */
    SDHCIState sd0;
    SDHCIState sd1;
    MemoryRegion sd0_mr;
    MemoryRegion sd1_mr;

    /* MIP (Microcontroller Interrupt Processor) */
    MemoryRegion mip_mr;
    uint64_t mip_irq_enable;
    uint64_t mip_irq_status;
    uint64_t mip_irq_mask;
    uint64_t mip_irq_pending;
    uint64_t mip_fiq_enable;
    uint64_t mip_fiq_status;
    uint64_t mip_fiq_mask;
    uint32_t mip_msi_ctrl;
    uint64_t mip_msi_addr;
    uint32_t mip_msi_data;
    uint32_t mip_msi_status;
    uint32_t mip_msi_enable;
    qemu_irq mip_irq_outputs[64];  /* 64 interrupt outputs to GIC */

    /* MIP MSI Controllers */
    BCM2712MIPMSIState mip_msi0;
    BCM2712MIPMSIState mip_msi1;
};

struct BCM2712RP1Class {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
};

#endif /* BCM2712_RP1_H */