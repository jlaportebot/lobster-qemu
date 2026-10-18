/*
 * BCM2712 peripherals emulation (Raspberry Pi 5)
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/arm/bcm2712_peripherals.h"
#include "hw/arm/bcm2712_rp1.h"
#include "hw/arm/bcm2712_v3d.h"
#include "hw/pci/pci.h"
#include "hw/usb/hcd-xhci.h"
// #include "hw/net/bcmgenet.h"
#include "hw/usb/hcd-xhci-sysbus.h"
// #include "hw/pci-host/bcm_pcie.h"
// #include "trace.h"
#include "hw/misc/bcm2835_thermal.h"
#include "hw/misc/bcm2835_rng.h"
#include "hw/misc/bcm2835_powermgt.h"

/* Lower peripheral base address on the VC (GPU) system bus */
/* Matches device tree soc ranges: child 0x7c000000 -> parent 0x7c000000 */
#define BCM2712_VC_PERI_BASE       0x7c000000

/* RP1 base */
#define RP1_BASE_OFFSET            0x400000

/* V3D base */
#define V3D_BASE_OFFSET            0x500000

/* PCIe base */
#define PCIE_BASE_OFFSET           0x100000

/* XHCI base */
#define XHCI_BASE_OFFSET           0x200000
#define XHCI_SIZE                  0x20000

/* GENET Ethernet base */
#define GENET_BASE_OFFSET          0x300000
#define GENET_SIZE                 0x20000

/* PCIe offsets (Pi 5 has 3 PCIe lanes) */
#define PCIE0_OFFSET   0x100000
#define PCIE1_OFFSET   0x120000
#define PCIE2_OFFSET   0x140000
#define PCIE_SIZE      0x20000

/* L2 INTC offsets */
#define L2_INTC_V3D_OFFSET  0x160000
#define L2_INTC_RP1_OFFSET  0x170000
#define L2_INTC_AO_OFFSET   0x180000
#define L2_INTC_HDMI0_OFFSET 0x190000
#define L2_INTC_SIZE        0x10000
#define L2_INTC_MAIN_OFFSET   0x1503000
#define L2_INTC_HDMI1_OFFSET  0x1508380
#define L2_INTC_HDMI2_OFFSET  0x1508400
#define L2_INTC_GPIO_OFFSET   0x1517b00
#define GPIO0_OFFSET          0x1508500  /* gpio@7d508500 */
#define GPIO0_SIZE            0x40
#define GPIO1_OFFSET          0x1517c00  /* gpio@7d517c00 */
#define GPIO1_SIZE            0x40
#define PINCTRL_OFFSET        0x1504100  /* pinctrl@7d504100 */
#define PINCTRL_SIZE          0x30
#define AON_PINCTRL_OFFSET    0x1510700  /* pinctrl@7d510700 */
#define AON_PINCTRL_SIZE      0x20

/* Audio base */
#define AUDIO_BASE_OFFSET    0x1A0000
#define AUDIO_SIZE           0x10000

/* Power management base */
#define PM_OFFSET            0x100000

/* DMA remap */
#define DMA_REMAP_BASE 0xc0000000
#define DMA_REMAP_SIZE 0x40000000 /* 1GB */

/* Capabilities for SD controller */

#define TYPE_BCM2712_RP1 "bcm2712-rp1"
#define RNG200_OFFSET      0x1208000  /* rng@7d208000 - matches DTB */
#define RNG200_SIZE        0x28
#define BRDG_OFFSET_2712   0x620000
#define CLOCK_ISP_OFFSET   0x630000
#define HDMI0_OFFSET       0x700000
#define HDMI1_OFFSET       0x710000

