/*
 * LobsterOS Shared Folder Device — host directory over MMIO (9p-style)
 *
 * Exports a HOST directory into the guest via simple MMIO commands:
 * stat / list / read / write / create. Paths are relative to the share
 * root (LBST_SHARE env var); traversal is jailed to the share root.
 *
 * MMIO layout (32-bit registers):
 *   0x00  MAGIC   (read) — 0x4C534852 ("LSHR")
 *   0x04  VERSION (read) — 0x00010000
 *   0x08  OP      (write) — 1=stat 2=read 3=list 4=write 5=create
 *   0x10  OFFSET  (r/w)  — file offset (read/write ops)
 *   0x14  COUNT   (r/w)  — requested bytes (chunked, max 4096)
 *   0x18  STATUS  (read) — 0 idle, 1 done, 3 error, 4 done+EOF, 5 done+more
 *   0x20  RESULT  (read) — bytes moved / file size / entries remaining
 *   0x100 NAMEBUF (r/w) — 256-byte NUL path, or dir-entry name after LIST
 *   0x1000 DATA  (r/w)  — 4096-byte payload window
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LOBSTER_SHARE_MAGIC 0x4C534852 /* "LSHR" */

#define TYPE_LOBSTER_SHARE "lobster-share"

#define LS_NAMEBUF_SIZE 256
#define LS_DATA_SIZE     4096
#define LS_MMIO_SIZE     0x2000

OBJECT_DECLARE_SIMPLE_TYPE(LobsterShareState, LOBSTER_SHARE)

struct LobsterShareState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    char *root;          /* host share root, NULL = device disabled */
    char namebuf[LS_NAMEBUF_SIZE];
    uint8_t data[LS_DATA_SIZE];

    uint32_t op;
    uint64_t offset;
    uint32_t count;
    uint32_t status;
    uint64_t result;

    /* LIST iteration state */
    DIR *list_dir;
    struct dirent *list_ent;
    uint64_t list_index;
};

enum {
    REG_MAGIC  = 0x00,
    REG_VER    = 0x04,
    REG_OP     = 0x08,
    REG_OFFSET = 0x10,
    REG_COUNT  = 0x14,
    REG_STATUS = 0x18,
    REG_RESULT = 0x20,
    REG_NAMEBUF = 0x100,
    REG_DATA   = 0x1000,
};

enum {
    OP_STAT   = 1,
    OP_READ   = 2,
    OP_LIST   = 3,
    OP_WRITE  = 4,
    OP_CREATE = 5,
};

/* Resolve a guest-supplied relative path inside the share root, refusing
 * any escape via .. or absolute paths. Returns a g_malloc'd host path. */
static char *ls_resolve(LobsterShareState *s, const char *guest_path)
{
    if (!s->root || guest_path[0] == '/') {
        return NULL;
    }
    /* Empty path = the share root itself. */
    if (guest_path[0] == '\0') {
        return g_strdup(s->root);
    }

    /* Reject any component that escapes the root. */
    const char *p = guest_path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == guest_path || p[-1] == '/')) {
            return NULL;
        }
        p++;
    }
    return g_strdup_printf("%s/%s", s->root, guest_path);
}

static void ls_do_stat(LobsterShareState *s)
{
    char *path = ls_resolve(s, s->namebuf);
    if (!path) { s->status = 3; return; }
    struct stat st;
    if (stat(path, &st) != 0) { s->status = 3; g_free(path); return; }
    s->result = (uint64_t)st.st_size;
    s->status = 1;
    g_free(path);
}

static void ls_do_read(LobsterShareState *s)
{
    char *path = ls_resolve(s, s->namebuf);
    if (!path) { s->status = 3; return; }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { s->status = 3; g_free(path); return; }
    uint32_t want = s->count;
    if (want > LS_DATA_SIZE) { want = LS_DATA_SIZE; }
    ssize_t r = pread(fd, s->data, want, (off_t)s->offset);
    close(fd);
    g_free(path);
    if (r < 0) { s->status = 3; return; }
    s->result = (uint64_t)r;
    s->status = (want > 0 && (uint32_t)r < want) ? 4 : 1;
}

static void ls_do_list(LobsterShareState *s)
{
    if (!s->list_dir) {
        char *path = ls_resolve(s, s->namebuf);
        if (!path) { s->status = 3; return; }
        s->list_dir = opendir(path);
        g_free(path);
        if (!s->list_dir) { s->status = 3; return; }
        s->list_index = 0;
    }
    /* One entry per op call: copy name to NAMEBUF, RESULT=1 if entry. */
    for (;;) {
        struct dirent *ent = readdir(s->list_dir);
        if (!ent) {
            closedir(s->list_dir);
            s->list_dir = NULL;
            s->status = 4; /* EOF */
            s->result = 0;
            return;
        }
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
            continue;
        }
        snprintf(s->namebuf, LS_NAMEBUF_SIZE, "%s", ent->d_name);
        s->result = 1;
        s->status = 1;
        return;
    }
}

