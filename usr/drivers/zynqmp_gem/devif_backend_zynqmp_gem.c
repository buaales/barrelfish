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
#include "zynqmp_gem_desc.h"
#include "zynqmp_gem_devq.h"
#include "zynqmp_gem_debug.h"


/*****************************************************************
 * allocate a single frame, mapping it into our vspace with given
 * attributes
 ****************************************************************/

static void *alloc_map_frame(vregion_flags_t attr, size_t size, struct capref *retcap)
{
    struct capref frame;
    errval_t err;
    void *va;

    err = frame_alloc(&frame, size, NULL);
    if (err_is_fail(err)) {
        ZYNQMP_GEM_DEBUG("ERROR: frame_alloc failed.\n");
        return NULL;
    }

    err = vspace_map_one_frame_attr(&va, size, frame, attr, NULL, NULL);

    if (err_is_fail(err)) {
        ZYNQMP_GEM_DEBUG("Error: vspace_map_one_frame failed\n");
        return NULL;
    }

    if (retcap != NULL) {
        *retcap = frame;
    }

    return va;
}

static errval_t zynqmp_gem_register(struct devq* q, struct capref cap,
                                  regionid_t rid)
{    
    zynqmp_gem_queue_t *q_ext = (zynqmp_gem_queue_t *)q;
    struct frame_identity id;
    errval_t err;
    
    err = invoke_frame_identify(cap, &id);
    assert(err_is_ok(err));
    assert(!q_ext->region_id);
    q_ext->region_id = rid;
    q_ext->region_base = id.base;
    q_ext->region_size = id.bytes;
    ZYNQMP_GEM_DEBUG("buffer region registered: rid:%d, base:%lx, size:%lx\n",
            rid, id.base, id.bytes);

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

    if (zynqmp_gem_queue_free_rxslots(q) == 0) {
        ZYNQMP_GEM_DEBUG("Not enough space in RX ring. \n" );
        return DEVQ_ERR_QUEUE_FULL;
    }

    rx_desc_t rxdesc;

    rxdesc.addr = ((q->rx_tail == q->n_rx_buffers - 1) ? ZYNQMP_GEM_RX_WRAP_MASK : 0)
            | ((q->region_base + offset) & ZYNQMP_GEM_RX_ADDR_MASK);
    rxdesc.info = 0;

    q->rx_ring[q->rx_tail] = rxdesc;
    q->rx_tail = (q->rx_tail + 1) % q->n_rx_buffers;

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

    volatile rx_desc_t *rxdesc;

    rxdesc = &q->rx_ring[q->rx_head];
    
    if (!(rxdesc->addr & ZYNQMP_GEM_RX_USED_MASK)) {
        //Used bit not set means that the descriptor is to be filled by NIC controller.
        //And not prepared to be processed by software.
        return DEVQ_ERR_QUEUE_EMPTY;
    }

    if (!(rxdesc->info & ZYNQMP_GEM_RX_SOF_MASK) ||
            !(rxdesc->info & ZYNQMP_GEM_RX_EOF_MASK)) {
        //As the reference manual said, a valid rx should have both bits set.
        return NIC_ERR_RX_PKT;
    }
    
    *rid = q->region_id;
    *offset = (rxdesc->addr & ZYNQMP_GEM_RX_ADDR_MASK) - q->region_base;
    *length = ZYNQMP_GEM_RX_BUFSIZE;
    *valid_data = 0;
    *valid_length = rxdesc->info & ZYNQMP_GEM_RX_LEN_MASK;
    *flags = NETIF_RXFLAG;

    rxdesc->addr = q->rx_head == q->n_rx_buffers - 1 ? ZYNQMP_GEM_RX_WRAP_MASK : 0;
    rxdesc->info = 0;
    q->rx_head = (q->rx_head + 1) % q->n_rx_buffers;

    return SYS_ERR_OK;
}

