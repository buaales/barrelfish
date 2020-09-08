
#include <barrelfish/barrelfish.h>
#include <if/zynqmp_cni_devif_defs.h>
#include <barrelfish/nameservice_client.h>

#include "ttpc.h"

static void on_receive(struct zynqmp_cni_devif_binding *b, const zynqmp_cni_rx_msg_t *msgs, size_t n)
{
	//FIXME: receive stuff.
}

static void bind_cb(void *st, errval_t err, struct zynqmp_cni_devif_binding *b)
{

	assert(err_is_ok(err));
	struct ttpc_state *state = st;

	state->binding = b;
	b->st = state;
	b->rx_vtbl.receive = on_receive;
}

static void ttpc_init()
{
	char service[128] = "zynqmp_cni_devif";

	iref_t iref;

	err = nameservice_blocking_lookup(service, &iref);
	if (err_is_fail(err)) {
		return err;
	}

	err = zynqmp_cni_devif_bind(iref, bind_cb, q, get_default_waitset(), 1);
	if (err_is_fail(err)) {
		return err;
	}
}