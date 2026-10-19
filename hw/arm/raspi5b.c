/*
 * Raspberry Pi 5B emulation
 *
 * Copyright (C) 2025 Mister Lobster
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/raspi_platform.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "system/device_tree.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "hw/arm/bcm2712.h"
#include "hw/char/pl011.h"
#include "chardev/char-fe.h"
#include "system/system.h"
#include <libfdt.h>
#include <string.h>

#define TYPE_RASPI5B_MACHINE MACHINE_TYPE_NAME("raspi5b")
OBJECT_DECLARE_SIMPLE_TYPE(Raspi5bMachineState, RASPI5B_MACHINE)

struct Raspi5bMachineState {
    RaspiBaseMachineState parent_obj;
    BCM2712State soc;
};

/* Memory map for Pi 5:
 * 0x00000000 - 0x3fffffff: Low memory (1GB)
 * 0x40000000 - 0xffffffff: High memory (3GB for 4GB model)
 * 0xf0000000 - 0xffffffff: Peripherals
 */

/* Add second memory node if board RAM exceeds 1GB */
static int raspi5_add_memory_node(void *fdt, hwaddr mem_base, hwaddr mem_len)
{
    int ret;
    uint32_t acells, scells;
    char *nodename = g_strdup_printf("/memory@%" PRIx64, mem_base);

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);
    if (acells == 0 || scells == 0) {
        fprintf(stderr, "dtb file invalid (#address-cells or #size-cells 0)\n");
        ret = -1;
    } else {
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
        ret = qemu_fdt_setprop_sized_cells(fdt, nodename, "reg",
                                           acells, mem_base,
                                           scells, mem_len);
    }

    g_free(nodename);
    return ret;
}

