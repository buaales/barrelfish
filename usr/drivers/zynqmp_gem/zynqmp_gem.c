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

static struct zynqmp_gem_state* gem_state;

static void tx_request_cap_response_devif(struct zynqmp_gem_state* st);
static void tx_frame_polled_devif(struct zynqmp_gem_state* st);

static void rx_request_cap_call_devif(struct zynqmp_gem_devif_binding *b) {
    ZYNQMP_GEM_DEBUG("rx_request_cap_call_devif\n");
    struct zynqmp_gem_state *state = b->st;
    tx_request_cap_response_devif(state);
}

static rx_request_cap_call_pollif(struct zynqmp_gem_pollif_binding* b) {
    ZYNQMP_GEM_DEBUG("rx_request_cap_call_pollif\n");
    struct zynqmp_gem_state* state = b->st;
    tx_request_cap_response_pollif(state);
}

static void rx_frame_polled_pollif(struct zynqmp_gem_pollif_binding* b) {
    ZYNQMP_GEM_DEBUG("rx_frame_polled_pollif\n");
    struct zynqmp_gem_state* state = b->st;
    tx_frame_polled_devif(state);
}

static void tx_frame_polled_devif_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_frame_polled_devif done.");
}

static void tx_frame_polled_devif(struct zynqmp_gem_state* st) {
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_frame_polled_devif_cb, (void*)(st->binding));
    err = zynqmp_gem_devif_frame_polled__tx(st->binding, txcont);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_devif again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_frame_polled_devif, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_frame_polled_devif register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_devif error\n");
        }
    }
}

static void tx_request_cap_response_devif_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_response_devif done.");
}
static void tx_request_cap_response_devif(struct zynqmp_gem_state* st) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_response_devif\n");
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_request_cap_response_devif_cb, (void*)(st->binding));
    err = zynqmp_gem_devif_request_cap_response__tx(st->binding, txcont, st->mac, st->shared_vars_region, st->shared_tx_region, st->shared_rx_region);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_devif again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_request_cap_response_devif, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_frame_polled_devif register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_devif error\n");
        }
    }
}

static void tx_request_cap_response_pollif_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_response_devif done.");
}
static void tx_request_cap_response_pollif(struct zynqmp_gem_state* st) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_response_pollif\n");
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_request_cap_response_pollif_cb, (void*)(st->binding));
    err = zynqmp_gem_pollif_request_cap_response__tx(st->binding, txcont, st->shared_vars_region);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_pollif again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_request_cap_response_pollif, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_frame_polled_pollif register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_frame_polled_pollif error\n");
        }
    }
}

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
    struct zynqmp_gem_state* state = (struct zynqmp_gem_state*)st;
    b->rx_vtbl.request_cap_call = rx_request_cap_call_devif;
    b->st = state;
    state->binding = b;
    
    return SYS_ERR_OK;
}

static void export_pollif_cb(void* st, errval_t err, iref_t iref)
{
    struct zynqmp_gem_state* s = (struct zynqmp_gem_state*)st;
    const char* suffix = "pollif";
    char name[256];

    assert(err_is_ok(err));

    // Build label for interal management service
    sprintf(name, "%s_%s", s->service_name, suffix);
    ZYNQMP_GEM_DEBUG("registering nameservice %s.\n", name);
    err = nameservice_register(name, iref);
    assert(err_is_ok(err));
}

static errval_t connect_pollif_cb(void* st, struct zynqmp_gem_pollif_binding* b)
{
    struct zynqmp_gem_state* state = (struct zynqmp_gem_state*)st;
    b->rx_vtbl.frame_polled = rx_frame_polled_pollif;
    b->rx_vtbl.request_cap_call = rx_request_cap_call_pollif;
    b->st = state;
    state->poll_binding = b;

    return SYS_ERR_OK;
}
static void zynqmp_gem_init_mngif(struct zynqmp_gem_state *st)
{
    errval_t err;
    err = zynqmp_gem_devif_export(st, export_devif_cb, connect_devif_cb,
                           get_default_waitset(), 1);
    assert(err_is_ok(err));
}

static void zynqmp_gem_init_pollif(struct zynqmp_gem_state* st) {
    errval_t err;
    err = zynqmp_gem_pollif_export(st, export_pollif_cb, connect_pollif_cb,
        get_default_waitset(), 1);
    assert(err_is_ok(err));

}

static errval_t init(struct bfdriver_instance* bfi, const char* name, uint64_t
        flags, struct capref* caps, size_t caps_len, char** args, size_t
        args_len, iref_t* dev) {

    void* va;
    gem_state = (struct zynqmp_gem_state*)malloc(sizeof(struct zynqmp_gem_state));
    gem_state->initialized = false;
    gem_state->service_name = "zynqmp_gem";

    ZYNQMP_GEM_DEBUG("zynqmp gem init\n");
    vspace_map_one_frame_attr(&va, PRESET_DATA_SIZE, caps[1], VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    gem_state->mac = *(uint64_t *)va;

    gem_state->shared_vars_region = caps[2];
    vspace_map_one_frame_attr(&va, SHARED_REGION_VARIABLES_SIZE, gem_state->shared_vars_region, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    gem_state->shared_vars_base = (lvaddr_t)va;
    gem_state->shared_tx_region = caps[3];
    gem_state->shared_rx_region = caps[4];

    /* For use with the net_queue_manager */

    zynqmp_gem_init_mngif(gem_state);
    zynqmp_gem_init_pollif(gem_state);
    
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
