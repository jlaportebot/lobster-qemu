/*
 * LobsterOS QEMU Network Device — MMIO-based HTTP proxy + C compiler
 *
 * Provides a simple memory-mapped interface that lets the guest kernel
 * request HTTP fetches via the host's curl binary, and compile C source
 * via the host's aarch64-linux-gnu-gcc cross-compiler. This avoids needing
 * a full network stack or compiler toolchain on the emulated system.
 *
 * Protocol for HTTP fetch:
 *   1. Guest writes URL bytes to DATA region (offset 0x100+)
 *   2. Guest writes URL length to URL_LEN register (0x20)
 *   3. Guest writes 1 to CMD register (0x24) to fetch
 *   4. Device runs curl on the host, stores response in shared buffer
 *   5. Guest polls STATUS register (0x28): 0=idle, 1=fetching, 2=done, 3=error
 *   6. Guest reads RESP_LEN (0x2C) for body length, RESP_CODE (0x30) for HTTP code
 *   7. Guest reads body from RESP_DATA region (offset 0x1000+)
 *   8. Guest writes 2 to CMD to reset (clear buffers)
 *
 * Protocol for C compilation:
 *   1. Guest writes C source bytes to DATA region (offset 0x100+)
 *   2. Guest writes source length to URL_LEN register (0x20)
 *   3. Guest writes 3 to CMD register (0x24) to compile
 *   4. Device writes source to temp file, runs aarch64-linux-gnu-gcc,
 *      reads resulting ELF binary into response buffer
 *   5. Guest polls STATUS register (0x28): 0=idle, 1=compiling, 2=done, 3=error
 *   6. Guest reads RESP_LEN (0x2C) for ELF size, RESP_CODE for 0=ok / 1=compile-error
 *   7. Guest reads ELF from RESP_DATA region (offset 0x1000+)
 *
 * MMIO Layout (32-bit registers):
 *   0x00  MAGIC        (read-only)  — 0x4C4E4554 ("LNET")
 *   0x04  VERSION      (read-only)  — 0x00020000 (v1.1 — added compile)
 *   0x08  BUF_SIZE     (read-only)  — total MMIO size
 *   0x0C  RESERVED
 *   0x10  URL_OFFSET   (read-only)  — 0x100 (where source/URL data starts)
 *   0x14  RESP_OFFSET  (read-only)  — 0x1000 (where response data starts)
 *   0x18  URL_MAX      (read-only)  — 3840 (0xF00 bytes between offset 0x100 and 0x1000)
 *   0x1C  RESP_MAX     (read-only)  — 65536
 *   0x20  URL_LEN      (read/write) — number of bytes written to source/URL region
 *   0x24  CMD          (write-only) — 1=fetch, 2=reset, 3=compile
 *   0x28  STATUS       (read-only)  — 0=idle, 1=busy, 2=done, 3=error
 *   0x2C  RESP_LEN     (read-only)  — response body length
 *   0x30  RESP_CODE    (read-only)  — HTTP code (fetch) or 0=ok/1=error (compile)
 *   0x34  AUTH_MODE    (read/write) — 0=none, 1=Bearer token auth. When set,
 *                                     url_buf is "<token>\n<url>". Token is
 *                                     restricted to [A-Za-z0-9._-] and passed
 *                                     to curl as: -H "Authorization: Bearer X"
 *   0x100+   SRC_DATA  (write)      — URL bytes or C source (up to 3840)
 *   0x1000+  RESP_DATA  (read)       — response body or compiled ELF (up to 65536)
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include <glib/gstdio.h>
#include <ctype.h>

#define TYPE_LOBSTER_NET "lobster-net"
OBJECT_DECLARE_SIMPLE_TYPE(LobsterNetState, LOBSTER_NET)

#define LOBSTER_NET_MAGIC       0x4C4E4554u  /* "LNET" */
#define LOBSTER_NET_VERSION     0x00030000u  /* v1.2 — added Bearer-token auth for HTTP fetch */
#define LOBSTER_URL_MAX         3840          /* 0xF00: space between 0x100 and 0x1000 */
#define LOBSTER_RESP_MAX        524288        /* 512KB for MicroPython binaries */
#define LOBSTER_URL_OFFSET      0x100
#define LOBSTER_RESP_OFFSET     0x1000
#define LOBSTER_MMIO_SIZE       (LOBSTER_RESP_OFFSET + LOBSTER_RESP_MAX)

/* Status codes */
#define LOBSTER_LIBC_DIR "/mnt/data/sd-overflow/LobsterOS/lobster-os/tools/libc"

#define NET_STATUS_IDLE         0
#define NET_STATUS_FETCHING     1   /* also used for "compiling" */
#define NET_STATUS_DONE        2
#define NET_STATUS_ERROR        3

