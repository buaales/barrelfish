/**
 * \file
 * \brief ARMv8 arch specfic code
 */

/*
 * Copyright (c) 2013, 2016 ETH Zurich.
 * Copyright (c) 2015-2016, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <hw_records_arch.h>
#include <barrelfish/barrelfish.h>
#include <skb/skb.h>
#include <barrelfish_kpi/platform.h>
#include <if/monitor_blocking_defs.h>
#include "kaluga.h"


static errval_t armv8_startup_common(void)
{
    errval_t err = SYS_ERR_OK;

    // We need to run on core 0
    // (we are responsible for booting all the other cores)
    assert(my_core_id == BSP_CORE_ID);

    err = skb_client_connect();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Connect to SKB.");
    }

    // Make sure the driver db is loaded
    err = skb_execute("[device_db].");
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Device DB not loaded.");
    }

    struct monitor_blocking_binding *m = get_monitor_blocking_binding();
    assert(m != NULL);

    uint32_t arch, platform;
    err = m->rpc_tx_vtbl.get_platform(m, &arch, &platform);
    assert(err_is_ok(err));
    assert(arch == PI_ARCH_ARMV8A);

    if (platform != PI_PLATFORM_ZYNQMP) {
        // The current boot protocol needs us to have
        // knowledge about how many CPUs are available at boot
        // time in order to start-up properly.
        char* record = NULL;
        err = oct_barrier_enter("barrier.acpi", &record, 2);
    }
    else {
        uint8_t buffer[PI_ARCH_INFO_SIZE];
        struct arch_info_armv8 *arch_info = (struct arch_info_armv8*) buffer;
        size_t buflen;
        err = m->rpc_tx_vtbl.get_platform_arch(m, buffer,  &buflen);
        assert(buflen == sizeof(struct arch_info_armv8));
        // Query the SKB for the available cores on ZynMP ZCU104 platform - we
        // can't do this by ACPI on this platform.
        err = skb_execute_query("arm_mpids(L) ,write(L).");
        if (err_is_fail(err)) {
            USER_PANIC_SKB_ERR(err, "Finding cores");
        }

        debug_printf("CPU driver reports %d core(s).\n", arch_info->ncores);
        int mpidr_raw;
        //struct list_parser_status skb_list;
        uint32_t bf_core_id= 0;

        //skb_read_list_init(&skb_list);

        //while(skb_read_list(&skb_list, "mpid(%d)", &mpidr_raw)) {
        for(mpidr_raw = 0; mpidr_raw < arch_info->ncores; mpidr_raw++){

            KALUGA_DEBUG("Setup skb fact for core%d.\n", bf_core_id);
            // setup boot_driver_entry.
            skb_add_fact("boot_driver_entry(%"PRIu64",%s).", (uint64_t)mpidr_raw, "armBootPSCI");
            // setup psci_use_hvc.
            //skb_add_fact("psci_use_hvc(%"PRIu8").", 1);
            skb_add_fact("psci_use_hvc(%"PRIu8").", 0);
            // set ARMv8 record
            // FIXME: fix params, see usr/acpi/arch/armv8/acpi_interrupts_arch.c
            oct_set(HW_PROCESSOR_ARMV8_RECORD_FORMAT, bf_core_id, 1, bf_core_id, (uint32_t)mpidr_raw,
                    CPU_ARM8, bf_core_id, bf_core_id, 1, 0, 1, 1, 1, 1, 1, 1, 1, (uint64_t)mpidr_raw);

            bf_core_id++;
        }
    }

    KALUGA_DEBUG("Kaluga: watch_for_cores\n");

    err = watch_for_cores();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Watching cores.");
    }

    if(platform != PI_PLATFORM_ZYNQMP) {

        KALUGA_DEBUG("Kaluga: pci_root_bridge\n");

        err = watch_for_pci_root_bridge();
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "Watching PCI root bridges.");
        }

        KALUGA_DEBUG("Kaluga: pci_devices\n");

        err = watch_for_pci_devices();
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "Watching PCI devices.");
        }

    }

    KALUGA_DEBUG("Kaluga: wait_for_all_spawnds\n");

    err = wait_for_all_spawnds();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Unable to wait for spawnds failed.");
    }
	
	if (platform == PI_PLATFORM_ZYNQMP) {
		err = init_device_caps_manager();
    	assert(err_is_ok(err));

    	struct module_info* mi = find_module("zynqmp_gem");
    	if (mi != NULL) {
        	KALUGA_DEBUG("module found.\n");
        	err = mi->start_function(0, mi, "zynqmp_gem {}", NULL);
        	KALUGA_DEBUG("start func done.\n");
        	assert(err_is_ok(err));
    	}
        
        mi = find_module("zynqmp_cni");
        if (mi != NULL) {
            KALUGA_DEBUG("module found.\n");
            err = mi->start_function(0, mi, "zynqmp_cni {}", NULL);
            KALUGA_DEBUG("start func done.\n");
            assert(err_is_ok(err));
        }
	}

    return SYS_ERR_OK;
}

static errval_t fvp_startup(void)
{
    errval_t err;

    err = skb_execute_query("[plat_fvp].");
    if(err_is_fail(err)){
        USER_PANIC_SKB_ERR(err, "Additional device db file 'plat_fvp' not loaded.");
    }

    return armv8_startup_common();
}

static errval_t qemu_startup(void)
{
    errval_t err;

    err = skb_execute_query("[plat_qemu].");
    if(err_is_fail(err)){
        USER_PANIC_SKB_ERR(err, "Additional device db file 'plat_qemu' not loaded.");
    }

    return armv8_startup_common();
}

static errval_t apm88xxxx_startup(void)
{
    errval_t err;

    err = skb_execute_query("[plat_apm88xxxx].");
    if(err_is_fail(err)){
        USER_PANIC_SKB_ERR(err, "Additional device db file 'plat_apm88xxxx' not loaded.");
    }

    return armv8_startup_common();
}

static errval_t zynqmp_startup(void)
{
    errval_t err;
    err = skb_execute_query("[plat_zynqmp].");
    if(err_is_fail(err)){
        USER_PANIC_SKB_ERR(err,"Additional device db file 'plat_zynqmp' not loaded.");
    }
    //err = oct_set("all_spawnds_up { iref: 0 }");
    //assert(err_is_ok(err));
    //debug_printf("armv8 startup ok\n");
    return armv8_startup_common();
}

static errval_t cn88xx_startup(void)
{
    errval_t err;

    err = skb_execute_query("[plat_cn88xx].");
    if(err_is_fail(err)){
        USER_PANIC_SKB_ERR(err, "Additional device db file 'plat_cn88xx' not loaded.");
    }

    return armv8_startup_common();
}

errval_t arch_startup(char * add_device_db_file)
{
    errval_t err;

    err = skb_client_connect();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Connect to SKB.");
    }

    // Make sure the driver db is loaded
    err = skb_execute("[device_db].");
    if (err_is_fail(err)) {
        USER_PANIC_SKB_ERR(err, "Device DB not loaded.");
    }

    if (add_device_db_file) {
        err = skb_execute_query("[%s].", add_device_db_file);
        if(err_is_fail(err)){
            USER_PANIC_SKB_ERR(err, "Additional device db file '%s' not loaded.",
                               add_device_db_file);
        }
    }

    struct monitor_blocking_binding *m = get_monitor_blocking_binding();
    assert(m != NULL);

    uint32_t arch, platform;
    err = m->rpc_tx_vtbl.get_platform(m, &arch, &platform);
    assert(err_is_ok(err));
    assert(arch == PI_ARCH_ARMV8A);

    switch(platform) {
    case PI_PLATFORM_FVP:
        debug_printf("Kaluga running on FVP\n");
        return fvp_startup();
    case PI_PLATFORM_QEMU:
        debug_printf("Kaluga running on QEMU\n");
        return qemu_startup();
    case PI_PLATFORM_APM88XXXX:
        debug_printf("Kaluga running on APM88xxxx\n");
        return apm88xxxx_startup();
    case PI_PLATFORM_CN88XX:
        debug_printf("Kaluga running on CN88xx\n");
        return cn88xx_startup();
    case PI_PLATFORM_ZYNQMP:
        debug_printf("Kluga running on ZynqMP ZCU104\n");
        return zynqmp_startup();
    }

    return KALUGA_ERR_UNKNOWN_PLATFORM;
}
