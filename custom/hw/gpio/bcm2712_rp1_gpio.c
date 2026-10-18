/*
 * RP1 GPIO Controller (Raspberry Pi 5)
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/gpio/bcm2712_rp1_gpio.h"

/* RP1 GPIO register offsets (per bank) */
#define RP1_GPIO_FSEL       0x000   /* Function select (54 bits = 2 regs) */
#define RP1_GPIO_OUT        0x080   /* Output value */
#define RP1_GPIO_OE         0x100   /* Output enable */
#define RP1_GPIO_IN         0x180   /* Input value */
#define RP1_GPIO_IN_SYNC    0x200   /* Synchronized input */
#define RP1_GPIO_OUT_SET    0x280   /* Output set */
#define RP1_GPIO_OUT_CLR    0x300   /* Output clear */
#define RP1_GPIO_IRQ_EN     0x380   /* Interrupt enable */
#define RP1_GPIO_IRQ_STAT   0x400   /* Interrupt status */
#define RP1_GPIO_IRQ_TYPE   0x480   /* Interrupt type (0=level, 1=edge) */
#define RP1_GPIO_IRQ_POL    0x500   /* Interrupt polarity */
#define RP1_GPIO_IRQ_EDGE   0x580   /* Interrupt edge (0=both, 1=rising, 2=falling) */

/* Bank stride */
#define RP1_GPIO_BANK_STRIDE 0x600  /* 1536 bytes per bank */

/* Total GPIOs: 54 (3 banks * 18) */
#define RP1_GPIO_NR         54
#define RP1_GPIO_BANKS      3
#define RP1_GPIO_PER_BANK   18

static uint64_t rp1_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2712RP1GPIOState *s = (BCM2712RP1GPIOState *)opaque;
    uint32_t bank = offset / RP1_GPIO_BANK_STRIDE;
    uint32_t reg = offset % RP1_GPIO_BANK_STRIDE;
    uint64_t val = 0;

    if (bank >= RP1_GPIO_BANKS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad bank %u\\n", __func__, bank);
        return 0;
    }

    switch (reg) {
    case RP1_GPIO_FSEL:
    case RP1_GPIO_FSEL + 4:
        /* Function select - 2 registers per bank (54 bits) */
        val = s->fsel[bank][(reg - RP1_GPIO_FSEL) / 4];
        break;
    case RP1_GPIO_OUT:
        val = s->out[bank];
        break;
    case RP1_GPIO_OE:
        val = s->oe[bank];
        break;
    case RP1_GPIO_IN:
        /* Return synchronized input */
        val = s->in_sync[bank];
        break;
    case RP1_GPIO_IN_SYNC:
        val = s->in_sync[bank];
        break;
    case RP1_GPIO_OUT_SET:
    case RP1_GPIO_OUT_CLR:
        /* Write only */
        val = 0;
        break;
    case RP1_GPIO_IRQ_EN:
        val = s->irq_en[bank];
        break;
    case RP1_GPIO_IRQ_STAT:
        val = s->irq_stat[bank];
        break;
    case RP1_GPIO_IRQ_TYPE:
        val = s->irq_type[bank];
        break;
    case RP1_GPIO_IRQ_POL:
        val = s->irq_pol[bank];
        break;
    case RP1_GPIO_IRQ_EDGE:
        val = s->irq_edge[bank];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\\n",
                      __func__, offset);
        break;
    }

    return val;
}

