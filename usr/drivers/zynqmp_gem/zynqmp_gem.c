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
#include "zynqmp_gem_desc.h"
#include "zynqmp_gem.h"

static zynqmp_gem_t device;

static uint64_t zynqmp_gem_mac;

static void zynqmp_gem_hardware_init(mackerel_addr_t vbase) {
    int i;
    uint64_t mac_prefix, mac_suffix;
    zynqmp_gem_initialize(&device, vbase);

    //Disable all interrupts(This reg is reset as disabled, so no need to do this I guess)
    //zynqmp_gem_intdis_wr(&device, 0x7FFFEFF);

    //Clear the network control register
    zynqmp_gem_netctl_wr(&device, 0);

    //Clear the statistics registers
    zynqmp_gem_netctl_casr_wrf(&device, 0x1);
    //Clear the status registers
    zynqmp_gem_rxstat_wr(&device, 0x0F);
    zynqmp_gem_txstat_wr(&device, 0xFF);
    //Clear the buffer queues
    zynqmp_gem_rxqptr_wr(&device, 0);
    zynqmp_gem_txqptr_wr(&device, 0);
    zynqmp_gem_rxq1ptr_wr(&device, 0);
    zynqmp_gem_txq1ptr_wr(&device, 0);

    //Clear mac registers
	for (i = 0; i < 4; i++) {
	    zynqmp_gem_spcaddbt_wr(&device, i, 0);
	    zynqmp_gem_spcaddtp_wr(&device, i, 0);
		// Do not use MATCHx register
	    zynqmp_gem_spctype_wr(&device, i, 0);
	}

    zynqmp_gem_phymng_wr(&device, 0);

    zynqmp_gem_hashbt_wr(&device, 0); 
    zynqmp_gem_hashtp_wr(&device, 0);

    zynqmp_gem_netcfg_ehdr_wrf(&device, 0x1);
    //zynqmp_gem_netcfg_caf_wrf(&device, 0x1);
    zynqmp_gem_netcfg_fd_wrf(&device, 0x1);
    zynqmp_gem_netcfg_fr_wrf(&device, 0x1);
    zynqmp_gem_netcfg_mcd_wrf(&device, 0x3);
    zynqmp_gem_netcfg_gme_wrf(&device, 0x1);
    zynqmp_gem_netcfg_spd_wrf(&device, 0x1);

    //FIXME: use burned-in MAC address instead of random one if possible
    mac_prefix = ZYNQMP_GEM_MAC_PREFIX;
    srand((int)time(0));
    mac_suffix = rand() % 0x1000000;
    zynqmp_gem_mac = mac_prefix << 24 | mac_suffix;
    zynqmp_gem_spcaddbt_wr(&device, 0, ((mac_prefix & 0xff) << 24) | mac_suffix);
    zynqmp_gem_spcaddtp_addr_wrf(&device, 0, (mac_prefix & 0xffff00) >> 8);

    zynqmp_gem_dmacfg_rbs_wrf(&device, 0x18);
    zynqmp_gem_dmacfg_rps_wrf(&device, 0x3);
    zynqmp_gem_dmacfg_tps_wrf(&device, 0x0);
    zynqmp_gem_dmacfg_tpte_wrf(&device, 0x1);
    zynqmp_gem_dmacfg_esp_wrf(&device, 0);
    zynqmp_gem_dmacfg_esm_wrf(&device, 0);
    zynqmp_gem_dmacfg_abl_wrf(&device, 0x1);

    //FIXME: PHY configuration to be implemented if needed.
}

