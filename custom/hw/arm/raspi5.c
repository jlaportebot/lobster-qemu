/*
 * Raspberry Pi 5 machine emulation
 *
 * Copyright (C) 2025 LobsterOS Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/bcm2712.h"
#include "hw/arm/raspi_platform.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/loader.h"
// #include "hw/arm/machines-qom.h"  // MISSING
#include "qom/object.h"

#define TYPE_RASPI5_MACHINE  MACHINE_TYPE_NAME("raspi5-common")
OBJECT_DECLARE_SIMPLE_TYPE(Raspi5MachineState, RASPI5_MACHINE)

#define SMPBOOT_ADDR    0x300
#define MVBAR_ADDR      0x400
#define BOARDSETUP_ADDR (MVBAR_ADDR + 0x20)
#define FIRMWARE_ADDR_3 0x80000
#define SPINTABLE_ADDR  0xd8

struct Raspi5MachineState {
    /*< private >*/
    RaspiBaseMachineState parent_obj;
    /*< public >*/
    BCM2712State soc;
};

/* Board revision codes for Raspberry Pi 5 */
typedef enum Raspi5ProcessorId {
    PROCESSOR_ID_BCM2712 = 4,
} Raspi5ProcessorId;

static const struct {
    const char *type;
    int cores_count;
} soc5_property[] = {
    [PROCESSOR_ID_BCM2712] = {TYPE_BCM2712, BCM283X_NCPUS},
};

uint64_t board5_ram_size(uint32_t board_rev)
{
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    return 256 * MiB << FIELD_EX32(board_rev, REV_CODE, MEMORY_SIZE);
}

static Raspi5ProcessorId board5_processor_id(uint32_t board_rev)
{
    int proc_id = FIELD_EX32(board_rev, REV_CODE, PROCESSOR);
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    assert(proc_id < ARRAY_SIZE(soc5_property) && soc5_property[proc_id].type);
    return proc_id;
}

const char *board5_soc_type(uint32_t board_rev)
{
    return soc5_property[board5_processor_id(board_rev)].type;
}

static int cores5_count(uint32_t board_rev)
{
    return soc5_property[board5_processor_id(board_rev)].cores_count;
}

static const char *board5_type(uint32_t board_rev)
{
    static const char *types[] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, "5B",
    };
    assert(FIELD_EX32(board_rev, REV_CODE, STYLE));
    int bt = FIELD_EX32(board_rev, REV_CODE, TYPE);
    if (bt >= ARRAY_SIZE(types) || !types[bt]) {
        return "Unknown";
    }
    return types[bt];
}

static void write_smpboot64(ARMCPU *cpu, const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
    static const ARMInsnFixup smpboot[] = {
        { 0xd2801b05 }, /*        mov     x5, 0xd8 */
        { 0xd53800a6 }, /*        mrs     x6, mpidr_el1 */
        { 0x924004c6 }, /*        and     x6, x6, #0x3 */
        { 0xd503205f }, /* spin:  wfe */
        { 0xf86678a4 }, /*        ldr     x4, [x5,x6,lsl #3] */
        { 0xb4ffffc4 }, /*        cbz     x4, spin */
        { 0xd2800000 }, /*        mov     x0, #0x0 */
        { 0xd2800001 }, /*        mov     x1, #0x0 */
        { 0xd2800002 }, /*        mov     x2, #0x0 */
        { 0xd2800003 }, /*        mov     x3, #0x0 */
        { 0xd61f0080 }, /*        br      x4 */
        { 0, FIXUP_TERMINATOR }
    };
    static const uint32_t fixupcontext[FIXUP_MAX] = { 0 };
    static const uint64_t spintables[] = { 0, 0, 0, 0 };

    arm_write_bootloader("raspi5_smpboot", as, info->smp_loader_start,
                         smpboot, fixupcontext);
    rom_add_blob_fixed_as("raspi5_spintables", spintables, sizeof(spintables),
                          SPINTABLE_ADDR, as);
}

