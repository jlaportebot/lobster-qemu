/*
 * LobsterOS Storage Device — host-backed virtual disk (virtio-style MMIO)
 *
 * Maps a host-side raw disk image into the guest as a simple MMIO block
 * device — the same model VirtualBox/QEMU use for paravirtual storage,
 * without SDHCI emulation overhead. One MMIO command moves a full 512-byte
 * block; the host performs a pread/pwrite on the backing image.
 *
 * MMIO Layout (32-bit registers):
 *   0x00  MAGIC    (read)  — 0x4C44524B ("LDRK")
 *   0x04  VERSION  (read)  — 0x00010000 (v1.0)
 *   0x08  NUMBLKS  (read)  — total 512-byte blocks in the backing image
 *   0x0C  BLKSIZE  (read)  — 512
 *   0x10  STATUS   (read)  — 0=idle 1=busy 2=done 3=error
 *   0x14  CMD      (write) — 1=read block, 2=write block, 3=flush
 *   0x18  BLOCK    (r/w)  — block index for the next command
 *   0x1C  RESERVED
 *   0x1000 BLKBUF  (r/w)  — 512-byte data window for read/write payloads
 *
 * Guest protocol:
 *   read:  write BLOCK=n, write CMD=1, poll STATUS==2, copy BLKBUF (512B)
 *   write: copy 512B into BLKBUF, write BLOCK=n, write CMD=2, poll STATUS
 *
 * The backing image path comes from the -drive option:
 *   -drive if=none,file=disk.img,format=raw,id=lobsterdisk
 * (device looks up "lobsterdisk"; absent = no media, MAGIC still reads.)
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "system/block-backend.h"
#include "block/block.h"
#include "block/block-io.h"
#include "system/blockdev.h"
#include "migration/vmstate.h"
#include "qobject/qdict.h"
#include "qobject/qstring.h"
#include <unistd.h>
#include <fcntl.h>

#define LOBSTER_DISK_MAGIC   0x4C44524B /* "LDRK" */

#define TYPE_LOBSTER_DISK "lobster-disk"

#define LBST_BLKBUF_SIZE 512
#define LBST_MMIO_SIZE   0x2000

OBJECT_DECLARE_SIMPLE_TYPE(LobsterDiskState, LOBSTER_DISK)

struct LobsterDiskState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    BlockBackend *blk;  /* backing image, NULL = no media */
    uint64_t num_blocks;

    uint8_t buf[LBST_BLKBUF_SIZE];
    uint32_t status;
    uint64_t block;
};

enum {
    REG_MAGIC  = 0x00,
    REG_VER    = 0x04,
    REG_NBLK   = 0x08,
    REG_BLKSZ  = 0x0C,
    REG_STATUS = 0x10,
    REG_CMD    = 0x14,
    REG_BLOCK  = 0x18,
    REG_BLKBUF = 0x1000,
};

enum {
    CMD_READ  = 1,
    CMD_WRITE = 2,
    CMD_FLUSH = 3,
};

static uint64_t lobster_disk_read(void *opaque, hwaddr offset, unsigned size)
{
    LobsterDiskState *s = LOBSTER_DISK(opaque);

    switch (offset) {
    case REG_MAGIC:
        return LOBSTER_DISK_MAGIC;
    case REG_VER:
        return 0x00010000;
    case REG_NBLK:
        return s->num_blocks;
    case REG_BLKSZ:
        return LBST_BLKBUF_SIZE;
    case REG_STATUS:
        return s->status;
    default:
        if (offset >= REG_BLKBUF && offset < REG_BLKBUF + LBST_BLKBUF_SIZE) {
            if (size == 1) {
                return s->buf[offset - REG_BLKBUF];
            }
            if (size == 4) {
                uint32_t v;
                memcpy(&v, &s->buf[offset - REG_BLKBUF], sizeof(v));
                return v;
            }
        }
        return 0;
    }
}

