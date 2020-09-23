/**
 * \file
 * \brief Provides a generic startup function for the ARM OMAP platform
 */
/*
 * Copyright (c) 2013, ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <barrelfish/barrelfish.h>
#include <barrelfish/spawn_client.h>
#include <barrelfish_kpi/platform.h>
#include <if/monitor_blocking_defs.h>
#include <maps/zynqmp_map.h>

#include "kaluga.h"
#include <memory_maps.h>

#ifdef CLIENT
#undef CLIENT
#endif
#define CLIENT

struct allowed_registers
{
    char* binary;
    genpaddr_t registers[][2];
};

static struct allowed_registers zynqmp_gem = {
    .binary = "zynqmp_gem",
    .registers =
    {
        {ZYNQMP_GEM3_BASEADDR, 0x1000},
        {PRESET_DATA_BASE, PRESET_DATA_SIZE},
        {SHARED_REGION_VARIABLES_BASE, SHARED_REGION_VARIABLES_SIZE},
        {SHARED_REGION_ETH_TX_BASE, SHARED_REGION_ETH_SIZE},
        {SHARED_REGION_ETH_RX_BASE, SHARED_REGION_ETH_SIZE},
        {0x0, 0x0}
    }
};

static struct allowed_registers zynqmp_cni = {
    .binary = "zynqmp_cni",
    .registers = 
    {
        {ZYNQMP_CNI_BASEADDR, 0x1000},
        {SHARED_REGION_CNI_MSG_BASE, SHARED_REGION_CNI_MSG_SIZE},
        {0x0, 0x0}
    }
};

static struct allowed_registers* zynqmp[] = {
    &zynqmp_gem,
    &zynqmp_cni,
    NULL,
};

/**
 * \brief Startup function for ARMv8 drivers.
 *
 * Makes sure we get the device register capabilities.
 */
errval_t
default_start_function(coreid_t where, struct module_info* driver,
        char* record, struct driver_argument *placeholder)
{
    assert(driver != NULL);
    assert(record != NULL);

    errval_t err;

    struct monitor_blocking_binding *m=
        get_monitor_blocking_binding();
    assert(m != NULL);

    uint32_t arch, platform;
    err = m->rpc_tx_vtbl.get_platform(m, &arch, &platform);
    assert(err_is_ok(err));
    assert(arch == PI_ARCH_ARMV8A);

    struct allowed_registers **regs= NULL;
    switch(platform) {
        case PI_PLATFORM_ZYNQMP:
            regs= zynqmp;
            break;
        default:
            printf("Unrecognised ARMv8 platform\n");
            abort();
    }

    // TODO Request the right set of caps and put in device_range_cap
    struct cnoderef dev_cnode;
    struct capref dev_cnode_cap;
    err = cnode_create_l2(&dev_cnode_cap, &dev_cnode);
    assert(err_is_ok(err));

    struct capref device_cap;
    device_cap.cnode = dev_cnode;
    device_cap.slot = 0;

    char* name;
    err = oct_read(record, "%s", &name);
    assert(err_is_ok(err));
    KALUGA_DEBUG("%s:%d: Starting driver for %s\n", __FUNCTION__, __LINE__, name);
    for (size_t i=0; regs[i] != NULL; i++) {

        if(strcmp(name, regs[i]->binary) != 0) {
            continue;
        }

        // Get the device cap from the managed capability tree
        // put them all in a single cnode
        for (size_t j=0; regs[i]->registers[j][0] != 0x0; j++) {
            struct capref device_frame;
            KALUGA_DEBUG("%s:%d: mapping 0x%"PRIxLPADDR" %"PRIuLPADDR"\n", __FUNCTION__, __LINE__,
                   regs[i]->registers[j][0], regs[i]->registers[j][1]);

            lpaddr_t base = regs[i]->registers[j][0] & ~(BASE_PAGE_SIZE-1);
            err = get_device_cap(base,
                                 regs[i]->registers[j][1],
                                 &device_frame);
            assert(err_is_ok(err));

            err = cap_copy(device_cap, device_frame);
            assert(err_is_ok(err));
            device_cap.slot++;
        }
    }
    free(name);

    err = spawn_program_with_caps(where, driver->path, driver->argv, environ,
            NULL_CAP, dev_cnode_cap, 0, driver->did);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Spawning %s failed.", driver->path);
        return err;
    }

    return SYS_ERR_OK;
}


