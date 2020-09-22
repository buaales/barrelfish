#include <barrelfish/barrelfish.h>
#include <barrelfish/capabilities.h>
#include <barrelfish/nameservice_client.h>

#include <stdio.h>
#include <stdlib.h>
#include <mm/mm.h>
#include <if/monitor_blocking_defs.h>

#include "kaluga.h"
#include <memory_maps.h>

struct mem_regions
{
    genpaddr_t base;
    size_t size;
};

static struct mem_regions regions[] = {
    {PRESET_DATA_BASE, PRESET_DATA_SIZE},
    {SHARED_REGION_VARIABLES_BASE, SHARED_REGION_VARIABLES_SIZE},
    {SHARED_REGION_CNI_MSG_BASE, SHARED_REGION_CNI_MSG_SIZE},
    {SHARED_REGION_ETH_TX_BASE, SHARED_REGION_ETH_SIZE},
    {SHARED_REGION_ETH_RX_BASE, SHARED_REGION_ETH_SIZE},
};

static struct mm shared_manager;

/**
 * \brief Constructs a capability to a physical device range
 *
 * \param[in] address Physical address of register you want to map.
 * \param[in] size Size of register space to map
 * \param[out] devframe Capability to device range
 *
 * \retval SYS_ERR_OK devframe is a valid capability for the requested range.
 **/
errval_t get_shared_cap(genpaddr_t address, size_t size, struct capref* devframe) 
{
    KALUGA_DEBUG("map_device_register: %"PRIxLPADDR" %zu %u\n", address, size, log2ceil(size));

    struct allocated_range {
        struct capref cr;
        struct frame_identity id;
        struct allocated_range* next;
    };

    static struct allocated_range* allocation_head = NULL;

    assert(address != 0);
    // TODO(gz) the paging in the kernel wants these two preconditions?
    assert(size % BASE_PAGE_SIZE == 0);
    assert(size >= BASE_PAGE_SIZE && "ARM paging breaks when smaller");

    errval_t err = mm_alloc_range(&shared_manager, log2ceil(size),
                                  address, address+size,
                                  devframe, NULL);
    if (err_is_fail(err)) {
        // TODO(gz) Is there a better way to handle duplicated allocations?
        KALUGA_DEBUG("mm_alloc_range failed.\n");
        KALUGA_DEBUG("Do we already have an allocation that covers this range?\n");
        struct allocated_range* iter = allocation_head;
        while (iter != NULL) {
            if (address >= iter->id.base && 
                (address + size <= (iter->id.base + iter->id.bytes))) {
                KALUGA_DEBUG("Apparently, yes. We try to map that one.\n");
                *devframe = iter->cr;                
                return SYS_ERR_OK;
            }
            iter = iter->next;
        }
        // One way out of here might be to re-try with
        // a bigger maxlimit?
        DEBUG_ERR(err, "mm_alloc_range failed.\n");
        return err;
    }

    struct allocated_range* ar = calloc(sizeof(struct allocated_range), 1);
    assert(ar != NULL);
    ar->cr = *devframe;
    err = frame_identify(ar->cr, &ar->id);

    // Insert into the queue to track the allocation
    struct allocated_range** iter = &allocation_head;
    while(*iter != NULL) {
        iter = &(*iter)->next;
    }
    *iter = ar;

    return SYS_ERR_OK;
}

errval_t init_shared_caps_manager(void)
{
    errval_t err, msgerr;
    struct monitor_blocking_binding* cl = get_monitor_blocking_binding();
    assert(cl != NULL);

    static struct range_slot_allocator devframes_allocator;
    err = range_slot_alloc_init(&devframes_allocator, L2_CNODE_SLOTS, NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC_INIT);
    }

    err = range_slot_alloc_refill(&devframes_allocator, L2_CNODE_SLOTS);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC_INIT);
    }

    err = mm_init(&shared_manager, ObjType_DevFrame, PRESET_DATA_BASE, (4 * 7 + 2),
        /* This next parameter is important. It specifies the maximum
         * amount that a cap may be "chunked" (i.e. broken up) at each
         * level in the allocator. Setting it higher than 1 reduces the
         * memory overhead of keeping all the intermediate caps around,
         * but leads to problems if you chunk up a cap too small to be
         * able to allocate a large subregion. This caused problems
         * for me with a large framebuffer... -AB 20110810 */
        1, /*was DEFAULT_CNODE_BITS,*/
        slab_default_refill, slot_alloc_dynamic,
        slot_refill_dynamic, &devframes_allocator, false);
    if (err_is_fail(err)) {
        return err_push(err, MM_ERR_MM_INIT);
    }

    struct capref requested_caps;

    // XXX: The code below is confused about gen/l/paddrs.
    // Caps should be managed in genpaddr, while the bus mgmt must be in lpaddr.

    // Here we get a cnode cap, so we need to put it somewhere in the root cnode
    // As we already have a reserved slot for a phyaddr caps cnode, we put it there
    err = slot_alloc(&requested_caps);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "slot_alloc for monitor->get_phyaddr_cap");
    }
    err = cl->rpc_tx_vtbl.get_phyaddr_cap(cl, &requested_caps, &msgerr);
    assert(err_is_ok(err) && err_is_ok(msgerr));

    struct capref pacn = {
        .cnode = cnode_root,
        .slot = ROOTCN_SLOT_PACN
    };
    // Move phyaddr cap to ROOTCN_SLOT_PACN to conform to 2 level cspace
    err = cap_copy(pacn, requested_caps);
    assert(err_is_ok(err));

    // Build the capref for the first physical address capability
    struct capref phys_cap;
    phys_cap.cnode = build_cnoderef(pacn, CNODE_TYPE_OTHER);
    phys_cap.slot = 0;

    struct cnoderef devcnode;
    struct capref devcnode_cap;
    err = cnode_create_l2(&devcnode_cap, &devcnode);
    if (err_is_fail(err)) { USER_PANIC_ERR(err, "cnode create"); }
    struct capref devframe;
    devframe.cnode = devcnode;
    devframe.slot = 0;

    for (int i = 0; i <= sizeof(regions) / sizeof(struct mem_region); i++) {
        err = cap_retype(devframe, phys_cap, 0, ObjType_DevFrame, regions[i].size, 1);
        if (err_is_ok(err)) {
            err = mm_add_multi(&shared_manager, devframe, regions[i].size,
                regions[i].base);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "adding region %d FAILED\n", i);
            }
        }
        else {
            if (err_no(err) == SYS_ERR_REVOKE_FIRST) {
                printf("cannot retype region %d: need to revoke first; ignoring it\n", i);
            }
            else {
                USER_PANIC_ERR(err, "error in retype\n");
            }
        }
        devframe.slot++;
        phys_cap.slot++;
    }

    KALUGA_DEBUG("init_cap_manager DONE\n");
    return SYS_ERR_OK;
}