static void bcm2712_peripherals_init(Object *obj)
{
    BCM2712PeripheralState *s = BCM2712_PERIPHERALS(obj);

    /* Use parent's peri_mr which is sized at 256MB via bc_base->peri_size.
     * Do NOT create a separate peri_mr or call sysbus_init_mmio - the parent's peri_mr
     * is already registered as mmio region 0 by the base class.
     * Mailbox region is already initialized in base class. */

    /* EMMC2 */
    object_initialize_child(obj, "emmc2", &s->emmc2, TYPE_SYSBUS_SDHCI);

    /* RP1 I/O controller */
    object_initialize_child(obj, "rp1", &s->rp1, TYPE_BCM2712_RP1);
    memory_region_init(&s->rp1_mr, OBJECT(&s->rp1), "rp1",
                       BCM2712_RP1_SIZE);

    /* V3D GPU - DISABLED */
#if 0
    object_initialize_child(obj, "v3d", &s->v3d, TYPE_BCM2712_V3D);
    memory_region_init(&s->v3d_mr, OBJECT(&s->v3d), "v3d",
                       BCM2712_V3D_SIZE);
#endif

    /* GENET Ethernet controller - DISABLED */
#if 0
    object_initialize_child(obj, "genet", &s->genet, TYPE_BCM_GENET);
    memory_region_init(&s->genet_mr, OBJECT(&s->genet), "genet",
                       GENET_SIZE);
#endif

    /* XHCI USB 3.0 controller - ENABLED for USB mouse/keyboard */
    object_initialize_child(obj, "xhci", &s->xhci, TYPE_XHCI_SYSBUS);
    memory_region_init(&s->xhci_mr, OBJECT(&s->xhci), "xhci",
                       XHCI_SIZE);

/* PCIe controllers (3 lanes on Pi 5) - BCM PCIe controllers - DISABLED for now */
#if 0
    object_initialize_child(obj, "pcie0", &s->pcie0, TYPE_BCM_PCIE);
    memory_region_init(&s->pcie0_mr, OBJECT(&s->pcie0), "pcie0",
                       PCIE_SIZE);

    object_initialize_child(obj, "pcie1", &s->pcie1, TYPE_BCM_PCIE);
    memory_region_init(&s->pcie1_mr, OBJECT(&s->pcie1), "pcie1",
                       PCIE_SIZE);

    object_initialize_child(obj, "pcie2", &s->pcie2, TYPE_BCM_PCIE);
    memory_region_init(&s->pcie2_mr, OBJECT(&s->pcie2), "pcie2",
                       PCIE_SIZE);
#endif

    /* HDMI controllers - DISABLED for now */
#if 0
    object_initialize_child(obj, "hdmi0", &s->hdmi0, TYPE_BCM2712_HDMI);
    memory_region_init(&s->hdmi0_mr, OBJECT(&s->hdmi0), "hdmi0",
                       HDMI_REG_SIZE);

    object_initialize_child(obj, "hdmi1", &s->hdmi1, TYPE_BCM2712_HDMI);
    memory_region_init(&s->hdmi1_mr, OBJECT(&s->hdmi1), "hdmi1",
                       HDMI_REG_SIZE);
#endif

    /* Audio controller - DISABLED for now */
#if 0
    object_initialize_child(obj, "audio", &s->audio, TYPE_BCM2712_AUDIO);
    memory_region_init(&s->audio_mr, OBJECT(&s->audio), "audio",
                       AUDIO_REG_SIZE);
#endif

    /* Interrupt OR gates */
    object_initialize_child(obj, "mmc_irq_orgate", &s->mmc_irq_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->mmc_irq_orgate), "num-lines", 2,
                            &error_abort);

    object_initialize_child(obj, "emmc_irq_orgate", &s->emmc_irq_orgate,
                            TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->emmc_irq_orgate), "num-lines", 2,
                            &error_abort);

    /* DMA IRQ OR gates (for DMA channels 16-31) - DISABLED because
     * bcm2835-dma only has 16 channels and can't connect to IRQs 16+ */
#if 0
    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "dma_irq_orgate_%d", i);
        object_initialize_child(obj, name, &s->dma_irq_orgate[i],
                                TYPE_OR_IRQ);
        object_property_set_int(OBJECT(&s->dma_irq_orgate[i]), "num-lines", 2,
                                &error_abort);
    }
#endif
}