static void raspi5_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    uint64_t ram_size;

    /* Explicitly disable RP1 UARTs (they don't inherit disabled status properly)
     * Do this FIRST before disabling their parent nodes */
    {
        int uart_offset = fdt_node_offset_by_compatible(fdt, -1, "arm,pl011-axi");
        while (uart_offset >= 0) {
            const char *name = fdt_get_name(fdt, uart_offset, NULL);
            warn_report("bcm2712 dtc: found PL011 node: %s", name ? name : "NULL");
            if (name && (strstr(name, "serial@30000") || strstr(name, "serial@34000") ||
                         strstr(name, "serial@38000") || strstr(name, "serial@3c000") ||
                         strstr(name, "serial@40000") || strstr(name, "serial@44000") ||
                         strstr(name, "serial@48000") || strstr(name, "serial@4c000"))) {
                warn_report("bcm2712 dtc: disabling RP1 UART %s", name);
                fdt_setprop_string(fdt, uart_offset, "status", "disabled");
            }
            uart_offset = fdt_node_offset_by_compatible(fdt, uart_offset, "arm,pl011-axi");
        }
    }

    /* Fix BCM2835 UART0 reg size from 0x200 to 0x1000 (4KB for PL011 register space) */
    {
        int uart0_offset = fdt_node_offset_by_compatible(fdt, -1, "arm,pl011");
        while (uart0_offset >= 0) {
            const char *name = fdt_get_name(fdt, uart0_offset, NULL);
            warn_report("bcm2712 dtc: found arm,pl011 node: %s", name ? name : "NULL");
            if (name && strstr(name, "serial@7d001000")) {
                warn_report("bcm2712 dtc: fixing BCM2835 UART0 reg size");
                /* /soc has #address-cells=1, #size-cells=1, so reg format: <addr size> */
                uint32_t reg[2];
                reg[0] = cpu_to_be32(0x7d001000);
                reg[1] = cpu_to_be32(0x1000);
                fdt_setprop(fdt, uart0_offset, "reg", reg, sizeof(reg));
                break;
            }
            uart0_offset = fdt_node_offset_by_compatible(fdt, uart0_offset, "arm,pl011");
        }
    }

    /* Disable devices not yet implemented */
    const char *nodes_to_remove[] = {
        "brcm,bcm2712-pcie",
        "brcm,bcm2712-rng200",
        "brcm,bcm2712-thermal",
        "raspberrypi,bcm2835-firmware",
        "raspberrypi,bcm2835-power",
        "brcm,bcm2712-pm",
        "brcm,bcm2712-dma",
        "brcm,bcm2835-dma",
        "brcm,bcm2712-hdmi0",
        "brcm,bcm2712-hdmi1",
        "brcm,bcm2712-dsi0",
        "brcm,bcm2712-dsi1",
        "brcm,bcm2712-vec",
        "brcm,bcm2712-dvp",
        "brcm,bcm2712-cam0",
        "brcm,bcm2712-cam1",
        "brcm,bcm2712-pisp",
        "brcm,bcm2712-hevc",
        "brcm,bcm2712-l2-intc",
        "brcm,bcm2712-l2-intc-v3d",
        "brcm,bcm2712-l2-intc-rp1",
        "brcm,bcm2712-l2-intc-ao",
        "brcm,bcm2712-l2-intc-hdmi0",
        "brcm,bcm2712-aon-irq",
        "brcm,bcm2712-mip-intc-irq",
        "brcm,bcm7271-uart",
        "brcm,bcm2712-i2c",
        "brcm,bcm2712-spi",
        "brcm,bcm2712-avs",
        "brcm,bcm2712-vc4",
        "brcm,bcm2712-v3d",
        "brcm,l2-intc",
        "brcm,bcm7271-l2-intc",
        "brcm,bcm2711-l2-intc",
        "brcm,bcm2712-iommu",
        "brcm,bcm2712-iommuc",
        "brcm,bcm2712-genet",
        "brcm,bcm2712-xhci",
        "brcm,bcm2712-hevc",
        "brcm,bcm2712-isp",
        "brcm,bcm2712-csi",
        "brcm,bcm2712-rng200",
        "brcm,bcm2712-thermal",
        "brcm,bcm2712-dma",
        "brcm,bcm2712-v3d",
        "raspberrypi,rpivid-vid-decoder",
        "raspberrypi,pispbe",
        "raspberrypi,rp1-cfe",
        "raspberrypi,rp1dsi",
        "raspberrypi,rp1vec",
        "raspberrypi,rp1dpi",
    };

    for (int i = 0; i < ARRAY_SIZE(nodes_to_remove); i++) {
        const char *dev_str = nodes_to_remove[i];

        int offset = fdt_node_offset_by_compatible(fdt, -1, dev_str);
        while (offset >= 0) {
            if (!fdt_nop_node(fdt, offset)) {
                warn_report("bcm2712 dtc: %s has been disabled!", dev_str);
            }
            offset = fdt_node_offset_by_compatible(fdt, offset, dev_str);
        }
    }

    /* Explicitly disable L2 interrupt controllers by setting status=disabled
     * instead of nop, as nop nodes may still be visible to kernel DT parser. */
    const char *l2_intc_compat[] = {
        "brcm,l2-intc",
        "brcm,bcm7271-l2-intc",
        "brcm,bcm2711-l2-intc",
    };
    for (int i = 0; i < ARRAY_SIZE(l2_intc_compat); i++) {
        int offset = fdt_node_offset_by_compatible(fdt, -1, l2_intc_compat[i]);
        while (offset >= 0) {
            fdt_setprop_string(fdt, offset, "status", "disabled");
            offset = fdt_node_offset_by_compatible(fdt, offset, l2_intc_compat[i]);
        }
    }

    /* Explicitly disable BCM2712 GPIO controllers that depend on disabled L2 intcs */
    const char *gpio_compat[] = {
        "brcm,brcmstb-gpio",
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_compat); i++) {
        int offset = fdt_node_offset_by_compatible(fdt, -1, gpio_compat[i]);
        while (offset >= 0) {
            fdt_setprop_string(fdt, offset, "status", "disabled");
            offset = fdt_node_offset_by_compatible(fdt, offset, gpio_compat[i]);
        }
    }

    ram_size = board_ram_size(info->board_id);

    if (info->ram_size > 0x40000000) {
        raspi5_add_memory_node(fdt, 0x40000000, ram_size - 0x40000000);
    }
}