errval_t start_networking(coreid_t where,
                          struct module_info* driver,
                          char* record, struct driver_argument *placeholder)
{
    assert(driver != NULL);
    errval_t err = SYS_ERR_OK;


    if (is_started(driver)) {
        printf("Already started %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_ALREADY_STARTED;
    }

    if (!is_auto_driver(driver)) {
        printf("Not auto %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    driver->allow_multi = 1;
    err = default_start_function(where, driver, record, placeholder);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Spawning %s failed.", driver->path);
        return err;
    }

    // cards with driver in seperate process
    struct module_info* net_sockets = find_module("net_sockets_server");
    if (net_sockets == NULL) {
        printf("Net sockets server not found\n");
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    // Spawn net_sockets_server
    net_sockets->argv[0] = "net_sockets_server";
    net_sockets->argv[1] = "auto";
    net_sockets->argv[2] = driver->binary;
#ifdef CLIENT
    net_sockets->argv[3] = "--ip=192.168.0.1";
#else
    net_sockets->argv[3] = "--ip=192.168.0.2";
#endif


    err = spawn_program(where, net_sockets->path, net_sockets->argv, environ, 0,
                        get_did_ptr(net_sockets));
    return err;
}

static void provide_driver_with_caps(struct driver_instance* drv, char* name) {
    errval_t err;

    struct monitor_blocking_binding *m = get_monitor_blocking_binding();
    assert(m != NULL);

    uint32_t arch, platform;
    err = m->rpc_tx_vtbl.get_platform(m, &arch, &platform);
    assert(err_is_ok(err));
    assert(arch == PI_ARCH_ARMV8A);

    struct allowed_registers **regs= NULL;
    switch(platform) {
    case PI_PLATFORM_ZYNQMP:
        regs= zynqmp;
        break;
    default:
        printf("Unrecognised ARMv8 platform\n");
        abort();
    }

    KALUGA_DEBUG("%s:%d: Finding caps for driver for %s\n", __FUNCTION__, __LINE__, name);
    for (size_t i=0; regs[i] != NULL; i++) {
        if(strcmp(name, regs[i]->binary) != 0) {
            continue;
        }

        debug_printf("my dbg provide cap 0.\n");
        // Get the device cap from the managed capability tree
        // put them all in a single cnode
        for (size_t j=0; regs[i]->registers[j][0] != 0x0; j++) {
            debug_printf("my dbg provide cap 1.\n");
            struct capref device_frame;
            KALUGA_DEBUG("%s:%d: mapping 0x%"PRIxLPADDR" %"PRIuLPADDR"\n", __FUNCTION__, __LINE__,
            regs[i]->registers[j][0], regs[i]->registers[j][1]);
            lpaddr_t base = regs[i]->registers[j][0] & ~(BASE_PAGE_SIZE - 1);
            if (base > 0x80000000) {
                debug_printf("my dbg provide cap x1.\n");
                err = get_device_cap(base, regs[i]->registers[j][1], &device_frame);
                assert(err_is_ok(err));
            }
            else {
                debug_printf("my dbg provide cap x2.\n");
                err = get_shared_cap(base, regs[i]->registers[j][1], &device_frame);
                assert(err_is_ok(err));
            }

            KALUGA_DEBUG("get_device_cap worked\n");
            debug_printf("my dbg provide cap 2.\n");
            err = ddomain_driver_add_cap(drv, device_frame);
            assert(err_is_ok(err));
        }
    }
}

/**
 * \brief Startup function for new-style ARMv8 drivers.
 *
 * Launches the driver instance in a driver domain instead.
 */
errval_t
default_start_function_new(coreid_t where, struct module_info* mi, char* record,
                        struct driver_argument* arg)
{
    assert(mi != NULL);
    assert(record != NULL);

    struct domain_instance* inst = instantiate_driver_domain(mi->binary, where);

    char module_name[100];
    sprintf(module_name, "%s_module", mi->binary);

    struct driver_instance* drv = ddomain_create_driver_instance(module_name, module_name);
    provide_driver_with_caps(drv, mi->binary);
    ddomain_instantiate_driver(inst, drv);

    return SYS_ERR_OK;
}

errval_t start_networking_new(coreid_t where,
                              struct module_info* driver,
                              char* record, struct driver_argument *arg)
{
    errval_t err;

    if (is_started(driver)) {
        printf("Already started %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_ALREADY_STARTED;
    }

    if (!is_auto_driver(driver)) {
        printf("Not auto %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    err = default_start_function_new(where, driver, record, arg);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Spawning %s failed.", driver->path);
        return err;
    }


    // cards with driver in seperate process TODO might put into same process

    struct module_info* net_sockets = find_module("net_sockets_server");
    if (net_sockets == NULL) {
        printf("Net sockets server not found\n");
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    // Spawn net_sockets_server
    net_sockets->argv[0] = "net_sockets_server";
    net_sockets->argv[1] = "auto";
    net_sockets->argv[2] = driver->binary;
#ifdef CLIENT
    net_sockets->argv[3] = "--ip=192.168.0.1";
#else
    net_sockets->argv[3] = "--ip=192.168.0.2";
#endif

    err = spawn_program(where, net_sockets->path, net_sockets->argv, environ, 0,
                        get_did_ptr(net_sockets));

    return err;
}