static void bcm2712_peripherals_realize(DeviceState *dev, Error **errp)
{
    BCM2712PeripheralState *s = BCM2712_PERIPHERALS(dev);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(dev);
    DeviceState *mmc_irq_orgate;
    DeviceState *emmc_irq_orgate;
    int n;

    /* Enable base framebuffer for Pi 5 for display output.
     * "dma-mr" is already set via object_property_add_const_link in
     * bcm2835_peripherals_realize — don't override it here. */
    object_property_set_uint(OBJECT(&s_base->fb), "vcram-size", 0x4000000, &error_abort);
    object_property_set_uint(OBJECT(&s_base->fb), "xres", 1920, &error_abort);
    object_property_set_uint(OBJECT(&s_base->fb), "yres", 1080, &error_abort);
    object_property_set_uint(OBJECT(&s_base->fb), "bpp", 32, &error_abort);

    /* Connect framebuffer IRQ to mailbox channel FB (like raspi4) */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->fb), 0,
                       qdev_get_gpio_in(DEVICE(&s_base->mboxes), MBOX_CHAN_FB));

    /* Property channel mapping is already done by
     * bcm_soc_peripherals_common_realize — don't duplicate it. */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->property), 0,
                      qdev_get_gpio_in(DEVICE(&s_base->mboxes), MBOX_CHAN_PROPERTY));

    bcm_soc_peripherals_common_realize(dev, errp);

    /* Map UART0 at Pi 5 address (0x7d001000 = peri_base + 0x1001000).
     * Create an alias at the Pi5 offset WITHOUT removing the original
     * from peri_mr. Removing it breaks the chardev connection. */
    {
        MemoryRegion *uart0_mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s_base->uart0), 0);
        MemoryRegion *uart0_pi5_alias = g_new0(MemoryRegion, 1);
        memory_region_init_alias(uart0_pi5_alias, OBJECT(s),
                                 "bcm2712-uart0-pi5", uart0_mr, 0,
                                 memory_region_size(uart0_mr));
        memory_region_add_subregion(&s_base->peri_mr, 0x1001000, uart0_pi5_alias);
    }

    /* Map peripherals into GPU address space */
    memory_region_init_alias(&s->peri_alias_mr, OBJECT(s),
                             "bcm2712-peripherals-gpu", &s_base->peri_mr, 0,
                             memory_region_size(&s_base->peri_mr));
    /* Remove peri_alias_mr from its container if any */
    if (s->peri_alias_mr.container) {
        memory_region_del_subregion(s->peri_alias_mr.container, &s->peri_alias_mr);
    }
    memory_region_add_subregion_overlap(&s_base->gpu_bus_mr,
                                        BCM2712_VC_PERI_BASE,
                                        &s->peri_alias_mr, 1);

    /* EMMC2 */
    object_property_set_uint(OBJECT(&s->emmc2), "sd-spec-version", 3,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->emmc2), "capareg",
                             BCM2835_SDHC_CAPAREG, &error_abort);
    object_property_set_bool(OBJECT(&s->emmc2), "pending-insert-quirk", true,
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emmc2), errp)) {
        return;
    }

    /* Remove EMMC2 iomem from system memory (added by sysbus_init_mmio) */
    {
        MemoryRegion *emmc2_iomem = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->emmc2), 0);
        if (emmc2_iomem && emmc2_iomem->container) {
            memory_region_del_subregion(emmc2_iomem->container, emmc2_iomem);
        }
    }

    memory_region_add_subregion(&s_base->peri_mr, EMMC2_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->emmc2), 0));

    /* RP1 I/O controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rp1), errp)) {
        return;
    }

    /* Remove RP1 iomem from system memory (added by sysbus_init_mmio) */
    {
        MemoryRegion *rp1_iomem = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rp1), 0);
        if (rp1_iomem && rp1_iomem->container) {
            memory_region_del_subregion(rp1_iomem->container, rp1_iomem);
        }
    }

    memory_region_add_subregion(&s_base->peri_mr, RP1_BASE_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rp1), 0));

    /* Add SD bus alias on peripherals object for board-level connections */
    /* Pi 5 uses EMMC2 for microSD card, not RP1 SD0 */
    object_property_add_alias(OBJECT(dev), "sd-bus", OBJECT(&s->emmc2), "sd-bus");

    /* V3D GPU - DISABLED for now */
#if 0
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->v3d), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, V3D_BASE_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->v3d), 0));
#endif

    /* GENET Ethernet controller - DISABLED for now */
#if 0
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->genet), errp)) {
        return;
    }

    /* Remove GENET iomem from system memory (added by sysbus_init_mmio) */
    {
        MemoryRegion *genet_iomem = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->genet), 0);
        if (genet_iomem && genet_iomem->container) {
            memory_region_del_subregion(genet_iomem->container, genet_iomem);
        }
    }

    memory_region_add_subregion(&s_base->peri_mr, GENET_BASE_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->genet), 0));
#endif

    /* XHCI USB 3.0 controller - ENABLED for USB mouse/keyboard */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xhci), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, XHCI_BASE_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->xhci), 0));

    /* PCIe controllers (3 lanes on Pi 5) - BCM PCIe controllers - DISABLED for now */