static void raspi5b_machine_init(MachineState *machine)
{
    Raspi5bMachineState *s = RASPI5B_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM2712State *soc = &s->soc;

    s_base->binfo.modify_dtb = raspi5_modify_dtb;
    s_base->binfo.board_id = mc->board_rev;
    s_base->binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    object_initialize_child(OBJECT(machine), "soc", soc,
                            board_soc_type(mc->board_rev));

    /* UART chardev connection is handled by raspi_base_machine_init via
     * sysbus_create_simple / pl011 - do NOT manually set chardev here
     * as it causes "chardev conflicts" errors. */

    /* Keep all CPUs powered off until boot process starts them */
    for (int cpu_idx = 0; cpu_idx < 4; cpu_idx++) {
        object_property_set_bool(OBJECT(&((BCM283XBaseState *)soc)->cpu[cpu_idx].core), "start-powered-off", true, &error_abort);
    }

    raspi_base_machine_init(machine, (BCM283XBaseState *)soc);

    /* Create and map the LobsterOS input device at 0x7d008000
     * This MMIO device captures mouse/keyboard events from QEMU's display
     * layer and exposes them to the guest via a ring buffer. */
    {
        DeviceState *input_dev = sysbus_create_simple("lobster-input", 0, NULL);
        if (input_dev) {
            sysbus_mmio_map(SYS_BUS_DEVICE(input_dev), 0, 0x7d008000ULL);
        }
    }

    /* Create and map the LobsterOS network device at 0x7d009000
     * This MMIO device provides HTTP fetch capability via host curl. */
    {
        DeviceState *net_dev = sysbus_create_simple("lobster-net", 0, NULL);
        if (net_dev) {
            sysbus_mmio_map(SYS_BUS_DEVICE(net_dev), 0, 0x7d009000ULL);
        }
    }

    /* Create and map the LobsterOS bulk-serial device at 0x7d100000
     * This MMIO device lets the guest emit console output with one MMIO
     * write per chunk instead of two MMIO ops per byte via the PL011.
     * Output is forwarded into the PL011's serial chardev. */
    {
        DeviceState *ser_dev = sysbus_create_simple("lobster-serial", 0, NULL);
        if (ser_dev) {
            /*
             * Resolve the SoC's PL011 UART (soc/peripherals/uart0) and link it
             * so the bulk window forwards output into the same serial chardev.
             */
            Object *uart = object_resolve_path_component(
                    object_resolve_path_component(
                        object_resolve_path_component(OBJECT(machine), "soc"),
                        "peripherals"),
                    "uart0");
            if (uart && object_dynamic_cast(uart, TYPE_PL011)) {
                object_property_set_link(OBJECT(ser_dev), "pl011", uart,
                                         &error_abort);
            } else {
                fprintf(stderr, "[lobster-serial] WARNING: PL011 not found; "
                                "bulk serial output disabled\n");
            }
            sysbus_mmio_map(SYS_BUS_DEVICE(ser_dev), 0, 0x7d100000ULL);
        }
    }

    /* Create and map the LobsterOS virtual disk at 0x7d200000
     * Host-backed block device (backing image via LBST_DISKIMG env var):
     * one MMIO command per 512-byte block, read or write. */
    {
        DeviceState *disk_dev = sysbus_create_simple("lobster-disk", 0, NULL);
        if (disk_dev) {
            sysbus_mmio_map(SYS_BUS_DEVICE(disk_dev), 0, 0x7d200000ULL);
        }
    }

    /* Create and map the LobsterOS shared folder at 0x7d300000
     * Host-directory export (LBST_SHARE env var): stat/list/read/write
     * over MMIO, jailed to the share root. */
    {
        DeviceState *share_dev = sysbus_create_simple("lobster-share", 0, NULL);
        if (share_dev) {
            sysbus_mmio_map(SYS_BUS_DEVICE(share_dev), 0, 0x7d300000ULL);
        }
    }

    /* Create and map the LobsterOS audio device at 0x7d400000
     * Guest pushes 16-bit mono PCM chunks; host plays them through
     * QEMU's audio backend (PipeWire/ALSA/Pulse). */
    {
        DeviceState *audio_dev = sysbus_create_simple("lobster-audio", 0, NULL);
        if (audio_dev) {
            sysbus_mmio_map(SYS_BUS_DEVICE(audio_dev), 0, 0x7d400000ULL);
        }
    }

    /* For bare-metal kernels (no ARM64 Linux magic header), write a proper
     * bootloader using the Linux bootloader_aarch64 which sets up registers
     * and jumps to the kernel. This handles fixups correctly. */
    if (machine->kernel_filename) {
        gsize len;
        gchar *buf;
        bool has_linux_magic = false;
        if (g_file_get_contents(machine->kernel_filename, &buf, &len, NULL)) {
            if (len > 60 && memcmp(buf + 56, "ARM\x64", 4) == 0) {
                has_linux_magic = true;
            }
            g_free(buf);
        }

        if (!has_linux_magic) {
            /* Bare-metal kernel: write Linux-compatible bootloader at address 0.
             * This uses the same bootloader_aarch64 as Linux, which properly
             * sets up x0=DTB, x1-3=0, x4=kernel_entry, and branches to x4. */
            hwaddr dtb_addr = s_base->binfo.dtb_start;
            hwaddr real_entry = s_base->binfo.entry;
            if (real_entry == 0) {
                real_entry = 0x80000; /* KERNEL64_LOAD_ADDR for raw AArch64 images */
            }

            /* Override entry to 0 so CPU starts at our bootloader at address 0 */
            s_base->binfo.entry = 0;

            /* Set up fixup context for bootloader_aarch64 */
            uint32_t fixupcontext[FIXUP_MAX];
            fixupcontext[FIXUP_ARGPTR_LO] = (uint32_t)dtb_addr;
            fixupcontext[FIXUP_ARGPTR_HI] = (uint32_t)(dtb_addr >> 32);
            fixupcontext[FIXUP_ENTRYPOINT_LO] = (uint32_t)real_entry;
            fixupcontext[FIXUP_ENTRYPOINT_HI] = (uint32_t)(real_entry >> 32);

            /* Use the same bootloader as Linux (bootloader_aarch64 from hw/arm/boot.c) */
            static const ARMInsnFixup bootloader_aarch64[] = {
                { 0x580000c0 }, /* ldr x0, arg ; Load the lower 32-bits of DTB */
                { 0xaa1f03e1 }, /* mov x1, xzr */
                { 0xaa1f03e2 }, /* mov x2, xzr */
                { 0xaa1f03e3 }, /* mov x3, xzr */
                { 0x58000084 }, /* ldr x4, entry ; Load the lower 32-bits of kernel entry */
                { 0xd61f0080 }, /* br x4      ; Jump to the kernel entry point */
                { 0, FIXUP_ARGPTR_LO }, /* arg: .word @DTB Lower 32-bits */
                { 0, FIXUP_ARGPTR_HI }, /* .word @DTB Higher 32-bits */
                { 0, FIXUP_ENTRYPOINT_LO }, /* entry: .word @Kernel Entry Lower 32-bits */
                { 0, FIXUP_ENTRYPOINT_HI }, /* .word @Kernel Entry Higher 32-bits */
                { 0, FIXUP_TERMINATOR }
            };

            arm_write_bootloader("baremetal-bootloader", &address_space_memory, 0,
                                 bootloader_aarch64, fixupcontext);
        }
    }
}

static void raspi5b_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    /* Default to 4GB model */
    rmc->board_rev = RPI5_BOARD_REV_4G;
    raspi_machine_class_common_init(mc, rmc->board_rev);
    mc->desc = "Raspberry Pi 5B (revision 1.0)";
    mc->auto_create_sdcard = true;
    mc->init = raspi5b_machine_init;
}

static const TypeInfo raspi5b_machine_type = {
    .name           = TYPE_RASPI5B_MACHINE,
    .parent         = TYPE_RASPI_BASE_MACHINE,
    .instance_size  = sizeof(Raspi5bMachineState),
    .class_init     = raspi5b_machine_class_init,
};

static void raspi5b_machine_register_type(void)
{
    type_register_static(&raspi5b_machine_type);
}

type_init(raspi5b_machine_register_type)