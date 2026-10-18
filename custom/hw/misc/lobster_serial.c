/*
 * LobsterOS Fast Serial Device — bulk UART TX window
 *
 * A zero-setup MMIO companion to the PL011 that lets the guest emit
 * console output with ONE MMIO write per chunk instead of two MMIO ops
 * per byte (FR poll + DR write). On TCG this removes ~600k memory
 * dispatch round-trips for a 300KB serial dump.
 *
 * Protocol:
 *   Guest writes up to 2048 bytes of payload to TXBUF (offset 0x100).
 *   Guest writes the payload length (1..2048) to TXLEN (offset 0x00).
 *   The device forwards the buffer to the serial chardev in one call.
 *
 * MMIO Layout:
 *   0x00  TXLEN   (write) — number of bytes to flush from TXBUF
 *   0x04  MAGIC   (read)  — 0x4C425558 ("LBUF")
 *   0x08  VERSION (read)  — 0x00010000 (v1.0)
 *   0x0C  BUFSIZE (read)  — 2048
 *   0x100 TXBUF   (write) — payload bytes (up to 2048)
 *
 * The device creates NO chardev of its own: it forwards to the
 * machine's default serial backend ("serial0") when present, so output
 * lands in the same -serial stdio stream as the PL011.
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/char/pl011.h"
#include "qom/object.h"

#define LOBSTER_SERIAL_BUFSIZE 2048
#define LOBSTER_SERIAL_MAGIC   0x4C425558 /* "LBUF" */

#define TYPE_LOBSTER_SERIAL "lobster-serial"
OBJECT_DECLARE_SIMPLE_TYPE(LobsterSerialState, LOBSTER_SERIAL)

struct LobsterSerialState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    /* Set by raspi5b.c machine init: the PL011 whose CharBackend we
     * forward bulk output into. */
    PL011State *pl011;

    uint8_t buf[LOBSTER_SERIAL_BUFSIZE];
};

enum {
    REG_TXLEN   = 0x00,
    REG_MAGIC   = 0x04,
    REG_VERSION = 0x08,
    REG_BUFSIZE = 0x0C,
    REG_TXBUF   = 0x100,
};

static uint64_t lobster_serial_read(void *opaque, hwaddr offset, unsigned size)
{
    switch (offset) {
    case REG_MAGIC:
        return LOBSTER_SERIAL_MAGIC;
    case REG_VERSION:
        return 0x00010000;
    case REG_BUFSIZE:
        return LOBSTER_SERIAL_BUFSIZE;
    default:
        return 0;
    }
}

static void lobster_serial_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    LobsterSerialState *s = LOBSTER_SERIAL(opaque);

    switch (offset) {
    case REG_TXLEN: {
        uint32_t len = val;
        if (len == 0 || len > LOBSTER_SERIAL_BUFSIZE) {
            return;
        }
        if (s->pl011) {
            qemu_chr_fe_write_all(&s->pl011->chr, s->buf, len);
        }
        break;
    }
    default:
        if (offset >= REG_TXBUF &&
            offset < REG_TXBUF + LOBSTER_SERIAL_BUFSIZE) {
            /* Byte-stream payload window: 1 byte per write, any size. */
            if (size == 1) {
                s->buf[offset - REG_TXBUF] = (uint8_t)val;
            }
        }
        break;
    }
}

static const MemoryRegionOps lobster_serial_mmio_ops = {
    .read = lobster_serial_read,
    .write = lobster_serial_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void lobster_serial_realize(DeviceState *dev, Error **errp)
{
    /*
     * No chardev of our own: the machine's PL011 already owns the serial
     * backend. raspi5b.c stores a pointer to that PL011 in the "pl011"
     * link property at machine init; we forward bulk chunks straight into
     * its CharBackend so output lands in the same stream with zero extra
     * configuration.
     */
}

static void lobster_serial_init(Object *obj)
{
    LobsterSerialState *s = LOBSTER_SERIAL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_serial_mmio_ops, s,
                          "lobster-serial", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);

    object_property_add_link(obj, "pl011", TYPE_PL011,
                              (Object **)&s->pl011,
                              object_property_allow_set_link,
                              OBJ_PROP_LINK_STRONG);
}

static void lobster_serial_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = lobster_serial_realize;
    dc->desc = "LobsterOS bulk serial TX window";
}

static const TypeInfo lobster_serial_info = {
    .name          = TYPE_LOBSTER_SERIAL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterSerialState),
    .instance_init = lobster_serial_init,
    .class_init    = lobster_serial_class_init,
};

static void lobster_serial_register_types(void)
{
    type_register_static(&lobster_serial_info);
}

type_init(lobster_serial_register_types)