#if 0
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, PCIE0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie0), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, PCIE1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie1), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pcie2), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, PCIE2_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pcie2), 0));

    /* NVMe controllers on PCIe buses */
    {
        PCIBus *pcie0_bus = s->pcie0.dw_pcie.parent_obj.bus;
        if (pcie0_bus) {
            pci_create_simple(pcie0_bus, -1, "nvme");
        }
        PCIBus *pcie1_bus = s->pcie1.dw_pcie.parent_obj.bus;
        if (pcie1_bus) {
            pci_create_simple(pcie1_bus, -1, "nvme");
        }
        PCIBus *pcie2_bus = s->pcie2.dw_pcie.parent_obj.bus;
        if (pcie2_bus) {
            pci_create_simple(pcie2_bus, -1, "nvme");
        }
    }
#endif

    /* HDMI controllers - DISABLED for now */
#if 0
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hdmi0), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, HDMI0_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->hdmi0), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hdmi1), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, HDMI1_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->hdmi1), 0));
#endif

    /* Audio controller - DISABLED for now */
#if 0
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->audio), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, AUDIO_BASE_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->audio), 0));
#endif

    /* EMMC and EMMC2 share one irq */
    if (!qdev_realize(DEVICE(&s->mmc_irq_orgate), NULL, errp)) {
        return;
    }
    mmc_irq_orgate = DEVICE(&s->mmc_irq_orgate);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->sdhci), 0,
                       qdev_get_gpio_in(mmc_irq_orgate, 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emmc2), 0,
                       qdev_get_gpio_in(mmc_irq_orgate, 1));
    qdev_connect_gpio_out(mmc_irq_orgate, 0,
                          qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                 BCM2835_IC_GPU_IRQ,
                                                 INTERRUPT_ARASANSDIO));

    /* SDHOST */
    if (!qdev_realize(DEVICE(&s->emmc_irq_orgate), NULL, errp)) {
        return;
    }
    emmc_irq_orgate = DEVICE(&s->emmc_irq_orgate);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->sdhost), 0,
                       qdev_get_gpio_in(emmc_irq_orgate, 0));
    qdev_connect_gpio_out(emmc_irq_orgate, 0,
                          qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                 BCM2835_IC_GPU_IRQ,
                                                 INTERRUPT_SDIO));

    /* DMA 0-15 to GPU interrupt controller */
    for (n = 0; n < 16; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), n,
                           qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                  BCM2835_IC_GPU_IRQ,
                                                  GPU_INTERRUPT_DMA0 + n));
    }

    /* DMA 16-31 - use OR gates for shared IRQs
     * TODO: bcm2835-dma device only has 16 channels (0-15) currently.
     * The Pi 5 may have more, but they're not modeled yet.
     * Disabling for now to avoid "Property 'bcm2835-dma.sysbus-irq[16]' not found" error.
     */
#if 0
    for (n = 0; n < 4; n++) {
        if (!qdev_realize(DEVICE(&s->dma_irq_orgate[n]), NULL, errp)) {
            return;
        }
        dma_irq_orgate = DEVICE(&s->dma_irq_orgate[n]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 16 + n * 2,
                           qdev_get_gpio_in(dma_irq_orgate, 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 17 + n * 2,
                           qdev_get_gpio_in(dma_irq_orgate, 1));
        qdev_connect_gpio_out(dma_irq_orgate, 0,
                              qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                                     BCM2835_IC_GPU_IRQ,
                                                     GPU_INTERRUPT_DMA16 + n));
    }
#endif

    /* DMA 15 is special - used by GPU */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s_base->dma), 15,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GPU_INTERRUPT_DMA15));

    /* GENET Ethernet controller interrupt - connected to GIC in bcm2712.c */
#if 0
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->genet), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_GENET));
#endif

    /* XHCI USB 3.0 controller interrupt - connected in bcm2712.c */

    /* PCIe controllers interrupts - DISABLED (devices not implemented) */