static void lobster_disk_write(void *opaque, hwaddr offset,
                               uint64_t val, unsigned size)
{
    LobsterDiskState *s = LOBSTER_DISK(opaque);

    switch (offset) {
    case REG_CMD:
        if (!s->blk) {
            s->status = 3;
            return;
        }
        s->status = 1;
        switch (val) {
        case CMD_READ: {
            int64_t pos = (int64_t)s->block * LBST_BLKBUF_SIZE;
            /* blk_pread returns 0 on success, negative errno on failure. */
            int r = blk_pread(s->blk, pos, LBST_BLKBUF_SIZE, s->buf, 0);
            s->status = (r >= 0) ? 2 : 3;
            break;
        }
        case CMD_WRITE: {
            int64_t pos = (int64_t)s->block * LBST_BLKBUF_SIZE;
            int r = blk_pwrite(s->blk, pos, LBST_BLKBUF_SIZE, s->buf, 0);
            s->status = (r >= 0) ? 2 : 3;
            break;
        }
        case CMD_FLUSH:
            blk_flush(s->blk);
            s->status = 2;
            break;
        default:
            s->status = 3;
        }
        break;
    case REG_BLOCK:
        s->block = val;
        break;
    default:
        if (offset >= REG_BLKBUF && offset < REG_BLKBUF + LBST_BLKBUF_SIZE) {
            if (size == 1) {
                s->buf[offset - REG_BLKBUF] = (uint8_t)val;
            } else if (size == 4) {
                memcpy(&s->buf[offset - REG_BLKBUF], &val, sizeof(uint32_t));
            }
        }
        break;
    }
}

static const MemoryRegionOps lobster_disk_mmio_ops = {
    .read = lobster_disk_read,
    .write = lobster_disk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void lobster_disk_realize(DeviceState *dev, Error **errp)
{
    LobsterDiskState *s = LOBSTER_DISK(dev);

    s->blk = NULL;
    s->num_blocks = 0;
    s->status = 0;

    /*
     * Media resolution, in order:
     *   1. A -drive if=none,id=lobsterdisk backend (classic QEMU block
     *      options: qcow2 overlays, snapshots, savevm …)
     *   2. The LBST_DISKIMG environment variable, opened through the
     *      block layer so even the env workflow gets format sniffing
     *      (qcow2 works there too).
     */
    BlockBackend *blk = blk_by_name("lobsterdisk");
    if (!blk) {
        const char *img = getenv("LBST_DISKIMG");
        if (img) {
            /* Explicit raw format: probing is disallowed for writable
             * images. qcow2 accepted by extension. */
            /* blk_new_open consumes the options dict in ALL cases — never
             * unref it ourselves. Build a json: filename that pins the
             * format explicitly (writable images must not rely on
             * probing, which QEMU disables and restricts). */
            const char *fmt = g_str_has_suffix(img, ".qcow2") ? "qcow2" : "raw";
            g_autofree char *json = g_strdup_printf(
                "json:{\"driver\":\"%s\","
                "\"file\":{\"driver\":\"file\",\"filename\":\"%s\"}}",
                fmt, img);
            blk = blk_new_open(json, NULL, NULL, BDRV_O_RDWR, NULL);
            if (blk) {
                /* Attach to this device so the backend joins the main
                 * AioContext (unattached backends never dispatch I/O). */
                blk_attach_dev(blk, dev);
                /* Grant ourselves the permissions we opened with. */
                blk_set_perm(blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                             BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE, NULL);
            }
        }
    }
    if (blk) {
        int64_t len = blk_getlength(blk);
        if (len > 0) {
            s->blk = blk;
            s->num_blocks = (uint64_t)len / LBST_BLKBUF_SIZE;
        } else if (blk_by_name("lobsterdisk") != blk) {
            blk_unref(blk);
        }
    }
}

static bool lobster_disk_vmstate_needed(void *opaque)
{
    return true;
}

static const VMStateDescription lobster_disk_vmstate = {
    .name = "lobster-disk",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lobster_disk_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(buf, LobsterDiskState, LBST_BLKBUF_SIZE),
        VMSTATE_UINT32(status, LobsterDiskState),
        VMSTATE_UINT64(block, LobsterDiskState),
        VMSTATE_END_OF_LIST()
    },
};

static void lobster_disk_init(Object *obj)
{
    LobsterDiskState *s = LOBSTER_DISK(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_disk_mmio_ops, s,
                          "lobster-disk", LBST_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    s->blk = NULL;
}

static void lobster_disk_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = lobster_disk_realize;
    dc->vmsd = &lobster_disk_vmstate;
    dc->desc = "LobsterOS host-backed virtual disk";
}

static const TypeInfo lobster_disk_info = {
    .name          = TYPE_LOBSTER_DISK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterDiskState),
    .instance_init = lobster_disk_init,
    .class_init    = lobster_disk_class_init,
};

static void lobster_disk_register_types(void)
{
    type_register_static(&lobster_disk_info);
}

type_init(lobster_disk_register_types)