/* Commands */
#define NET_CMD_FETCH           1
#define NET_CMD_RESET           2
#define NET_CMD_COMPILE         3

struct LobsterNetState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* URL/source buffer */
    uint8_t url_buf[LOBSTER_URL_MAX];
    uint32_t url_len;

    /* Response buffer */
    uint8_t resp_buf[LOBSTER_RESP_MAX];
    uint32_t resp_len;
    uint32_t resp_code;

    /* Status */
    uint32_t status;

    /* Auth mode: 0 = none, 1 = Bearer token (carried as "<token>\n<url>"
     * in url_buf). Lets the guest make authenticated API calls (e.g. GitHub). */
    uint32_t auth_mode;
};

/* ─── Host-side HTTP fetch via curl ─────────────────────────────── */

static void lobster_net_fetch(LobsterNetState *s)
{
    char url[LOBSTER_URL_MAX + 1];
    char token[768] = {0};
    const char *url_ptr = (const char *)s->url_buf;
    uint32_t url_len = s->url_len;
    int has_auth = 0;

    /*
     * When auth_mode is set, the buffer is "<token>\n<url>". Split on the
     * first newline so we only ever append a fixed-format header to curl.
     * Token characters are restricted to [A-Za-z0-9._-] to keep the
     * header value well-formed.
     */
    if (s->auth_mode == 1) {
        uint32_t nl = 0;
        while (nl < s->url_len && s->url_buf[nl] != '\n') {
            nl++;
        }
        if (nl < s->url_len && nl > 0) {
            uint32_t tlen = nl < sizeof(token) - 1 ? nl : sizeof(token) - 1;
            uint32_t i;
            for (i = 0; i < tlen; i++) {
                unsigned char c = s->url_buf[i];
                if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
                    break;  /* reject token with metacharacters */
                }
            }
            if (i == tlen) {
                memcpy(token, s->url_buf, tlen);
                url_ptr += nl + 1;
                url_len = s->url_len - nl - 1;
                has_auth = 1;
            }
        }
        /* reset auth so the next fetch is anon unless the guest re-sets it */
        s->auth_mode = 0;
    }

    /* Null-terminate URL */
    if (url_len >= sizeof(url)) {
        url_len = sizeof(url) - 1;
    }
    memcpy(url, url_ptr, url_len);
    url[url_len] = '\0';

    /* Validate URL starts with http */
    if (s->url_len < 7 || (strncmp(url, "http://", 7) != 0 &&
                            strncmp(url, "https://", 8) != 0)) {
        s->status = NET_STATUS_ERROR;
        s->resp_code = 0;
        s->resp_len = 0;
        return;
    }

    /*
     * Run curl directly (no shell): silent, body to a temp file (binary
     * safe), HTTP code on stdout, follow redirects, max-time 10s.
     */
    char *body_path = NULL;
    int body_fd = g_file_open_tmp("lobster_net_XXXXXX", &body_path, NULL);
    if (body_fd < 0) {
        s->status = NET_STATUS_ERROR;
        s->resp_code = 0;
        s->resp_len = 0;
        return;
    }
    close(body_fd);

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    const char *fixed[] = { "curl", "-s", "-o", body_path, "-w",
                            "%{http_code}", "-L", "--max-time", "10" };
    for (size_t k = 0; k < G_N_ELEMENTS(fixed); k++) {
        g_ptr_array_add(argv, g_strdup(fixed[k]));
    }
    if (has_auth) {
        g_ptr_array_add(argv, g_strdup("-H"));
        g_ptr_array_add(argv, g_strdup_printf("Authorization: Bearer %s",
                                              token));
    }
    g_ptr_array_add(argv, g_strdup(url));
    g_ptr_array_add(argv, NULL);

    gchar *out = NULL;
    GError *gerr = NULL;
    gboolean spawned = g_spawn_sync(NULL, (char **)argv->pdata, NULL,
                                    G_SPAWN_SEARCH_PATH |
                                    G_SPAWN_STDERR_TO_DEV_NULL,
                                    NULL, NULL, &out, NULL, NULL, &gerr);
    g_ptr_array_free(argv, TRUE);
    gchar *body = NULL;
    gsize body_len = 0;
    if (!spawned) {
        g_error_free(gerr);
    } else if (!g_file_get_contents(body_path, &body, &body_len, NULL)) {
        body = NULL;
    }
    g_unlink(body_path);
    g_free(body_path);
    if (!spawned) {
        s->status = NET_STATUS_ERROR;
        s->resp_code = 0;
        s->resp_len = 0;
        g_free(out);
        return;
    }

    body_len = MIN(body_len, LOBSTER_RESP_MAX - 16);
    if (body) {
        memcpy(s->resp_buf, body, body_len);
    }
    s->resp_len = body_len;
    s->resp_code = out ? (uint32_t)atoi(out) : 0;
    g_free(body);
    g_free(out);

    s->status = NET_STATUS_DONE;

    fprintf(stderr, "[lobster-net] Fetched %s: HTTP %u, %u bytes\n",
            url, s->resp_code, s->resp_len);
}

