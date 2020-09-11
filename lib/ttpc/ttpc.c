
#include <barrelfish/barrelfish.h>
#include <if/zynqmp_cni_devif_defs.h>
#include <barrelfish/nameservice_client.h>

#include "ttpc.h"

static bool flag;
static struct ttpc_state *ttpc_state;

static void rx_read_msg_response(struct zynqmp_cni_devif_binding *b, char *p_msg)
{
	struct ttpc_state *state = (struct ttpc_state*)b->st;
	state->p_cni_msg = p_msg;
	flag = 1;
}

static void rx_check_controller_lifesign_response(struct zynqmp_cni_devif_binding* b, uint32_t lifesign) {
	struct ttpc_state *state = (struct ttpc_state*)b->st;
	state->controller_lifesign = lifesign;
	flag = 1;
}

static void bind_cb(void *st, errval_t err, struct zynqmp_cni_devif_binding *b)
{

	assert(err_is_ok(err));
	struct ttpc_state *state = st;

	state->binding = b;
	b->st = state;
	b->rx_vtbl.read_msg_response = rx_read_msg_response;
	b->rx_vtbl.check_controller_lifesign = rx_check_controller_lifesign_response
}

static void tx_push_tx_msg_cb(void *a) {
	debug_printf("send message done.");
}

static void tx_push_tx_msg(struct ttpc_state *st, uint32_t slot, char *p_msg) {
	struct event_closure txcont = MKCONT(tx_push_tx_msg_cb, st);
	err = zynqmp_cni_devif_push_tx_msg__tx(st->b, txcont, p_msg);

	if (err_is_fail(err)) {
		if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
			debug_printf("tx_push_tx_msg again\n");
			struct waitset* ws = get_default_waitset();
			txcont = MKCONT(tx_push_tx_msg, st);
			err = st->b->register_send(st->b, ws, txcont);
			if (err_is_fail(err)) {
				// note that only one continuation may be registered at a time
				debug_printf("tx_push_tx_msg register failed!");
			}
		}
		else {
			debug_printf("tx_push_tx_msg error\n");
		}
	}
}

static void tx_read_rx_msg_request_cb(void* a) {
	debug_printf("read message request done.");
}

static void tx_read_rx_msg_request(struct ttpc_state *st, uint32_t slot) {
	struct event_closure txcont = MKCONT(tx_read_rx_msg_request_cb, st);
	err = zynqmp_cni_devif_read_rx_msg_request__tx(st->b, txcont, slot);

	if (err_is_fail(err)) {
		if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
			debug_printf("tx_read_rx_msg_request again\n");
			struct waitset* ws = get_default_waitset();
			txcont = MKCONT(tx_read_rx_msg_request, st);
			err = st->b->register_send(st->b, ws, txcont);
			if (err_is_fail(err)) {
				// note that only one continuation may be registered at a time
				debug_printf("tx_read_rx_msg_request register failed!");
			}
		}
		else {
			debug_printf("tx_read_rx_msg_request error\n");
		}
	}
}

static void tx_update_host_lifesign_cb(void* a) {
	debug_printf("tx_update_host_lifesign done.");
}

static void tx_update_host_lifesign(struct ttpc_state* st, uint32_t lifesign) {
	struct event_closure txcont = MKCONT(tx_update_host_lifesign_cb, st);
	err = zynqmp_cni_devif_update_host_lifesign__tx(st->b, txcont, slot);
	if (err_is_fail(err)) {
		if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
			debug_printf("tx_update_host_lifesign again\n");
			struct waitset* ws = get_default_waitset();
			txcont = MKCONT(tx_update_host_lifesign, st);
			err = st->b->register_send(st->b, ws, txcont);
			if (err_is_fail(err)) {
				// note that only one continuation may be registered at a time
				debug_printf("tx_update_host_lifesign register failed!");
			}
		}
		else {
			debug_printf("tx_update_host_lifesign error\n");
		}
	}
}

static void tx_check_controller_lifesign_request_cb(void *a) {
	debug_printf("tx_check_controller_lifesign_request done.");
}

static void tx_check_controller_lifesign_request(struct ttpc_state *st) {
	struct event_closure txcont = MKCONT(tx_check_controller_lifesign_request_cb, st);
	err = zynqmp_cni_devif_check_controller_lifesign_request__tx(st->b, txcont);

	if (err_is_fail(err)) {
		if (err_no(err) == FLOUNDER_ERR_TX_BUSY) {
			debug_printf("tx_check_controller_lifesign_request again\n");
			struct waitset* ws = get_default_waitset();
			txcont = MKCONT(tx_check_controller_lifesign_request, st);
			err = st->b->register_send(st->b, ws, txcont);
			if (err_is_fail(err)) {
				// note that only one continuation may be registered at a time
				debug_printf("tx_check_controller_lifesign_request register failed!");
			}
		}
		else {
			debug_printf("tx_check_controller_lifesign_request error\n");
		}
	}
}

void ttpc_send_msg(uint32_t slot, char *src) {
	tx_push_tx_msg(ttpc_state, slot, src, len);
}

void ttpc_read_msg(uint32_t slot, char *dest) {
	flag = 0;
	tx_read_rx_msg_request(ttpc_state, slot);
	while (!flag);
	memcpy(dest, ttpc_state->p_cni_msg, 256);
}

void ttpc_update_host_lifesign(uint32_t lifesign) {
	tx_update_host_lifesign(ttpc_state, lifesign);
}

bool ttpc_check_controller_lifesign() {
	flag = 0;
	tx_check_controller_lifesign_request(ttpc_state);
	while (!flag);
	return ttpc_state->controller_lifesign;
}

void ttpc_init()
{
	char service[128] = "zynqmp_cni_devif";

	iref_t iref;
	ttpc_state = malloc(sizeof(struct ttpc_state));

	err = nameservice_blocking_lookup(service, &iref);
	if (err_is_fail(err)) {
		return err;
	}

	err = zynqmp_cni_devif_bind(iref, bind_cb, ttpc_state, get_default_waitset(), 1);
	if (err_is_fail(err)) {
		return err;
	}
}