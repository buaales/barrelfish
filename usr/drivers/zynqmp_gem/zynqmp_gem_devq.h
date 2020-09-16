/*
 * Copyright (c) 2008, ETH Zurich. All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __ZYNQMP_GEM_DEVQ_H__
#define __ZYNQMP_GEM_DEVQ_H__

#include <dev/zynqmp_gem_dev.h>

typedef struct zynqmp_gem_queue {
    struct devq q;

    uint64_t mac_address;

    uint32_t tx_head, tx_tail, rx_head, rx_tail;

    regionid_t rid;
    struct capref region;
    lvaddr_t region_base;

    struct capref shared_vars_region;
    lvaddr_t shared_vars_base;
    struct capref shared_tx_region;
    lvaddr_t shared_tx_base;
    struct capref shared_rx_region;
    lvaddr_t shared_rx_base;

    void (*isr)(void*);

    // binding
    bool bound;
    struct zynqmp_gem_devif_binding* binding;
} zynqmp_gem_queue_t;

#endif