/* ─── Host-side C compilation via aarch64-linux-gnu-gcc ─────────── */

static void lobster_net_compile_fail(LobsterNetState *s)
{
    s->status = NET_STATUS_ERROR;
    s->resp_code = 1;
    s->resp_len = 0;
}

/*
 * Build the cross-compiler command:
 * -ffreestanding: no hosted environment, no libc
 * -nostdlib: no standard library link
 * -static: fully static binary
 * -Wl,-Ttext=0x10000000: place text at USER_CODE_START
 * -Wl,--build-id=none: no build-id section
 * -Wl,--nmagic: minimize file alignment (no 64KB page gaps)
 * -O2: optimize for size
 * The resulting binary is a static AArch64 ELF that runs at EL0.
 */
static gboolean lobster_net_run_gcc(const char *out_path, const char *src_path,
                                    gchar **err_text, gint *exit_status)
{
    char *out_arg = g_strdup(out_path);
    char *src_arg = g_strdup(src_path);
    char *argv[] = {
        (char *)"aarch64-linux-gnu-gcc",
        (char *)"-ffreestanding", (char *)"-nostdlib", (char *)"-static",
        (char *)"-O2", (char *)"-nostartfiles",
        (char *)"-ffunction-sections", (char *)"-fdata-sections",
        (char *)"-Wl,-Ttext=0x10000000", (char *)"-Wl,--build-id=none",
        (char *)"-Wl,--nmagic", (char *)"-Wl,--gc-sections",
        (char *)"-I" LOBSTER_LIBC_DIR "/include", (char *)"-L" LOBSTER_LIBC_DIR,
        (char *)"-o", out_arg, src_arg, (char *)"-llobsterc", NULL
    };
    GError *gerr = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL,
                               NULL, NULL, err_text, exit_status, &gerr);

    if (!ok) {
        *err_text = g_strdup(gerr->message);
        g_error_free(gerr);
    }
    g_free(out_arg);
    g_free(src_arg);
    return ok;
}

static void lobster_net_compile(LobsterNetState *s)
{
    char *src_path = NULL;
    char *out_path = NULL;
    char *err_text = NULL;
    gchar *elf = NULL;
    gsize elf_size = 0;
    gint exit_status = 0;
    int src_fd = g_file_open_tmp("lobster_cc_XXXXXX.c", &src_path, NULL);
    int out_fd = g_file_open_tmp("lobster_cc_XXXXXX.elf", &out_path, NULL);

    if (src_fd >= 0) {
        ssize_t written = write(src_fd, s->url_buf, s->url_len);
        close(src_fd);
        if (written != (ssize_t)s->url_len) {
            src_fd = -1;
        }
    }
    if (out_fd >= 0) {
        close(out_fd);
    }
    if (src_fd < 0 || out_fd < 0) {
        fprintf(stderr, "[lobster-net] compile: cannot prepare temp files\n");
        lobster_net_compile_fail(s);
        goto out;
    }

    fprintf(stderr, "[lobster-net] compile: wrote %u bytes to %s\n",
            s->url_len, src_path);

    if (!lobster_net_run_gcc(out_path, src_path, &err_text, &exit_status) ||
        exit_status != 0) {
        /* Compilation failed: hand the compiler diagnostics to the guest */
        size_t err_len = err_text ? MIN(strlen(err_text), LOBSTER_RESP_MAX - 1)
                                  : 0;
        lobster_net_compile_fail(s);
        memcpy(s->resp_buf, err_text, err_len);
        s->resp_buf[err_len] = '\0';
        s->resp_len = err_len;
        fprintf(stderr, "[lobster-net] compile: FAILED (ret=%d)\n",
                exit_status);
        if (err_len > 0) {
            fprintf(stderr, "[lobster-net] compile error: %s\n",
                    (char *)s->resp_buf);
        }
        goto out;
    }

    if (!g_file_get_contents(out_path, &elf, &elf_size, NULL)) {
        lobster_net_compile_fail(s);
        goto out;
    }
    elf_size = MIN(elf_size, LOBSTER_RESP_MAX);
    memcpy(s->resp_buf, elf, elf_size);
    s->resp_len = elf_size;
    s->resp_code = 0;
    s->status = NET_STATUS_DONE;
    fprintf(stderr, "[lobster-net] compile: OK, ELF %u bytes\n", s->resp_len);

out:
    if (src_path) {
        g_unlink(src_path);
    }
    if (out_path) {
        g_unlink(out_path);
    }
    g_free(src_path);
    g_free(out_path);
    g_free(err_text);
    g_free(elf);
}