static errval_t zynqmp_gem_enqueue_tx(zynqmp_gem_queue_t *q, regionid_t rid,
                               genoffset_t offset, genoffset_t length,
                               genoffset_t valid_data, genoffset_t valid_length,
                               uint64_t flags)
{
    ZYNQMP_GEM_DEBUG("zynqmp gem enqueue tx called.\n");
    if (zynqmp_gem_queue_free_txslots(q) == 0) {
        ZYNQMP_GEM_DEBUG("Not enough space in TX ring, not transmitting buffer \n" );
        return DEVQ_ERR_QUEUE_FULL;
    }
    
    tx_desc_t txdesc;

    txdesc.addr = q->region_base + offset + valid_data;
    txdesc.info = (valid_length & ZYNQMP_GEM_TX_LEN_MASK)
            | ((flags & NETIF_TXFLAG_LAST) ? ZYNQMP_GEM_TX_LAST_MASK : 0)
            | (q->tx_tail == q->n_tx_buffers - 1 ? ZYNQMP_GEM_TX_WRAP_MASK : 0);

    ZYNQMP_GEM_DEBUG("insert txdesc into pos %d, addr:%x, info:%x.\n", q->tx_tail, txdesc.addr, txdesc.info);

    //Magic.
    q->tx_ring[(q->tx_tail + 1) % q->n_tx_buffers].info |= 0x00002000;

    q->tx_ring[q->tx_tail] = txdesc;
    q->tx_tail = (q->tx_tail + 1) % q->n_tx_buffers;

    ZYNQMP_GEM_DEBUG("before transmit.\n");
    for (int i = 0; i < 5; i++) {
        ZYNQMP_GEM_DEBUG("txdesc[%d] addr:%x, info:%x.\n", i, q->tx_ring[i].addr, q->tx_ring[i].info);
    }

    errval_t err = SYS_ERR_OK;
    if (flags & NETIF_TXFLAG_LAST) {
        err = q->b->tx_vtbl.transmit_start(q->b, NOP_CONT);
    }

    ZYNQMP_GEM_DEBUG("after transmit.\n");
    for (int i = 0; i < 5; i++) {
        ZYNQMP_GEM_DEBUG("txdesc[%d] addr:%x, info:%x.\n", i, q->tx_ring[i].addr, q->tx_ring[i].info);
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

    volatile tx_desc_t *txdesc;

    txdesc = &q->tx_ring[q->tx_head];
    *rid = q->region_id;
    *offset = txdesc->addr - q->region_base;
    *length = ZYNQMP_GEM_TX_BUFSIZE;
    //valid_data stands for the offset value within a single buffer.
    *valid_data = *offset & (ZYNQMP_GEM_TX_BUFSIZE - 1);
    //the inner buffer offset value truncated from total offset.
    *offset &= ~(ZYNQMP_GEM_TX_BUFSIZE - 1);
    *valid_length = txdesc->info | ZYNQMP_GEM_TX_LEN_MASK;
    *flags = NETIF_TXFLAG;
    if (txdesc->info & ZYNQMP_GEM_TX_LAST_MASK) *flags |= NETIF_TXFLAG_LAST;

    txdesc->addr = 0;
    txdesc->info = q->tx_head == q->n_tx_buffers - 1 ? ZYNQMP_GEM_TX_WRAP_MASK : 0;
    q->tx_head = (q->tx_head + 1) % q->n_tx_buffers;

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
        /* can not enqueue receive buffer larger than 2048 bytes */
        assert(length <= ZYNQMP_GEM_RX_BUFSIZE);

        err = zynqmp_gem_enqueue_rx(q_ext, rid, offset, length, valid_data, valid_length,
                             flags);
        if (err_is_fail(err)) {
            return err;
        }
    } else if (flags & NETIF_TXFLAG) {
        assert(length <= BASE_PAGE_SIZE);

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
    zynqmp_gem_queue_t* q_ext = (zynqmp_gem_queue_t *) q;
    errval_t err;
    err = vspace_unmap((void*)q_ext->rx_ring);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->dummy_rx_ring);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->tx_ring);
    assert(err_is_ok(err));
    err = vspace_unmap((void*)q_ext->dummy_tx_ring);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->rx_ring_cap);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->dummy_rx_ring_cap);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->tx_ring_cap);
    assert(err_is_ok(err));
    err = cap_delete(q_ext->dummy_tx_ring_cap);
    assert(err_is_ok(err));
    free(q_ext);
    return SYS_ERR_OK;
}