static void ls_do_write(LobsterShareState *s)
{
    char *path = ls_resolve(s, s->namebuf);
    if (!path) { s->status = 3; return; }
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) { s->status = 3; g_free(path); return; }
    uint32_t have = s->count;
    if (have > LS_DATA_SIZE) { have = LS_DATA_SIZE; }
    ssize_t r = pwrite(fd, s->data, have, (off_t)s->offset);
    close(fd);
    g_free(path);
    s->result = r > 0 ? (uint64_t)r : 0;
    s->status = (r == (ssize_t)have) ? 1 : 3;
}

static void ls_do_create(LobsterShareState *s)
{
    char *path = ls_resolve(s, s->namebuf);
    if (!path) { s->status = 3; return; }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_free(path);
    if (fd < 0) { s->status = 3; return; }
    close(fd);
    s->status = 1;
}

static uint64_t lobster_share_read(void *opaque, hwaddr offset, unsigned size)
{
    LobsterShareState *s = LOBSTER_SHARE(opaque);

    switch (offset) {
    case REG_MAGIC:  return LOBSTER_SHARE_MAGIC;
    case REG_VER:    return 0x00010000;
    case REG_STATUS: return s->status;
    case REG_RESULT: return s->result;
    case REG_OFFSET: return s->offset;
    case REG_COUNT:  return s->count;
    default:
        if (offset >= REG_NAMEBUF && offset < REG_NAMEBUF + LS_NAMEBUF_SIZE) {
            return s->namebuf[offset - REG_NAMEBUF];
        }
        if (offset >= REG_DATA && offset < REG_DATA + LS_DATA_SIZE) {
            return s->data[offset - REG_DATA];
        }
        return 0;
    }
}

static void lobster_share_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    LobsterShareState *s = LOBSTER_SHARE(opaque);

    switch (offset) {
    case REG_OP:
        s->status = 0;
        if (!s->root) { s->status = 3; return; }
        switch (val) {
        case OP_STAT:   ls_do_stat(s);   break;
        case OP_READ:   ls_do_read(s);   break;
        case OP_LIST:   ls_do_list(s);   break;
        case OP_WRITE:  ls_do_write(s);  break;
        case OP_CREATE: ls_do_create(s); break;
        default: s->status = 3;
        }
        break;
    case REG_OFFSET:
        s->offset = val;
        if (val == 0 && s->list_dir) {
            closedir(s->list_dir);
            s->list_dir = NULL;
        }
        break;
    case REG_COUNT:
        s->count = (uint32_t)val;
        break;
    default:
        if (offset >= REG_NAMEBUF && offset < REG_NAMEBUF + LS_NAMEBUF_SIZE) {
            s->namebuf[offset - REG_NAMEBUF] = (uint8_t)val;
        } else if (offset >= REG_DATA && offset < REG_DATA + LS_DATA_SIZE) {
            s->data[offset - REG_DATA] = (uint8_t)val;
        }
        break;
    }
}

static const MemoryRegionOps lobster_share_mmio_ops = {
    .read = lobster_share_read,
    .write = lobster_share_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void lobster_share_realize(DeviceState *dev, Error **errp)
{
    LobsterShareState *s = LOBSTER_SHARE(dev);

    s->root = NULL;
    s->status = 0;
    s->list_dir = NULL;

    const char *dir = getenv("LBST_SHARE");
    if (dir) {
        struct stat st;
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            s->root = g_strdup(dir);
        } else {
            fprintf(stderr, "[lobster-share] LBST_SHARE is not a directory: %s\n", dir);
        }
    }
}

static void lobster_share_init(Object *obj)
{
    LobsterShareState *s = LOBSTER_SHARE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_share_mmio_ops, s,
                          "lobster-share", LS_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    s->root = NULL;
    s->list_dir = NULL;
}

static void lobster_share_finalize(Object *obj)
{
    LobsterShareState *s = LOBSTER_SHARE(obj);
    g_free(s->root);
    if (s->list_dir) {
        closedir(s->list_dir);
    }
}

static void lobster_share_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = lobster_share_realize;
    dc->desc = "LobsterOS shared folder (host directory over MMIO)";
}

static const TypeInfo lobster_share_info = {
    .name          = TYPE_LOBSTER_SHARE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterShareState),
    .instance_init = lobster_share_init,
    .instance_finalize = lobster_share_finalize,
    .class_init    = lobster_share_class_init,
};

static void lobster_share_register_types(void)
{
    type_register_static(&lobster_share_info);
}

type_init(lobster_share_register_types)
