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

#include "zynqmp_cni_debug.h"
#include "zynqmp_cni.h"

static zynqmp_cni_t device;
static uint32_t lifesign;

static void zynqmp_cni_init(mackerel_addr_t vbase) {
	lifesign = 1;
    zynqmp_cni_initialize(&device, vbase);

	zynqmp_cni_hlife_wr(&device, lifesign);

	zynqmp_cni_ctrl_bist_wr(&device, 0x1);
	zynqmp_cni_ctrl_co_wr(&device, 0x1);
}


static void on_interrupt(struct zynqmp_cni_devif_binding *b)
{
    ZYNQMP_CNI_DEBUG("on interrupt called.\n");
    uint32_t intstat, txstat, rxstat;
    intstat = zynqmp_gem_intstat_rd(&device);
    ZYNQMP_CNI_DEBUG("intstat:%x.\n", intstat);
    if (intstat & ZYNQMP_CNI_INT_CR_MASK) {
		(struct zynqmp_cni_state *)(b->st)->controller_ready = true;
    }
    zynqmp_gem_intstat_wr(&device, 0xffffffff);
}

static void on_send(struct zynqmp_cni_devif_binding *b, zynqmp_cni_tx_msg_t msg)
{
	//FIXME: send stuff.
	ZYNQMP_CNI_DEBUG("on send called.\n");
}

static void export_devif_cb(void *st, errval_t err, iref_t iref)
{
	struct zynqmp_cni_state* state = (struct zynqmp_cni_state*) st;
	const char *suffix = "devif";
	char name[256];

	assert(err_is_ok(err));

	// Build label for interal management service
	sprintf(name, "%s_%s", state->service_name, suffix);
	ZYNQMP_GEM_DEBUG("registering nameservice %s.\n", name);
	err = nameservice_register(name, iref);
	assert(err_is_ok(err));
	state->initialized = true;
}

static errval_t connect_devif_cb(void *st, struct zynqmp_cni_devif_binding *b)
{
	st->binding = b;
	//b->rpc_rx_vtbl.create_queue_call = on_create_queue;
	//b->rx_vtbl.transmit_start = on_transmit_start;
	b->rx_vtbl.interrupt = on_interrupt;
	b->st = st;
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
    st = (struct zynqmp_cni_state*)malloc(sizeof(struct zynqmp_cni_state));
    st->service_name = "zynqmp_cni";
	st->initialized = false;
	st->controller_ready = false;

    errval_t err;
    lvaddr_t vbase;
    ZYNQMP_CNI_DEBUG("Map device capability.\n");
    err = map_device_cap(caps[0], &vbase);
    assert(err_is_ok(err) && vbase);

    zynqmp_cni_init((mackerel_addr_t)vbase);

    //Enable interrupt
	zynqmp_cni_inten_wr(&device, 0xffffffff);

    /* For use with the net_queue_manager */

    ZYNQMP_CNI_DEBUG("Init management interface.\n");
    zynqmp_gem_init_mngif(st);
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
