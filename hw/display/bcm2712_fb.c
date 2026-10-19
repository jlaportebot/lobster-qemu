/*
 * BCM2712 Framebuffer / Pixelvalve operations
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/display/bcm2712_pixelvalve.h"

static uint64_t bcm2712_pixelvalve_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2712PixelValveState *s = (BCM2712PixelValveState *)opaque;
    uint32_t val = 0;

    switch (offset) {
    case 0x00: /* PV_REG_STATUS */
        val = 0x1; /* Pixelvalve enabled */
        break;
    case 0x04: /* PV_REG_CONTROL */
        val = 0x1; /* Enabled */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "bcm2712-pixelvalve: read offset 0x%" HWADDR_PRIx "\n", offset);
        break;
    }

    return val;
}

static void bcm2712_pixelvalve_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    switch (offset) {
    case 0x04: /* PV_REG_CONTROL */
        /* Enable/disable pixelvalve */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "bcm2712-pixelvalve: write offset 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", offset, val);
        break;
    }
}

const MemoryRegionOps bcm2712_pixelvalve_ops = {
    .read = bcm2712_pixelvalve_read,
    .write = bcm2712_pixelvalve_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};