/*
 * BCM2712 SoC emulation (Raspberry Pi 5)
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_H
#define BCM2712_H

#include "hw/arm/bcm2836.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bcm2712_peripherals.h"

/* BCM2712 uses a different peripheral base address: 0x10_7c00_0000 */
/* Peripheral base as seen by CPU (with 0x10 prefix for 40-bit addressing) */
#define BCM2712_PERI_BASE         0x107c000000ULL
#define BCM2712_CTRL_BASE         0x107fff9000ULL
#define BCM2712_GIC_BASE          0x7fff9000

/* GIC-400 PPI interrupts (same as BCM2838 but with GIC-400) */
#define GIC400_MAINTENANCE_IRQ      9
#define GIC400_TIMER_NS_EL2_IRQ     10
#define GIC400_TIMER_VIRT_IRQ       11
#define GIC400_LEGACY_FIQ           12
#define GIC400_TIMER_S_EL1_IRQ      13
#define GIC400_TIMER_NS_EL1_IRQ     14
#define GIC400_LEGACY_IRQ           15

#define VIRTUAL_PMU_IRQ 7

/* Number of external interrupt lines for GIC */
#define GIC_NUM_IRQS                192

#define PPI(cpu, irq) (GIC_NUM_IRQS + (cpu) * GIC_INTERNAL + GIC_NR_SGIS + irq)

#define GIC_BASE_OFS                0x0000
#define GIC_DIST_OFS                0x1000
#define GIC_CPU_OFS                 0x2000
#define GIC_VIFACE_THIS_OFS         0x4000
#define GIC_VIFACE_OTHER_OFS(cpu)  (0x5000 + (cpu) * 0x200)
#define GIC_VCPU_OFS                0x6000

#define TYPE_BCM2712 "bcm2712"

OBJECT_DECLARE_TYPE(BCM2712State, BCM2712Class, BCM2712)

struct BCM2712State {
    /*< private >*/
    BCM283XBaseState parent_obj;
    /*< public >*/
    BCM2712PeripheralState peripherals;
    GICState gic;
};

#endif /* BCM2712_H */