/*
 * BCM2712 peripherals emulation
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_PERIPHERALS_H
#define BCM2712_PERIPHERALS_H

#include "hw/arm/bcm2838_peripherals.h"

/* BCM2712 peripheral offsets (CPU view, with 0x10_7c00_0000 base) */
#define GPIO_OFFSET           0x00200000
#define UART0_OFFSET          0x00201000
#define UART1_OFFSET          0x00201400  /* AUX */
#define MBOX_OFFSET           0x0000b880
#define SYSTMR_OFFSET         0x00003000
#define RNG_OFFSET            0x00104000
#define THERMAL_OFFSET        0x00104100
#define MPHI_OFFSET           0x00104200
#define CPRMAN_OFFSET         0x00101000
#define EMMC2_OFFSET          0x00300000
#define EMMC_OFFSET           0x00340000
#define SDHOST_OFFSET         0x00202000
#define DMA_OFFSET            0x00007000
#define ARMCTRL_IC_OFFSET     0x0000b000
#define ARMCTRL_0_SBM_OFFSET  0x0000b880
#define OTP_OFFSET            0x00100000
#define MPHI_SIZE             0x1000
#define CLOCK_ISP_OFFSET      0x00111000
#define CLOCK_ISP_SIZE        0x100
#define BRDG_OFFSET           0x00112000

/* Pixelvalve v2 offsets */
#define PIXELVALVE0_OFFSET    0x00410000
#define PIXELVALVE1_OFFSET    0x00411000
#define MOP_OFFSET            0x00500000
#define MOPLET_OFFSET         0x00501000
#define DISP_INTR_OFFSET      0x00502000
#define DVP_OFFSET            0x00700000
#define HDMI0_OFFSET          0x0ef00700
#define HDMI1_OFFSET          0x0ef05700

/* HDMI register sub-offsets */
#define HDMI_HDMI_REGS        0x00701400
#define HDMI_DVP_REGS         0x00701000
#define HDMI_PHY_REGS         0x00701d00
#define HDMI_RM_REGS          0x00702000
#define HDMI_PACKET_REGS      0x00703800
#define HDMI_METADATA_REGS    0x00704000
#define HDMI_CSC_REGS         0x00700100
#define HDMI_CEC_REGS         0x007d510800
#define HDMI_HD_REGS          0x007c720000

/* I2C/DDC offsets */
#define DDC0_OFFSET           0x00508200
#define DDC1_OFFSET           0x00508280
#define BSC_IRQ_OFFSET        0x00508380
#define MAIN_IRQ_OFFSET       0x00508400

/* GIO_AON offset */
#define GIO_AON_OFFSET        0x00517c00

/* AON interrupt controller */
#define AON_INTR_OFFSET       0x00510600

/* L2 interrupt controllers */
#define L2_INTC0_OFFSET       0x00517000
#define L2_INTC1_OFFSET       0x00510600
#define BSC_INTC_OFFSET       0x00508380

/* PCIe offsets */
#define PCIE0_OFFSET          0x00100000
#define PCIE1_OFFSET          0x00110000
#define PCIE2_OFFSET          0x00120000
#define MIP0_OFFSET           0x00130000
#define MIP1_OFFSET           0x00131000
#define PCIE_RESCAL_OFFSET    0x00119500
#define BCM_RESET_OFFSET      0x01504318

/* Lower peripheral base (GPU view) */
#define BCM2712_VC_PERI_LOW_BASE  0x7c000000

#define TYPE_BCM2712_PERIPHERALS "bcm2712-peripherals"

OBJECT_DECLARE_TYPE(BCM2712PeripheralState, BCM2712PeripheralClass, BCM2712_PERIPHERALS)

struct BCM2712PeripheralState {
    /*< private >*/
    BCM2838PeripheralState parent_obj;
    /*< public >*/

    /* Memory regions */
    MemoryRegion peri_low_mr;
    MemoryRegion peri_low_mr_alias;
    MemoryRegion mbox_mr;

    /* Pixelvalve v2 display controllers */
    SysBusDevice pixelvalve0;
    SysBusDevice pixelvalve1;
    SysBusDevice mop;
    SysBusDevice moplet;
    SysBusDevice disp_intr;
    SysBusDevice dvp;
    SysBusDevice hdmi0;
    SysBusDevice hdmi1;

    /* I2C for HDMI DDC */
    SysBusDevice ddc0;
    SysBusDevice ddc1;
    SysBusDevice bsc_irq;
    SysBusDevice main_irq;

    /* GIO_AON GPIO */
    SysBusDevice gio_aon;

    /* AON interrupt controller */
    SysBusDevice aon_intr;

    /* L2 interrupt controllers */
    SysBusDevice l2_intc0;
    SysBusDevice l2_intc1;
    SysBusDevice bsc_intc;

    /* PCIe controllers */
    SysBusDevice pcie0;
    SysBusDevice pcie1;
    SysBusDevice pcie2;
    SysBusDevice mip0;
    SysBusDevice mip1;
    SysBusDevice pcie_rescal;
    SysBusDevice bcm_reset;

    /* Mailboxes, framebuffer, property channel */
    BCM2835MboxState mboxes;
    BCM2712FBState fb;
    BCM2835PropertyState property;

    /* Unimplemented placeholders for complex devices */
    UnimplementedDeviceState clkisp;
    UnimplementedDeviceState asb;
};

struct BCM2712PeripheralClass {
    /*< private >*/
    BCM2838PeripheralClass parent_class;
    /*< public >*/
};

#endif /* BCM2712_PERIPHERALS_H */