static void rp1_gpio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    BCM2712RP1GPIOState *s = (BCM2712RP1GPIOState *)opaque;
    uint32_t bank = offset / RP1_GPIO_BANK_STRIDE;
    uint32_t reg = offset % RP1_GPIO_BANK_STRIDE;

    if (bank >= RP1_GPIO_BANKS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad bank %u\\n", __func__, bank);
        return;
    }

    switch (reg) {
    case RP1_GPIO_FSEL:
    case RP1_GPIO_FSEL + 4:
        s->fsel[bank][(reg - RP1_GPIO_FSEL) / 4] = value;
        break;
    case RP1_GPIO_OUT:
        s->out[bank] = value & 0x3FFFF; /* 18 bits */
        /* Update output GPIOs */
        for (int i = 0; i < RP1_GPIO_PER_BANK; i++) {
            if (value & (1 << i)) {
                qemu_set_irq(s->gpio_out[bank * RP1_GPIO_PER_BANK + i], 1);
            } else {
                qemu_set_irq(s->gpio_out[bank * RP1_GPIO_PER_BANK + i], 0);
            }
        }
        break;
    case RP1_GPIO_OE:
        s->oe[bank] = value & 0x3FFFF;
        break;
    case RP1_GPIO_IN:
        /* Read only */
        break;
    case RP1_GPIO_IN_SYNC:
        /* Read only */
        break;
    case RP1_GPIO_OUT_SET:
        s->out[bank] |= (value & 0x3FFFF);
        /* Update output GPIOs */
        for (int i = 0; i < RP1_GPIO_PER_BANK; i++) {
            if (value & (1 << i)) {
                qemu_set_irq(s->gpio_out[bank * RP1_GPIO_PER_BANK + i], 1);
            }
        }
        break;
    case RP1_GPIO_OUT_CLR:
        s->out[bank] &= ~(value & 0x3FFFF);
        /* Update output GPIOs */
        for (int i = 0; i < RP1_GPIO_PER_BANK; i++) {
            if (value & (1 << i)) {
                qemu_set_irq(s->gpio_out[bank * RP1_GPIO_PER_BANK + i], 0);
            }
        }
        break;
    case RP1_GPIO_IRQ_EN:
        s->irq_en[bank] = value & 0x3FFFF;
        break;
    case RP1_GPIO_IRQ_STAT:
        /* Write 1 to clear */
        s->irq_stat[bank] &= ~value;
        break;
    case RP1_GPIO_IRQ_TYPE:
        s->irq_type[bank] = value & 0x3FFFF;
        break;
    case RP1_GPIO_IRQ_POL:
        s->irq_pol[bank] = value & 0x3FFFF;
        break;
    case RP1_GPIO_IRQ_EDGE:
        s->irq_edge[bank] = value & 0x3FFFF;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps rp1_gpio_ops = {
    .read = rp1_gpio_read,
    .write = rp1_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp1_gpio_reset(DeviceState *dev)
{
    BCM2712RP1GPIOState *s = BCM2712_RP1_GPIO(dev);

    for (int b = 0; b < RP1_GPIO_BANKS; b++) {
        s->fsel[b][0] = 0;
        s->fsel[b][1] = 0;
        s->out[b] = 0;
        s->oe[b] = 0;
        s->in_sync[b] = 0;
        s->irq_en[b] = 0;
        s->irq_stat[b] = 0;
        s->irq_type[b] = 0;
        s->irq_pol[b] = 0;
        s->irq_edge[b] = 0;
        for (int i = 0; i < RP1_GPIO_PER_BANK; i++) {
            qemu_set_irq(s->gpio_out[b * RP1_GPIO_PER_BANK + i], 0);
        }
    }
}

static void rp1_gpio_init(Object *obj)
{
    BCM2712RP1GPIOState *s = BCM2712_RP1_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &rp1_gpio_ops, s,
                          "rp1-gpio", RP1_GPIO_BANKS * RP1_GPIO_BANK_STRIDE);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out(DEVICE(obj), s->gpio_out, RP1_GPIO_NR);
}

static void rp1_gpio_realize(DeviceState *dev, Error **errp)
{
    /* Nothing special for now */
}

static const VMStateDescription vmstate_rp1_gpio = {
    .name = "rp1_gpio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(fsel[0], BCM2712RP1GPIOState, 2),
        VMSTATE_UINT32_ARRAY(fsel[1], BCM2712RP1GPIOState, 2),
        VMSTATE_UINT32_ARRAY(fsel[2], BCM2712RP1GPIOState, 2),
        VMSTATE_UINT32_ARRAY(out, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(oe, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(in_sync, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(irq_en, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(irq_stat, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(irq_type, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(irq_pol, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_UINT32_ARRAY(irq_edge, BCM2712RP1GPIOState, RP1_GPIO_BANKS),
        VMSTATE_END_OF_LIST()
    }
};

static void rp1_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rp1_gpio;
    dc->realize = rp1_gpio_realize;
    device_class_set_legacy_reset(dc, rp1_gpio_reset);
}

static const TypeInfo rp1_gpio_info = {
    .name          = TYPE_BCM2712_RP1_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2712RP1GPIOState),
    .instance_init = rp1_gpio_init,
    .class_init    = rp1_gpio_class_init,
};

static void rp1_gpio_register_types(void)
{
    type_register_static(&rp1_gpio_info);
}

type_init(rp1_gpio_register_types)