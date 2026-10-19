/*
 * LobsterOS Audio Device — guest PCM over MMIO to host speakers
 *
 * The guest pushes 16-bit mono PCM chunks into a 4 KiB MMIO window and
 * writes LEN; the host plays them through QEMU's audio backend
 * (PipeWire/ALSA/Pulse — whatever the host provides), honoring RATE
 * and VOL(ume) registers.
 *
 * MMIO layout (32-bit registers):
 *   0x00   MAGIC (read)  — 0x4C415544 ("LAUD")
 *   0x04   VERSION (read) — 0x00010000
 *   0x08   RATE   (r/w)  — sample rate, default 8000 Hz
 *   0x0C   VOL    (r/w)  — volume 0..100, default 100
 *   0x10   LEN    (write)— play LEN bytes of PCM from the window
 *   0x14   STATUS (read) — 0 idle, 1 playing, 2 done
 *   0x1000 WAVE   (write) — 4096-byte PCM window (s16 mono)
 *
 * Copyright (C) 2026 Mister Lobster
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "audio/audio.h"

#define LOBSTER_AUDIO_MAGIC 0x4C415544 /* "LAUD" */

#define TYPE_LOBSTER_AUDIO "lobster-audio"

#define LA_WAVE_SIZE 4096
#define LA_MMIO_SIZE 0x2000

OBJECT_DECLARE_SIMPLE_TYPE(LobsterAudioState, LOBSTER_AUDIO)

struct LobsterAudioState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    QEMUSoundCard card;
    SWVoiceOut *voice;

    uint32_t rate;
    uint32_t volume;
    uint32_t status;
    uint8_t wave[LA_WAVE_SIZE];

    /* Guest→host sample ring (16-bit mono) */
    int16_t ring[LA_WAVE_SIZE];
    uint32_t ring_head; /* read pos */
    uint32_t ring_tail; /* write pos, in samples; empty iff head==tail */
};

enum {
    REG_MAGIC  = 0x00,
    REG_VER    = 0x04,
    REG_RATE   = 0x08,
    REG_VOL    = 0x0C,
    REG_LEN    = 0x10,
    REG_STATUS = 0x14,
    REG_WAVE   = 0x1000,
};

static void la_update_volume(LobsterAudioState *s)
{
    if (!s->voice) {
        return;
    }
    /* 0..100 → 0..255 per-channel volume, mute when zero. */
    uint8_t vol = (uint8_t)(s->volume * 255 / 100);
    AUD_set_volume_out(s->voice, s->volume == 0, vol, vol);
}

static uint32_t la_ring_count(LobsterAudioState *s)
{
    return s->ring_tail - s->ring_head; /* both wrap-safe with u32 mono idx */
}

/* QEMU's mixer asks for more samples: drain the guest ring. */
static void la_callback(void *opaque, int avail)
{
    LobsterAudioState *s = opaque;
    if (!s->voice) {
        return;
    }
    static struct { int16_t l; int16_t r; } frame;
    while (avail > 0 && la_ring_count(s) > 0) {
        int16_t sample = s->ring[s->ring_head % (LA_WAVE_SIZE / 2)];
        s->ring_head++;
        frame.l = sample;
        frame.r = sample;
        AUD_write(s->voice, &frame, sizeof(frame));
        avail -= (int)sizeof(frame);
    }
}

static void la_ensure_voice(LobsterAudioState *s)
{
    if (s->voice) {
        return;
    }
    struct audsettings as = {
        .freq = s->rate ? s->rate : 8000,
        .nchannels = 2,
        .fmt = AUDIO_FORMAT_S16,
        .endianness = 0,
    };
    s->voice = AUD_open_out(&s->card, s->voice, "lobster-audio",
                            s, la_callback, &as);
    if (s->voice) {
        la_update_volume(s);
        AUD_set_active_out(s->voice, 1);
    } else {
        fprintf(stderr, "[lobster-audio] no host audio backend available\n");
    }
}

static uint64_t lobster_audio_read(void *opaque, hwaddr offset, unsigned size)
{
    LobsterAudioState *s = LOBSTER_AUDIO(opaque);

    switch (offset) {
    case REG_MAGIC:  return LOBSTER_AUDIO_MAGIC;
    case REG_VER:    return 0x00010000;
    case REG_RATE:   return s->rate;
    case REG_VOL:    return s->volume;
    case REG_STATUS: return s->status;
    default:
        return 0;
    }
}

static void lobster_audio_write(void *opaque, hwaddr offset,
                                uint64_t val, unsigned size)
{
    LobsterAudioState *s = LOBSTER_AUDIO(opaque);

    switch (offset) {
    case REG_RATE:
        if (val >= 4000 && val <= 192000) {
            s->rate = (uint32_t)val;
            if (s->voice) {
                AUD_close_out(&s->card, s->voice);
                s->voice = NULL;
                s->ring_head = s->ring_tail = 0;
            }
        }
        break;
    case REG_VOL:
        if (val <= 100) {
            s->volume = (uint32_t)val;
            la_update_volume(s);
        }
        break;
    case REG_LEN: {
        uint32_t len = (uint32_t)val;
        if (len > LA_WAVE_SIZE) {
            len = LA_WAVE_SIZE;
        }
        la_ensure_voice(s);
        uint32_t samples = len / 2;
        for (uint32_t i = 0; i < samples; i++) {
            int16_t sample;
            memcpy(&sample, &s->wave[2 * i], sizeof(sample));
            /* Drop on overflow — guest should pace with STATUS. */
            if (la_ring_count(s) < LA_WAVE_SIZE / 2) {
                s->ring[s->ring_tail % (LA_WAVE_SIZE / 2)] = sample;
                s->ring_tail++;
            }
        }
        s->status = 2;
        break;
    }
    default:
        if (offset >= REG_WAVE && offset < REG_WAVE + LA_WAVE_SIZE) {
            s->wave[offset - REG_WAVE] = (uint8_t)val;
        }
        break;
    }
}

static const MemoryRegionOps lobster_audio_mmio_ops = {
    .read = lobster_audio_read,
    .write = lobster_audio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void lobster_audio_realize(DeviceState *dev, Error **errp)
{
    LobsterAudioState *s = LOBSTER_AUDIO(dev);

    if (!AUD_register_card("lobster-audio", &s->card, errp)) {
        /* Card registration failed; device stays present but silent. */
        return;
    }
}

static void lobster_audio_init(Object *obj)
{
    LobsterAudioState *s = LOBSTER_AUDIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &lobster_audio_mmio_ops, s,
                          "lobster-audio", LA_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    s->rate = 8000;
    s->volume = 100;
    s->status = 0;
    s->voice = NULL;
    s->ring_head = 0;
    s->ring_tail = 0;
}

static void lobster_audio_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = lobster_audio_realize;
    dc->desc = "LobsterOS audio (guest PCM over MMIO to host speakers)";
}

static const TypeInfo lobster_audio_info = {
    .name          = TYPE_LOBSTER_AUDIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LobsterAudioState),
    .instance_init = lobster_audio_init,
    .class_init    = lobster_audio_class_init,
};

static void lobster_audio_register_types(void)
{
    type_register_static(&lobster_audio_info);
}

type_init(lobster_audio_register_types)
