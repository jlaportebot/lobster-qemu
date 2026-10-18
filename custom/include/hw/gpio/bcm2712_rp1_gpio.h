/*
 * RP1 GPIO Controller (Raspberry Pi 5)
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_RP1_GPIO_H
#define BCM2712_RP1_GPIO_H

#include "hw/sysbus.h"

#define TYPE_BCM2712_RP1_GPIO "bcm2712-rp1-gpio"
OBJECT_DECLARE_TYPE(BCM2712RP1GPIOState, BCM2712RP1GPIOClass, BCM2712_RP1_GPIO)

#define RP1_GPIO_NR         54
#define RP1_GPIO_BANKS      3
#define RP1_GPIO_PER_BANK   18
#define RP1_GPIO_BANK_STRIDE 0x600

struct BCM2712RP1GPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* GPIO state per bank (3 banks, 18 GPIOs each) */
    uint32_t fsel[3][2];      /* Function select (2 regs per bank = 54 bits) */
    uint32_t out[3];          /* Output value (18 bits) */
    uint32_t oe[3];           /* Output enable (18 bits) */
    uint32_t in_sync[3];      /* Synchronized input (18 bits) */
    uint32_t irq_en[3];       /* Interrupt enable (18 bits) */
    uint32_t irq_stat[3];     /* Interrupt status (18 bits) */
    uint32_t irq_type[3];     /* Interrupt type (0=level, 1=edge) */
    uint32_t irq_pol[3];      /* Interrupt polarity */
    uint32_t irq_edge[3];     /* Interrupt edge (0=both, 1=rising, 2=falling) */

    /* GPIO output lines (54) */
    qemu_irq gpio_out[54];
};

struct BCM2712RP1GPIOClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
};

#endif /* BCM2712_RP1_GPIO_H */