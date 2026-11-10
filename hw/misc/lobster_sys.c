/*
 * LobsterOS System Control Device — power, wall clock, host display hint
 *
 * The raspi5b machine boots bare-metal guests at EL3, which disables QEMU's
 * PSCI conduit, and it has no RTC. This device gives the guest the three
 * things a desktop OS needs from its platform and cannot otherwise get:
 *
 *   - power control: a guest write ends the VM (shutdown) or resets it
 *   - wall-clock time: host UTC epoch seconds plus the host's UTC offset
 *   - display hint: the resolution the VM manager would like the guest to use
 *
 * MMIO layout (little-endian, 32-bit registers):
 *   0x00  MAGIC      (r)  0x4C535953 ("LSYS")
 *   0x04  VERSION    (r)  0x00010000
 *   0x08  POWER      (w)  1 = power off, 2 = reboot
 *   0x0C  STATE      (rw) guest lifecycle state, free-form (0 boot, 1 desktop,
 *                         2 shutting down); visible to the host via `xp`
 *   0x10  RTC_LO     (r)  UTC epoch seconds, low word; reading it latches HI
 *   0x14  RTC_HI     (r)  UTC epoch seconds, high word (latched)
 *   0x18  TZ_OFFSET  (r)  host local-time offset from UTC in seconds (signed)
 *   0x1C  RTC_NSEC   (r)  nanoseconds within the latched second
 *   0x20  HINT_WIDTH (r)  preferred framebuffer width  (0 = no preference)
 *   0x24  HINT_HEIGHT(r)  preferred framebuffer height (0 = no preference)
 *
 * The display hint comes from the "width"/"height" device properties, which
 * raspi5b.c fills from the LBST_RES=WxH environment variable.
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "system/runstate.h"
#include "qom/object.h"

#define LOBSTER_SYS_MAGIC   0x4C535953 /* "LSYS" */
#define LOBSTER_SYS_VERSION 0x00010000

#define LOBSTER_SYS_POWER_OFF    1
#define LOBSTER_SYS_POWER_REBOOT 2

#define TYPE_LOBSTER_SYS "lobster-sys"
OBJECT_DECLARE_SIMPLE_TYPE(LobsterSysState, LOBSTER_SYS)

struct LobsterSysState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t guest_state;
    uint32_t rtc_hi_latch;
    uint32_t rtc_nsec_latch;
    uint32_t hint_width;
    uint32_t hint_height;
};

enum {
    REG_MAGIC       = 0x00,
    REG_VERSION     = 0x04,
    REG_POWER       = 0x08,
    REG_STATE       = 0x0C,
    REG_RTC_LO      = 0x10,
    REG_RTC_HI      = 0x14,
    REG_TZ_OFFSET   = 0x18,
    REG_RTC_NSEC    = 0x1C,
    REG_HINT_WIDTH  = 0x20,
    REG_HINT_HEIGHT = 0x24,
};

static int32_t lobster_sys_tz_offset(void)
{
    time_t now = time(NULL);
    struct tm local;

    if (!localtime_r(&now, &local)) {
        return 0;
    }
    return (int32_t)local.tm_gmtoff;
}

static uint64_t lobster_sys_read(void *opaque, hwaddr offset, unsigned size)
{
    LobsterSysState *s = LOBSTER_SYS(opaque);

    switch (offset) {
    case REG_MAGIC:
        return LOBSTER_SYS_MAGIC;
    case REG_VERSION:
        return LOBSTER_SYS_VERSION;
    case REG_STATE:
        return s->guest_state;
    case REG_RTC_LO: {
        int64_t ns = qemu_clock_get_ns(QEMU_CLOCK_HOST);
        uint64_t secs = ns / NANOSECONDS_PER_SECOND;

        s->rtc_hi_latch = secs >> 32;
        s->rtc_nsec_latch = ns % NANOSECONDS_PER_SECOND;
        return (uint32_t)secs;
    }
    case REG_RTC_HI:
        return s->rtc_hi_latch;
    case REG_TZ_OFFSET:
        return (uint32_t)lobster_sys_tz_offset();
    case REG_RTC_NSEC:
        return s->rtc_nsec_latch;
    case REG_HINT_WIDTH:
        return s->hint_width;
    case REG_HINT_HEIGHT:
        return s->hint_height;
    default:
        return 0;
    }
}

static void lobster_sys_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    LobsterSysState *s = LOBSTER_SYS(opaque);

    switch (offset) {
    case REG_POWER:
        if (val == LOBSTER_SYS_POWER_OFF) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        } else if (val == LOBSTER_SYS_POWER_REBOOT) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case REG_STATE:
        s->guest_state = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps lobster_sys_mmio_ops = {
    .read = lobster_sys_read,
    .write = lobster_sys_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void lobster_sys_reset(DeviceState *dev)
{
    LobsterSysState *s = LOBSTER_SYS(dev);

    s->guest_state = 0;
    s->rtc_hi_latch = 0;
    s->rtc_nsec_latch = 0;
}

static void lobster_sys_init(Object *obj)
{
    LobsterSysState *s = LOBSTER_SYS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_sys_mmio_ops, s,
                          "lobster-sys", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static const Property lobster_sys_properties[] = {
    DEFINE_PROP_UINT32("width", LobsterSysState, hint_width, 0),
    DEFINE_PROP_UINT32("height", LobsterSysState, hint_height, 0),
};

static void lobster_sys_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "LobsterOS system control (power, RTC, display hint)";
    device_class_set_legacy_reset(dc, lobster_sys_reset);
    device_class_set_props(dc, lobster_sys_properties);
}

static const TypeInfo lobster_sys_info = {
    .name          = TYPE_LOBSTER_SYS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterSysState),
    .instance_init = lobster_sys_init,
    .class_init    = lobster_sys_class_init,
};

static void lobster_sys_register_types(void)
{
    type_register_static(&lobster_sys_info);
}

type_init(lobster_sys_register_types)