static void reset_secondary(ARMCPU *cpu, const struct arm_boot_info *info)
{
    CPUState *cs = CPU(cpu);
    cpu_set_pc(cs, info->smp_loader_start);
}

static void setup_boot5(MachineState *machine, ARMCPU *cpu,
                        Raspi5ProcessorId processor_id, size_t ram_size)
{
    RaspiBaseMachineState *s = RASPI_BASE_MACHINE(machine);
    int r;

    s->binfo.ram_size = ram_size;

    /* BCM2712 doesn't need secure board setup */
    s->binfo.secure_boot = false;
    s->binfo.secure_board_setup = false;

    /* BCM2712 requires SMP setup */
    s->binfo.smp_loader_start = SMPBOOT_ADDR;
    s->binfo.write_secondary_boot = write_smpboot64;
    s->binfo.secondary_cpu_reset_hook = reset_secondary;

    /* If the user specified a "firmware" image (e.g. UEFI), we bypass
     * the normal Linux boot process
     */
    if (machine->firmware) {
        hwaddr firmware_addr = FIRMWARE_ADDR_3;
        r = load_image_targphys(machine->firmware, firmware_addr,
                                ram_size - firmware_addr, NULL);
        if (r < 0) {
            error_report("Failed to load firmware from %s", machine->firmware);
            exit(1);
        }
        s->binfo.entry = firmware_addr;
        s->binfo.firmware_loaded = true;
    }

    arm_load_kernel(cpu, machine, &s->binfo);
}

void raspi5_machine_init(MachineState *machine)
{
    Raspi5MachineState *s = RASPI5_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM2712State *soc = &s->soc;

    s_base->binfo.board_id = MACH_TYPE_BCM2712;

    object_initialize_child(OBJECT(machine), "soc", soc,
                            board5_soc_type(mc->board_rev));
    raspi_base_machine_init(machine, &soc->parent_obj);
}

void raspi5_machine_class_common_init(MachineClass *mc, uint32_t board_rev)
{
    mc->desc = g_strdup_printf("Raspberry Pi %s (revision 1.%u)",
                               board5_type(board_rev),
                               FIELD_EX32(board_rev, REV_CODE, REVISION));
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = cores5_count(board_rev);
    mc->default_ram_size = board5_ram_size(board_rev);
    mc->default_ram_id = "ram";
}

static void raspi5_machine_class_init(MachineClass *mc, uint32_t board_rev)
{
    raspi5_machine_class_common_init(mc, board_rev);
    mc->init = raspi5_machine_init;
}

static void raspi5b_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0xc03115; /* Pi 5 Model B Rev 1.5 */
    raspi5_machine_class_init(mc, rmc->board_rev);
}

static void raspi5b_8gb_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0xd03115; /* Pi 5 Model B Rev 1.5 8GB */
    raspi5_machine_class_init(mc, rmc->board_rev);
}

static void raspi5b_16gb_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    mc->auto_create_sdcard = true;
    rmc->board_rev = 0xe03115; /* Pi 5 Model B Rev 1.5 16GB */
    raspi5_machine_class_init(mc, rmc->board_rev);
}

static const TypeInfo raspi5_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("raspi5b"),
        .parent         = TYPE_RASPI5_MACHINE,
        .class_init     = raspi5b_machine_class_init,
        .interfaces     = aarch64_machine_interfaces,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi5b-8gb"),
        .parent         = TYPE_RASPI5_MACHINE,
        .class_init     = raspi5b_8gb_machine_class_init,
        .interfaces     = aarch64_machine_interfaces,
    }, {
        .name           = MACHINE_TYPE_NAME("raspi5b-16gb"),
        .parent         = TYPE_RASPI5_MACHINE,
        .class_init     = raspi5b_16gb_machine_class_init,
        .interfaces     = aarch64_machine_interfaces,
    }, {
        .name           = TYPE_RASPI5_MACHINE,
        .parent         = TYPE_RASPI_BASE_MACHINE,
        .instance_size  = sizeof(Raspi5MachineState),
        .abstract       = true,
    }
};

DEFINE_TYPES(raspi5_machine_types)