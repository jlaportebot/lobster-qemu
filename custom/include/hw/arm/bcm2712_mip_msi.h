/*
 * BCM2712 MIP MSI Controller
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_MIP_MSI_H
#define BCM2712_MIP_MSI_H

#include "hw/pci/msi.h"
#include "hw/sysbus.h"

#define TYPE_BCM2712_MIP_MSI "bcm2712-mip-msi"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712MIPMSIState, BCM2712_MIP_MSI)

struct BCM2712MIPMSIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t spi_base;
    uint32_t numSpi;
    uint32_t spi_offset;
    uint32_t pci_addr;
};

#define BCM2712_MIP_MSI(obj) OBJECT_CHECK(BCM2712MIPMSIState, (obj), TYPE_BCM2712_MIP_MSI)

#endif