
#include <barrelfish/barrelfish.h>
#include <barrelfish/nameservice_client.h>
#include <if/zynqmp_gem_pollif_defs.h>
#include "zynqmp_gem.h"
#include "zynqmp_gem_debug.h"

static struct zynqmp_gem_poll_state *poll_state;

static void rx_request_cap_response(struct zynqmp_gem_pollif_binding* b, struct capref vars) {
    struct zynqmp_gem_poll_state* state = b->st;
    void* va;
    state->shared_vars_region = vars;
    vspace_map_one_frame_attr(&va, SHARED_REGION_VARIABLES_SIZE, state->shared_vars_region, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    state->shared_vars_base = (lvaddr_t)va;
}

static void tx_request_cap_call_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_call done.");
}

static void tx_request_cap_call(struct zynqmp_gem_poll_state* st) {
    ZYNQMP_GEM_DEBUG("tx_request_cap_call\n");
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_request_cap_call_cb, (void*)(st->binding));
    err = zynqmp_gem_pollif_request_cap_call__tx(st->binding, txcont);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_request_cap_call again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_request_cap_call, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_request_cap_call register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_request_cap_call error\n");
        }
    }
}

static void tx_frame_polled_cb(void* a) {
    ZYNQMP_GEM_DEBUG("tx_frame_polled done.");
}

static void tx_frame_polled(struct zynqmp_gem_poll_state* st) {
    ZYNQMP_GEM_DEBUG("tx_frame_polled\n");
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void*))tx_frame_polled_cb, (void*)(st->binding));
    err = zynqmp_gem_pollif_frame_polled__tx(st->binding, txcont);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_GEM_DEBUG("tx_frame_polled again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void*))tx_frame_polled, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_GEM_DEBUG("tx_frame_polled register failed!");
            }
        }
        else {
            ZYNQMP_GEM_DEBUG("tx_frame_polled error\n");
        }
    }
}


static void bind_cb(void* st, errval_t err, struct zynqmp_gem_pollif_binding* b)
{
    struct zynqmp_gem_poll_state* state = (struct zynqmp_gem_poll_state*)st;
    b->st = st;
    b->rx_vtbl.request_cap_response = rx_request_cap_response;
    state->binding = b;
    state->bound = true;
}

static errval_t init(void) {
    errval_t err;
    poll_state = (struct zynqmp_gem_poll_state*)malloc(sizeof(struct zynqmp_gem_poll_state));

    char service[128] = "zynqmp_gem_pollif";
    iref_t iref;
    poll_state->shared_vars_base = 0;
    poll_state->shared_vars_region = NULL_CAP;

    err = nameservice_blocking_lookup(service, &iref);
    if (err_is_fail(err)) {
        return err;
    }

    err = zynqmp_gem_pollif_bind(iref, bind_cb, poll_state, get_default_waitset(), 1);
    if (err_is_fail(err)) {
        return err;
    }

    while (!poll_state->bound) {
        event_dispatch(get_default_waitset());
    }

    tx_request_cap_call(poll_state);

    return SYS_ERR_OK;
}

static void poll(void) {
    uint32_t head, tail;
    head = ((uint32_t*)poll_state->shared_vars_base)[3];
    tail = ((uint32_t*)poll_state->shared_vars_base)[4];
    if (head != tail) {
        tx_frame_polled(poll_state);
    }
}

int main(int argc, char** argv)
{
    init();
    while (poll_state->shared_vars_base == 0) {
        event_dispatch(get_default_waitset());
    }
    while (1) {
        poll();
        event_dispatch(get_default_waitset());
    }
    return 0;
}