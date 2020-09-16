/**
 * \file
 * \brief ZYNQMP GEM driver.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <barrelfish/barrelfish.h>
#include <barrelfish/inthandler.h>
#include <barrelfish/nameservice_client.h>
#include <barrelfish/systime.h>

#include <maps/zynqmp_map.h>

#include <if/zynqmp_gem_devif_defs.h>

#include <dev/zynqmp_gem_dev.h>

#include <driverkit/driverkit.h>

#include "zynqmp_gem_debug.h"
#include "zynqmp_gem.h"

void foo(void) {
    printf("foo\n");
}

/*
static void rx_create_queue_call(struct zynqmp_gem_devif_binding *b,
        struct capref rx, struct capref dummy_rx,
        struct capref tx, struct capref dummy_tx) {
    struct zynqmp_gem_state *state = b->st;
    lvaddr_t va;

    err = vspace_map_one_frame_attr(&va, ZYNQMP_GEM_N_RX_BUFS * sizeof(rx_desc_t), rx, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    assert(err_is_ok(err));
    state->rx_descq = (rx_desc_t*)va;

    err = vspace_map_one_frame_attr(&va, ZYNQMP_GEM_N_TX_BUFS * sizeof(tx_desc_t), tx, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    assert(err_is_ok(err));
    state->tx_descq = (tx_desc_t*)va;

    tx_create_queue_response(state, state->mac);

    return SYS_ERR_OK;
}

static void rx_transmit_start(struct zynqmp_gem_devif_binding* b) {
    struct zynqmp_gem_state* state = b->st;
    uint32_t *p_tx_head = (uint32_t *)(state->vars_base + 0x4);
    uint32_t tx_tail = *(uint32_t *)(state->vars_base + 0x8);
}

static void tx_create_queue_response_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx create queue response sent\n");
}

static void tx_create_queue_response(void* a, uint64_t mac) {
    errval_t err;
    struct zynqmp_gem_state* st = a;

    ZYNQMP_GEM_DEBUG("tx create queue response\n");

    struct event_closure txcont = MKCONT(tx_create_queue_response_cb, st);
    err = zynqmp_gem_devif_create_queue_response__tx(st->b, txcont, mac);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx create queue response again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT(tx_create_queue_response, st);
            err = st->b->register_send(st->b, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx create queue response register failed\n");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx create queue response error\n");
        }
    }
}
 */

static void export_devif_cb(void *st, errval_t err, iref_t iref)
{
    struct zynqmp_gem_state* s = (struct zynqmp_gem_state*) st;
    const char *suffix = "devif";
    char name[256];

    assert(err_is_ok(err));

    // Build label for interal management service
    sprintf(name, "%s_%s", s->service_name, suffix);
    ZYNQMP_GEM_DEBUG("registering nameservice %s.\n", name);
    err = nameservice_register(name, iref);
    assert(err_is_ok(err));
    s->initialized = true;
}

static errval_t connect_devif_cb(void *st, struct zynqmp_gem_devif_binding *b)
{
    //b->rx_vtbl.create_queue_call = on_create_queue;
    b->st = st;
    return SYS_ERR_OK;
}

static void zynqmp_gem_init_mngif(struct zynqmp_gem_state *st)
{
    errval_t err;
    err = zynqmp_gem_devif_export(st, export_devif_cb, connect_devif_cb,
                           get_default_waitset(), 1);
    assert(err_is_ok(err));
}

static errval_t init(struct bfdriver_instance* bfi, const char* name, uint64_t
        flags, struct capref* caps, size_t caps_len, char** args, size_t
        args_len, iref_t* dev) {

    struct zynqmp_gem_state *state = (struct zynqmp_gem_state*)malloc(sizeof(struct zynqmp_gem_state));
    state->initialized = false;
    state->service_name = "zynqmp_gem";



    /* For use with the net_queue_manager */

    zynqmp_gem_init_mngif(state);
    
    return SYS_ERR_OK;
}

/**
 * Instructs driver to attach to the device.
 * This function is only called if the driver has previously detached
 * from the device (see also detach).
 *
 * \note After detachment the driver can not assume anything about the
 * configuration of the device.
 *
 * \param[in]   bfi   The instance of this driver.
 * \retval SYS_ERR_OK Device initialized successfully.
 */
static errval_t attach(struct bfdriver_instance* bfi) {
    return SYS_ERR_OK;
}

/**
 * Instructs driver to detach from the device.
 * The driver must yield any control over to the device after this function returns.
 * The device may be left in any state.
 *
 * \param[in]   bfi   The instance of this driver.
 * \retval SYS_ERR_OK Device initialized successfully.
 */
static errval_t detach(struct bfdriver_instance* bfi) {
    return SYS_ERR_OK;
}

/**
 * Instructs the driver to go in a particular sleep state.
 * Supported states are platform/device specific.
 *
 * \param[in]   bfi   The instance of this driver.
 * \retval SYS_ERR_OK Device initialized successfully.
 */
static errval_t set_sleep_level(struct bfdriver_instance* bfi, uint32_t level) {
    return SYS_ERR_OK;
}

/**
 * Destroys this driver instance. The driver will yield any
 * control over the device and free any state allocated.
 *
 * \param[in]   bfi   The instance of this driver.
 * \retval SYS_ERR_OK Device initialized successfully.
 */
static errval_t destroy(struct bfdriver_instance* bfi) {
    return SYS_ERR_OK;
}

/**
 * Registers the driver module with the system.
 *
 * To link this particular module in your driver domain,
 * add it to the addModules list in the Hakefile.
 */
DEFINE_MODULE(zynqmp_gem_module, init, attach, detach, set_sleep_level, destroy);