static void rx_create_queue_call(struct zynqmp_gem_devif_binding *b,
        struct capref rx, struct capref dummy_rx,
        struct capref tx, struct capref dummy_tx) {
    struct frame_identity frameid = { .base = 0, .bytes = 0 };

    ZYNQMP_GEM_DEBUG("on create queue called.\n");

    err = invoke_frame_identify(rx, &frameid);
    assert(err_is_ok(err));
    zynqmp_gem_rxqptr_wr(&device, frameid.base);

    err = invoke_frame_identify(dummy_rx, &frameid);
    assert(err_is_ok(err));
    zynqmp_gem_rxq1ptr_wr(&device, frameid.base);

    err = invoke_frame_identify(tx, &frameid);
    assert(err_is_ok(err));
    zynqmp_gem_txqptr_wr(&device, frameid.base);
    ZYNQMP_GEM_DEBUG("original txqptr:%x.\n", frameid.base);

    err = invoke_frame_identify(dummy_tx, &frameid);
    assert(err_is_ok(err));
    zynqmp_gem_txq1ptr_wr(&device, frameid.base);
    tx_create_queue_response(b->st, zynqmp_gem_mac);

    //Enable controller
    zynqmp_gem_netctl_mpe_wrf(&device, 0x1);
    zynqmp_gem_netctl_er_wrf(&device, 0x1);
    zynqmp_gem_netctl_et_wrf(&device, 0x1);

    return SYS_ERR_OK;
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

static void on_transmit_start(struct zynqmp_gem_devif_binding *b) {
    ZYNQMP_GEM_DEBUG("txstat:%x.\n", zynqmp_gem_txstat_rd(&device));
    ZYNQMP_GEM_DEBUG("start transmission. qptr = %x\n", zynqmp_gem_txqptr_rd(&device));
    zynqmp_gem_netctl_tsp_wrf(&device, 0x1);
}
/*
static void zynqmp_gem_dump_stats(void) {
    int i = 0;
    ZYNQMP_GEM_DEBUG("dump stats#################################");
    for (i = 0; i < 45; i++) {
        if (i % 4 == 0) {
            printf("\noffset:%x\tcontents:", i * 4);
        }
        printf("%x\t", zynqmp_gem_stats_rd(&device, i));
    }
    ZYNQMP_GEM_DEBUG("\nstats dumped#################################\n");
}
 */

static void on_interrupt(struct zynqmp_gem_devif_binding *b)
{
    ZYNQMP_GEM_DEBUG("on interrupt called.\n");
    uint32_t intstat, txstat, rxstat;
    intstat = zynqmp_gem_intstat_rd(&device);
    ZYNQMP_GEM_DEBUG("intstat:%x.\n", intstat);
    if (intstat & ZYNQMP_GEM_INT_TC_MASK) {
        txstat = zynqmp_gem_txstat_rd(&device);
        //FIXME:do something with txstat.
        uint32_t qptr = zynqmp_gem_txqptr_rd(&device);
        ZYNQMP_GEM_DEBUG("Tx completed, txstat:%x, qptr:%x.\n", txstat, qptr);
        zynqmp_gem_txstat_wr(&device, 0xffffffff);
    }
    if (intstat & ZYNQMP_GEM_INT_RC_MASK) {
        rxstat = zynqmp_gem_rxstat_rd(&device);
        //FIXME:do something with rxstat.
        ZYNQMP_GEM_DEBUG("Rx completed, rxstat:%x.\n", rxstat);
        //zynqmp_gem_dump_stats();
        zynqmp_gem_rxstat_wr(&device, 0xffffffff);
    }
    if (intstat & ZYNQMP_GEM_INT_TXUSED_MASK) {
        ZYNQMP_GEM_DEBUG("Tx used bit read.\n");
        //zynqmp_gem_netctl_tsp_wrf(&device, 0x1);
    } 
    zynqmp_gem_intstat_wr(&device, 0xffffffff);
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
    b->rx_vtbl.create_queue_call = on_create_queue;
    b->rx_vtbl.transmit_start = on_transmit_start;
    b->rx_vtbl.interrupt = on_interrupt;
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

    struct zynqmp_gem_state *st;
    st = (struct zynqmp_gem_state*)malloc(sizeof(struct zynqmp_gem_state));
    st->initialized = false;
    st->service_name = "zynqmp_gem";

    errval_t err;
    lvaddr_t vbase;
    ZYNQMP_GEM_DEBUG("Map device capability.\n");
    err = map_device_cap(caps[0], &vbase);
    assert(err_is_ok(err) && vbase);
    ZYNQMP_GEM_DEBUG("Device capability mapped.\n");

    ZYNQMP_GEM_DEBUG("Init hardware.\n");
    zynqmp_gem_hardware_init((mackerel_addr_t)vbase);
    ZYNQMP_GEM_DEBUG("Hardware initialized.\n");

    //Enable interrupt
    zynqmp_gem_inten_wr(&device, 0xFFFFFFFF);

    /* For use with the net_queue_manager */

    ZYNQMP_GEM_DEBUG("Init management interface.\n");
    zynqmp_gem_init_mngif(st);
    ZYNQMP_GEM_DEBUG("Management interface initialized.\n");
    
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