static void interrupt_handler(void* arg) {
    zynqmp_gem_queue_t *q = (zynqmp_gem_queue_t*) arg;
    q->b->tx_vtbl.interrupt(q->b, NOP_CONT);
    q->int_handler(arg);
}

static void rx_create_queue_response(struct zynqmp_gem_devif_binding* b, uint64_t mac) {
    zynqmp_gem_queue_t* q = (zynqmp_gem_queue_t*)b->st;
    q->mac_address = mac;
}

struct xmplrpc_rx_vtbl c_rx_vtbl = {
    .create_queue_resonse = rx_create_queue_response,
};

static void bind_cb(void *st, errval_t err, struct zynqmp_gem_devif_binding *b)
{
    zynqmp_gem_queue_t* q = (zynqmp_gem_queue_t*) st;
    b->st = st;
    b->rx_vtbl = c_rx_vtbl;
    
    q->b = b;
    q->bound = true;
}

errval_t zynqmp_gem_queue_create(zynqmp_gem_queue_t ** pq, void (*int_handler)(void *))
{
    errval_t err;
    zynqmp_gem_queue_t *q;
    int i;
    ZYNQMP_GEM_DEBUG("zynqmp gem queue create called.\n");

    q = malloc(sizeof(zynqmp_gem_queue_t));
    assert(q);

    q->rx_head = 0;
    q->rx_tail = 0;
    q->tx_head = 0;
    q->tx_tail = 0;
    q->region_id = 0;
    q->bound = false;

    q->n_rx_buffers = ZYNQMP_GEM_N_RX_BUFS;
    q->n_tx_buffers = ZYNQMP_GEM_N_TX_BUFS;

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

    ZYNQMP_GEM_DEBUG("allocating caps.\n");
    q->rx_ring = alloc_map_frame(VREGION_FLAGS_READ_WRITE_NOCACHE, 
            ZYNQMP_GEM_N_RX_BUFS * sizeof(rx_desc_t), &q->rx_ring_cap);
    if (q->rx_ring == NULL) {
        return DEVQ_ERR_INIT_QUEUE;
    }
    q->dummy_rx_ring = alloc_map_frame(VREGION_FLAGS_READ_WRITE_NOCACHE, 
            sizeof(rx_desc_t), &q->dummy_rx_ring_cap);
    if (q->dummy_rx_ring == NULL) {
        return DEVQ_ERR_INIT_QUEUE;
    }
    q->tx_ring = alloc_map_frame(VREGION_FLAGS_READ_WRITE_NOCACHE, 
            ZYNQMP_GEM_N_TX_BUFS * sizeof(tx_desc_t), &q->tx_ring_cap);
    if (q->tx_ring == NULL) {
        return DEVQ_ERR_INIT_QUEUE;
    }
    q->dummy_tx_ring = alloc_map_frame(VREGION_FLAGS_READ_WRITE_NOCACHE, 
            sizeof(tx_desc_t), &q->dummy_tx_ring_cap);
    if (q->dummy_tx_ring == NULL) {
        return DEVQ_ERR_INIT_QUEUE;
    }
    ZYNQMP_GEM_DEBUG("caps allocated.\n");
    
    for (i = 0; i < q->n_rx_buffers; i++) {
        zynqmp_gem_enqueue_rx(q, 0, i * ZYNQMP_GEM_RX_BUFSIZE, ZYNQMP_GEM_RX_BUFSIZE,
                0, ZYNQMP_GEM_RX_BUFSIZE, NETIF_RXFLAG);
    }
    q->dummy_rx_ring[0].addr = ZYNQMP_GEM_RX_WRAP_MASK | ZYNQMP_GEM_RX_USED_MASK;
    q->dummy_rx_ring[0].info = 0;
    q->dummy_tx_ring[0].addr = 0;
    q->dummy_tx_ring[0].info = ZYNQMP_GEM_TX_WRAP_MASK | ZYNQMP_GEM_TX_USED_MASK | ZYNQMP_GEM_TX_LAST_MASK;

    q->int_handler = int_handler;
    err = inthandler_setup_arm(interrupt_handler, q, ZYNQMP_GEM_IRQ);

    err = q->b->tx_vtbl.create_queue_call(q->b, q->rx_ring_cap, q->dummy_rx_ring_cap, q->tx_ring_cap, q->dummy_tx_ring_cap);
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
