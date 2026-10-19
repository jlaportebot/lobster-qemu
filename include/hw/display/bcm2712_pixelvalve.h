/*
 * BCM2712 Pixelvalve v2 display controller
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2712_PIXELVALVE_H
#define BCM2712_PIXELVALVE_H

#include "hw/sysbus.h"

#define TYPE_BCM2712_PIXELVALVE "bcm2712-pixelvalve"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2712PixelValveState, BCM2712_PIXELVALVE)

struct BCM2712PixelValveState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
};

extern const MemoryRegionOps bcm2712_pixelvalve_ops;

#endif /* BCM2712_PIXELVALVE_H */