#if 0
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE0_INTA));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie0), 1,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE0_INTB));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie0), 2,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE0_INTC));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie0), 3,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE0_INTD));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie0), 4,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE0_MSI));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie1), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE1_INTA));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie1), 1,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE1_INTB));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie1), 2,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE1_INTC));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie1), 3,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE1_INTD));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie1), 4,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE1_MSI));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie2), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE2_INTA));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie2), 1,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE2_INTB));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie2), 2,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE2_INTC));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie2), 3,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE2_INTD));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie2), 4,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_PCIE2_MSI));

    /* HDMI controller interrupts */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi0), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_HDMI0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hdmi1), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_HDMI1));

    /* Audio controller interrupt */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->audio), 0,
                       qdev_get_gpio_in_named(DEVICE(&s_base->ic),
                                              BCM2835_IC_GPU_IRQ,
                                              GIC_SPI_INTERRUPT_AUDIO));
#endif

    /* Thermal sensor (BCM2835 compatible) */
    object_initialize_child(OBJECT(dev), "thermal", &s->thermal, TYPE_BCM2835_THERMAL);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->thermal), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, THERMAL_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->thermal), 0));

    /* RNG (BCM2835 compatible) */
    object_initialize_child(OBJECT(dev), "rng", &s->rng, TYPE_BCM2835_RNG);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng), errp)) {
        return;
    }
    memory_region_add_subregion(&s_base->peri_mr, RNG200_OFFSET,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng), 0));

    /* Clock ISP (unimplemented) */
    create_unimp(s_base, &s->clkisp, "bcm2712-clkisp", CLOCK_ISP_OFFSET, 0x100);

    /* L2 INTCs (unimplemented) */
    create_unimp(s_base, &s->l2_intc_v3d, "bcm2712-l2-intc-v3d", L2_INTC_V3D_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_rp1, "bcm2712-l2-intc-rp1", L2_INTC_RP1_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_ao, "bcm2712-l2-intc-ao", L2_INTC_AO_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_hdmi0, "bcm2712-l2-intc-hdmi0", L2_INTC_HDMI0_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_main, "bcm2712-l2-intc-main", L2_INTC_MAIN_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_hdmi1, "bcm2712-l2-intc-hdmi1", L2_INTC_HDMI1_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_hdmi2, "bcm2712-l2-intc-hdmi2", L2_INTC_HDMI2_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->l2_intc_gpio, "bcm2712-l2-intc-gpio", L2_INTC_GPIO_OFFSET, L2_INTC_SIZE);
    create_unimp(s_base, &s->gpio0, "bcm2712-gpio0", GPIO0_OFFSET, GPIO0_SIZE);
    create_unimp(s_base, &s->gpio1, "bcm2712-gpio1", GPIO1_OFFSET, GPIO1_SIZE);
    create_unimp(s_base, &s->pinctrl, "bcm2712-pinctrl", PINCTRL_OFFSET, PINCTRL_SIZE);
    create_unimp(s_base, &s->aon_pinctrl, "bcm2712-aon-pinctrl", AON_PINCTRL_OFFSET, AON_PINCTRL_SIZE);
    create_unimp(s_base, &s->rng200, "bcm2712-rng200", RNG200_OFFSET, RNG200_SIZE);

    /* DMA remap region: 0xc0000000 -> 0x00000000 (30-bit DMA) */
    memory_region_init_alias(&s->dma_remap_mr, OBJECT(s), "dma-remap",
                             get_system_memory(), 0x00000000, DMA_REMAP_SIZE);
    memory_region_add_subregion(get_system_memory(), DMA_REMAP_BASE,
                                &s->dma_remap_mr);
}

static void bcm2712_peripherals_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM2712PeripheralClass *bc = BCM2712_PERIPHERALS_CLASS(oc);
    BCMSocPeripheralBaseClass *bc_base = BCM_SOC_PERIPHERALS_BASE_CLASS(oc);

    bc->peri_size = 0x10000000;  /* 256MB peripheral space */
    bc_base->peri_size = 0x10000000;
    dc->realize = bcm2712_peripherals_realize;
}

static const TypeInfo bcm2712_peripherals_type_info = {
    .name = TYPE_BCM2712_PERIPHERALS,
    .parent = TYPE_BCM_SOC_PERIPHERALS_BASE,
    .instance_size = sizeof(BCM2712PeripheralState),
    .instance_init = bcm2712_peripherals_init,
    .class_size = sizeof(BCM2712PeripheralClass),
    .class_init = bcm2712_peripherals_class_init,
};

static void bcm2712_peripherals_register_types(void)
{
    type_register_static(&bcm2712_peripherals_type_info);
}

type_init(bcm2712_peripherals_register_types)