/**
 * \file
 * \brief TTP/C protocol communication network interface driver.
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

#include <driverkit/driverkit.h>

#include <if/zynqmp_cni_devif_defs.h>

#include <dev/zynqmp_cni_dev.h>

#include "zynqmp_cni_debug.h"
#include "zynqmp_cni.h"
#include "ram_caps.h"

static zynqmp_cni_t device;
static char msg[256];

static void tx_read_rx_msg_response(struct zynqmp_cni_state* st);
static void tx_check_controller_lifesign_response(struct zynqmp_cni_state* st, uint32_t lifesign);

static void zynqmp_cni_init(mackerel_addr_t reg_vbase) {
    zynqmp_cni_initialize(&device, reg_vbase);

	// zynqmp_cni_ctrl_wr(&device, zynqmp_cni_ctrl_bist_insert(0, 0x1));
	zynqmp_cni_ctrl_wr(&device, zynqmp_cni_ctrl_co_insert(0, 0x1));
}

static void rx_push_tx_msg(struct zynqmp_cni_devif_binding* b, uint32_t slot, const char* p_msg) {
	struct zynqmp_cni_state* state = (struct zynqmp_cni_state*)b->st;
	memcpy((void*)(state->msg_vbase + slot * CNI_MSG_SLOT_SIZE), p_msg, CNI_MSG_SLOT_SIZE);
}

static void rx_read_rx_msg_request(struct zynqmp_cni_devif_binding* b, uint32_t slot) {
	struct zynqmp_cni_state* state = (struct zynqmp_cni_state*)b->st;
	memcpy(msg, (void*)(state->msg_vbase + slot * CNI_MSG_SLOT_SIZE), 256);
	tx_read_rx_msg_response(state);
}

static void rx_update_host_lifesign(struct zynqmp_cni_devif_binding* b, uint32_t lifesign) {
    zynqmp_cni_hlife_wr(&device, lifesign);
}

static void rx_check_controller_lifesign_request(struct zynqmp_cni_devif_binding* b) {
    struct zynqmp_cni_state* state = (struct zynqmp_cni_state*)b->st;
    uint32_t lifesign = zynqmp_cni_clife_rd(&device);
    tx_check_controller_lifesign_response(state, lifesign);
}

static void tx_read_rx_msg_response_cb(void *a) {
    ZYNQMP_CNI_DEBUG("tx_read_rx_msg_response done.");
}

static void tx_read_rx_msg_response(struct zynqmp_cni_state* st) {
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void *))tx_read_rx_msg_response_cb, (void*)(st->binding));
    err = zynqmp_cni_devif_read_rx_msg_response__tx(st->binding, txcont, msg);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_CNI_DEBUG("tx_read_rx_msg_response again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void *))tx_read_rx_msg_response, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_CNI_DEBUG("tx_read_rx_msg_response register failed!");
            }
        }
        else {
            ZYNQMP_CNI_DEBUG("tx_read_rx_msg_response error\n");
        }
    }
}

static void tx_check_controller_lifesign_response_cb(void *a) {
    ZYNQMP_CNI_DEBUG("tx_check_controller_lifesign_response done.");
}

static void tx_check_controller_lifesign_response(struct zynqmp_cni_state* st, uint32_t lifesign) {
    errval_t err;
    struct event_closure txcont = MKCONT((void (*)(void *))tx_check_controller_lifesign_response_cb, (void*)(st->binding));
    err = zynqmp_cni_devif_check_controller_lifesign_response__tx(st->binding, txcont, lifesign);

    if (err_is_fail(err)) {
        if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
            ZYNQMP_CNI_DEBUG("tx_check_controller_lifesign_response again\n");
            struct waitset* ws = get_default_waitset();
            txcont = MKCONT((void (*)(void *))tx_check_controller_lifesign_response, (void*)(st->binding));
            err = st->binding->register_send(st->binding, ws, txcont);
            if (err_is_fail(err)) {
                // note that only one continuation may be registered at a time
                ZYNQMP_CNI_DEBUG("tx_check_controller_lifesign_response failed!");
            }
        }
        else {
            ZYNQMP_CNI_DEBUG("tx_check_controller_lifesign_response error\n");
        }
    }
}

static void export_devif_cb(void *st, errval_t err, iref_t iref)
{
	struct zynqmp_cni_state* state = (struct zynqmp_cni_state*) st;
	const char *suffix = "devif";
	char name[256];

	assert(err_is_ok(err));

	// Build label for interal management service
	sprintf(name, "%s_%s", state->service_name, suffix);
	ZYNQMP_CNI_DEBUG("registering nameservice %s.\n", name);
	err = nameservice_register(name, iref);
	assert(err_is_ok(err));
	state->initialized = true;
}

static errval_t connect_devif_cb(void *st, struct zynqmp_cni_devif_binding *b)
{
        struct zynqmp_cni_state *state = (struct zynqmp_cni_state*)st;
	state->binding = b;
	b->rx_vtbl.push_tx_msg = rx_push_tx_msg;
	b->rx_vtbl.read_rx_msg_request = rx_read_rx_msg_request;
    b->rx_vtbl.update_host_lifesign = rx_update_host_lifesign;
    b->rx_vtbl.check_controller_lifesign_request = rx_check_controller_lifesign_request;
	b->st = state;
	return SYS_ERR_OK;
}

static void zynqmp_cni_init_mngif(struct zynqmp_cni_state *st)
{
	errval_t err;
	err = zynqmp_cni_devif_export(st, export_devif_cb, connect_devif_cb,
		get_default_waitset(), 1);
	assert(err_is_ok(err));
}

static errval_t init(struct bfdriver_instance* bfi, const char* name, uint64_t
        flags, struct capref* caps, size_t caps_len, char** args, size_t
        args_len, iref_t* dev) {

    struct zynqmp_cni_state *st;
    void *va;
    st = (struct zynqmp_cni_state*)malloc(sizeof(struct zynqmp_cni_state));
    st->service_name = "zynqmp_cni";
    st->initialized = false;
	st->controller_ready = false;

    errval_t err;
    lvaddr_t reg_vbase;
	struct capref frame;
    ZYNQMP_CNI_DEBUG("Map device capability.\n");
    err = map_device_cap(caps[0], &reg_vbase);
    assert(err_is_ok(err) && reg_vbase);
	err = init_ram_caps_manager();
	assert(err);
	err = get_ram_cap(SHARED_REGION_CNI_MSG_BASE, SHARED_REGION_CNI_MSG_SIZE, &frame);
	vspace_map_one_frame_attr(&va, SHARED_REGION_CNI_MSG_SIZE, frame, VREGION_FLAGS_READ_WRITE_NOCACHE, NULL, NULL);
    st->msg_vbase = (lvaddr_t)va;
    zynqmp_cni_init((mackerel_addr_t)reg_vbase);

    //Enable interrupt
	zynqmp_cni_inten_wr(&device, 0xffffffff);

    /* For use with the net_queue_manager */

    ZYNQMP_CNI_DEBUG("Init management interface.\n");
    zynqmp_cni_init_mngif(st);
    ZYNQMP_CNI_DEBUG("Management interface initialized.\n");
    
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
DEFINE_MODULE(zynqmp_cni_module, init, attach, detach, set_sleep_level, destroy);