/* ─── MMIO read/write handlers ──────────────────────────────────── */

static uint64_t lobster_net_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    LobsterNetState *s = LOBSTER_NET(opaque);

    switch (offset) {
    case 0x00: return LOBSTER_NET_MAGIC;
    case 0x04: return LOBSTER_NET_VERSION;
    case 0x08: return LOBSTER_MMIO_SIZE;
    case 0x0C: return 0;
    case 0x10: return LOBSTER_URL_OFFSET;
    case 0x14: return LOBSTER_RESP_OFFSET;
    case 0x18: return LOBSTER_URL_MAX;
    case 0x1C: return LOBSTER_RESP_MAX;
    case 0x20: return s->url_len;
    case 0x28: return s->status;
    case 0x2C: return s->resp_len;
    case 0x30: return s->resp_code;
    case 0x34: return s->auth_mode;
    default: {
        /* URL/source data region */
        if (offset >= LOBSTER_URL_OFFSET && offset < LOBSTER_RESP_OFFSET) {
            uint32_t idx = offset - LOBSTER_URL_OFFSET;
            if (idx + size <= LOBSTER_URL_MAX) {
                uint64_t val = 0;
                memcpy(&val, &s->url_buf[idx], size);
                return val;
            }
        }
        /* Response data region */
        if (offset >= LOBSTER_RESP_OFFSET) {
            uint32_t idx = offset - LOBSTER_RESP_OFFSET;
            if (idx + size <= LOBSTER_RESP_MAX) {
                uint64_t val = 0;
                memcpy(&val, &s->resp_buf[idx], size);
                return val;
            }
        }
        return 0;
    }
    }
}

static void lobster_net_mmio_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    LobsterNetState *s = LOBSTER_NET(opaque);

    switch (offset) {
    case 0x20: /* URL_LEN */
        s->url_len = (uint32_t)val;
        if (s->url_len > LOBSTER_URL_MAX) {
            s->url_len = LOBSTER_URL_MAX;
        }
        break;

    case 0x24: /* CMD */
        switch ((uint32_t)val) {
        case NET_CMD_FETCH:
            s->status = NET_STATUS_FETCHING;
            lobster_net_fetch(s);
            break;
        case NET_CMD_RESET:
            s->status = NET_STATUS_IDLE;
            s->url_len = 0;
            s->resp_len = 0;
            s->resp_code = 0;
            s->auth_mode = 0;
            memset(s->url_buf, 0, LOBSTER_URL_MAX);
            memset(s->resp_buf, 0, LOBSTER_RESP_MAX);
            break;
        case NET_CMD_COMPILE:
            s->status = NET_STATUS_FETCHING;
            lobster_net_compile(s);
            break;
        default:
            break;
        }
        break;

    case 0x30:
    case 0x34: /* AUTH_MODE — 0=none, 1=bearer */
        s->auth_mode = (uint32_t)val & 1;
        break;

    default: {
        /* URL/source data region */
        if (offset >= LOBSTER_URL_OFFSET && offset < LOBSTER_RESP_OFFSET) {
            uint32_t idx = offset - LOBSTER_URL_OFFSET;
            if (idx + size <= LOBSTER_URL_MAX) {
                memcpy(&s->url_buf[idx], &val, size);
            }
        }
        break;
    }
    }
}

static const MemoryRegionOps lobster_net_mmio_ops = {
    .read = lobster_net_mmio_read,
    .write = lobster_net_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
};

/* ─── Device realization ────────────────────────────────────────── */

static void lobster_net_init(Object *obj)
{
    LobsterNetState *s = LOBSTER_NET(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_net_mmio_ops,
                          s, "lobster-net", LOBSTER_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->url_len = 0;
    s->resp_len = 0;
    s->resp_code = 0;
    s->status = NET_STATUS_IDLE;
    s->auth_mode = 0;
    memset(s->url_buf, 0, LOBSTER_URL_MAX);
    memset(s->resp_buf, 0, LOBSTER_RESP_MAX);
}

static void lobster_net_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->user_creatable = true;
}

static const TypeInfo lobster_net_info = {
    .name = TYPE_LOBSTER_NET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterNetState),
    .instance_init = lobster_net_init,
    .class_init = lobster_net_class_init,
};

static void lobster_net_register_type(void)
{
    type_register_static(&lobster_net_info);
}

type_init(lobster_net_register_type)
