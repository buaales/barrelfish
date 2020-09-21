/*
 * Copyright (c) 2017 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/deferred.h>
#include <barrelfish/nameservice_client.h>
#include <barrelfish/inthandler.h>
#include <devif/queue_interface_backend.h>
#include <devif/backends/net/zynqmp_gem_devif.h>
#include <net_interfaces/flags.h>
#include <debug_log/debug_log.h>
#include <if/zynqmp_gem_devif_defs.h>
#include <int_route/int_route_client.h>


#include "zynqmp_gem.h"
#include "zynqmp_gem_devq.h"
#include "zynqmp_gem_debug.h"

lvaddr_t tx_addrs[ZYNQMP_GEM_N_BUFS] = { 0 };
lvaddr_t rx_addrs[ZYNQMP_GEM_N_BUFS] = { 0 };

static errval_t zynqmp_gem_register(struct devq* q, struct capref cap,
                                  regionid_t rid)
{    
    zynqmp_gem_queue_t *q_ext = (zynqmp_gem_queue_t *)q;
    errval_t err;
    void *va;
    
    q_ext->region = cap;
    q_ext->rid = rid;
    err = vspace_map_one_frame_attr(&va, ZYNQMP_GEM_N_BUFS * ZYNQMP_GEM_FRAMESIZE, cap, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL); 
    q_ext->region_base = (lvaddr_t)va;

    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_deregister(struct devq* q, regionid_t rid)
{
    return SYS_ERR_OK;
}


static errval_t zynqmp_gem_control(struct devq* q, uint64_t cmd, uint64_t value,
                                 uint64_t *result)
{
    zynqmp_gem_queue_t *q_ext = (zynqmp_gem_queue_t *)q;
    *result = q_ext->mac_address;
    ZYNQMP_GEM_DEBUG("control returned mac address:%x.\n", result);
    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_enqueue_rx(zynqmp_gem_queue_t *q, regionid_t rid,
                               genoffset_t offset, genoffset_t length,
                               genoffset_t valid_data, genoffset_t valid_length,
                               uint64_t flags) 
{

    if (q->rx_head == (q->rx_tail + 1) % ZYNQMP_GEM_N_BUFS) {
        return DEVQ_ERR_QUEUE_FULL;
    }

    rx_addrs[q->rx_tail] = q->region_base + offset + valid_data;
    q->rx_tail = (q->rx_tail + 1) % ZYNQMP_GEM_N_BUFS;

    return SYS_ERR_OK;
} 

static errval_t zynqmp_gem_dequeue_rx(zynqmp_gem_queue_t *q, regionid_t* rid, 
                                 genoffset_t* offset,
                                 genoffset_t* length, genoffset_t* valid_data,
                                 genoffset_t* valid_length, uint64_t* flags)
{
    ZYNQMP_GEM_DEBUG("zynqmp gem dequeue rx called.\n");
    if (q->rx_head == q->rx_tail) {
        return DEVQ_ERR_QUEUE_EMPTY;
    }
    
    // make sure shared rx tail goes first.
    uint32_t shared_region_head, shared_region_tail;
    shared_region_head = ((uint32_t*)q->shared_vars_base)[3];
    shared_region_tail = ((uint32_t*)q->shared_vars_base)[4];
    if (shared_region_head == shared_region_tail) {
        return DEVQ_ERR_QUEUE_EMPTY;
    }
    memcpy((void*)rx_addrs[q->rx_head], (void*)(q->shared_rx_base + q->rx_head * ZYNQMP_GEM_BUFSIZE), ZYNQMP_GEM_FRAMESIZE);
    
    *rid = q->rid;
    *offset = rx_addrs[q->rx_head];
    *length = ZYNQMP_GEM_FRAMESIZE;
    *valid_data = 0;
    *valid_length = ZYNQMP_GEM_FRAMESIZE;
    *flags = NETIF_RXFLAG;

    q->rx_head = (q->rx_head + 1) % ZYNQMP_GEM_N_BUFS; 

    ((uint32_t*)q->shared_vars_base)[3] = q->rx_head;
    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_enqueue_tx(zynqmp_gem_queue_t *q, regionid_t rid,
                               genoffset_t offset, genoffset_t length,
                               genoffset_t valid_data, genoffset_t valid_length,
                               uint64_t flags)
{
    ZYNQMP_GEM_DEBUG("zynqmp gem enqueue tx called.\n");
    if (q->tx_head == (q->tx_tail + 1) % ZYNQMP_GEM_N_BUFS) {
        return DEVQ_ERR_QUEUE_FULL;
    }

    tx_addrs[q->tx_tail] = q->region_base + offset + valid_data;
    assert(length <= ZYNQMP_GEM_FRAMESIZE);

    // q tx tail goes first, shared tx tail updates afterwards.
    memcpy((void*)(q->shared_tx_base + q->tx_tail * ZYNQMP_GEM_BUFSIZE), (void*)tx_addrs[q->tx_tail], length);

    q->tx_tail = (q->tx_tail + 1) % ZYNQMP_GEM_N_BUFS;

    if (flags & NETIF_TXFLAG_LAST) {
        ((uint32_t*)q->shared_vars_base)[2] = q->tx_tail;
        q->isr(q);
    }

    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_dequeue_tx(zynqmp_gem_queue_t *q, regionid_t* rid, genoffset_t* offset,
                                 genoffset_t* length, genoffset_t* valid_data,
                                 genoffset_t* valid_length, uint64_t* flags)
{
    ZYNQMP_GEM_DEBUG("zynqmp gem dequeue tx called.\n");
    if (q->tx_head == q->tx_tail) {
        return DEVQ_ERR_QUEUE_EMPTY;
    }

    *rid = q->rid;
    *offset = tx_addrs[q->tx_head];
    *length = ZYNQMP_GEM_FRAMESIZE;
    //valid_data stands for the offset value within a single buffer.
    *valid_data = *offset & (ZYNQMP_GEM_FRAMESIZE - 1);
    //the inner buffer offset value truncated from total offset.
    *offset &= ~(ZYNQMP_GEM_FRAMESIZE - 1);
    *valid_length = ZYNQMP_GEM_FRAMESIZE;
    // frame size fixed, every frame occupies just 1 buf.
    *flags = NETIF_TXFLAG | NETIF_TXFLAG_LAST;
    q->tx_head = (q->tx_head + 1) % ZYNQMP_GEM_N_BUFS;

    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_enqueue(struct devq* q, regionid_t rid,
                                 genoffset_t offset, genoffset_t length,
                                 genoffset_t valid_data, genoffset_t valid_length,
                                 uint64_t flags)
{
    zynqmp_gem_queue_t *q_ext = (zynqmp_gem_queue_t *)q;
    errval_t err;
    if (flags & NETIF_RXFLAG) {
        assert(length <= ZYNQMP_GEM_FRAMESIZE);

        err = zynqmp_gem_enqueue_rx(q_ext, rid, offset, length, valid_data, valid_length,
                             flags);
        if (err_is_fail(err)) {
            return err;
        }
    } else if (flags & NETIF_TXFLAG) {
        assert(length <= ZYNQMP_GEM_FRAMESIZE);

        err = zynqmp_gem_enqueue_tx(q_ext, rid, offset, length, valid_data, valid_length,
                             flags);
        if (err_is_fail(err)) {
            return err;
        }
    } else {
        printf("Unknown buffer flags \n");
        return NIC_ERR_ENQUEUE;
    }

    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_dequeue(struct devq* q, regionid_t* rid, genoffset_t* offset,
                                 genoffset_t* length, genoffset_t* valid_data,
                                 genoffset_t* valid_length, uint64_t* flags)
{
    zynqmp_gem_queue_t *q_ext = (zynqmp_gem_queue_t *)q;
    
    if (zynqmp_gem_dequeue_tx(q_ext, rid, offset, length, valid_data, valid_length,  flags) == SYS_ERR_OK) 
        return SYS_ERR_OK;
    if (zynqmp_gem_dequeue_rx(q_ext, rid, offset, length, valid_data, valid_length, flags) == SYS_ERR_OK) {
        return SYS_ERR_OK;
    }
    return DEVQ_ERR_QUEUE_EMPTY;
}

static errval_t zynqmp_gem_notify(struct devq* q)
{
    printf("not supposed to be called.\n");
    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_destroy(struct devq * q)
{
    errval_t err;
    zynqmp_gem_queue_t* q_ext = (zynqmp_gem_queue_t *) q;
    err = vspace_unmap((void*)q_ext->region_base);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->region);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->shared_vars_base);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->shared_vars_region);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->shared_rx_base);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->shared_rx_region);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->shared_tx_base);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->shared_tx_region);
    assert(err_is_ok(err));

    free(q_ext);
    return SYS_ERR_OK;
}

static void rx_frame_polled(struct zynqmp_gem_devif_binding* b) {
    struct zynqmp_gem_queue* q = b->st;
    q->isr(q);
}
static void tx_create_queue_call_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_create_queue_call done.");
}

static void tx_create_queue_call(struct zynqmp_gem_queue* st) {
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_create_queue_call_cb, (void*)(st->binding));
    err = zynqmp_gem_devif_create_queue_call__tx(st->binding, txcont, st->shared_vars_region);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_create_queue_call again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_create_queue_call, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_create_queue_call register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_create_queue_call error\n");
        }
    }
}

static void bind_cb(void *st, errval_t err, struct zynqmp_gem_devif_binding *b)
{
    zynqmp_gem_queue_t* q = (zynqmp_gem_queue_t*) st;
    b->st = st;
    b->rx_vtbl.frame_polled = rx_frame_polled;
    
    q->binding = b;
    q->bound = true;
}

errval_t zynqmp_gem_queue_create(zynqmp_gem_queue_t ** pq, void (*int_handler)(void *))
{
    errval_t err;
    zynqmp_gem_queue_t *q;
    void *va;
    struct capref tmp;

    ZYNQMP_GEM_DEBUG("zynqmp gem queue create called.\n");

    q = malloc(sizeof(zynqmp_gem_queue_t));
    assert(q);
    q->isr = int_handler;
    q->rid = 0;
    q->rx_head = 0;
    q->rx_tail = 0;
    q->tx_head = 0;
    q->tx_tail = 0;
    q->bound = false;
    ZYNQMP_GEM_DEBUG("zynqmp gem queue create .\n");
    err = get_ram_cap(&tmp, PRESET_DATA_BASE, PRESET_DATA_SIZE_BITS);
    assert(err_is_ok(err));
    vspace_map_one_frame_attr(&va, PRESET_DATA_SIZE, tmp, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    q->mac_address = *(uint64_t*)va;
    err = vspace_unmap(va);
    assert(err_is_ok(err));
    err = cap_delete(tmp);
    assert(err_is_ok(err));

    err = get_ram_cap(&(q->shared_vars_region), SHARED_REGION_VARIABLES_BASE, SHARED_REGION_VARIABELS_SIZE_BITS);
    assert(err_is_ok(err));
    vspace_map_one_frame_attr(&va, SHARED_REGION_VARIABELS_SIZE, q->shared_vars_region, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    q->shared_vars_base = (lvaddr_t)va;
    err = get_ram_cap(&(q->shared_rx_region), SHARED_REGION_ETH_RX_BASE, SHARED_REGION_ETH_SIZE_BITS);
    assert(err_is_ok(err));
    vspace_map_one_frame_attr(&va, SHARED_REGION_ETH_SIZE, q->shared_rx_region, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    q->shared_rx_base = (lvaddr_t)va;
    err = get_ram_cap(&(q->shared_tx_region), SHARED_REGION_ETH_TX_BASE, SHARED_REGION_ETH_SIZE_BITS);
    assert(err_is_ok(err));
    vspace_map_one_frame_attr(&va, SHARED_REGION_ETH_SIZE, q->shared_tx_region, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    q->shared_tx_base = (lvaddr_t)va;

    char service[128] = "zynqmp_gem_devif";
    iref_t iref;

    err = nameservice_blocking_lookup(service, &iref);
    if (err_is_fail(err)) {
        return err;
    }

    err = zynqmp_gem_devif_bind(iref, bind_cb, q, get_default_waitset(), 1);
    if (err_is_fail(err)) {
        return err;
    }
    
    // wait until bound
    while(!q->bound) {
        event_dispatch(get_default_waitset());
    }

    tx_create_queue_call(q);

    if (err_is_fail(err)) {
        return err;
    }

    err = devq_init(&q->q, false);
    ZYNQMP_GEM_DEBUG("devq initialized.\n");
    assert(err_is_ok(err));
    
    q->q.f.enq = zynqmp_gem_enqueue;
    q->q.f.deq = zynqmp_gem_dequeue;
    q->q.f.reg = zynqmp_gem_register;
    q->q.f.dereg = zynqmp_gem_deregister;
    q->q.f.ctrl = zynqmp_gem_control;
    q->q.f.notify = zynqmp_gem_notify;
    q->q.f.destroy = zynqmp_gem_destroy;
    
    *pq = q;

    return SYS_ERR_OK;
}
