#ifndef __TTPC_H__
#define __TTPC_H__

#include <barrelfish/barrelfish.h>
struct ttpc_state {
	const char* p_cni_msg;
	uint32_t controller_lifesign;

	struct zynqmp_cni_devif_binding *binding;
};

void ttpc_send_msg(uint32_t slot, char* src);

void ttpc_read_msg(uint32_t slot, char* dest);

void ttpc_update_host_lifesign(uint32_t lifesign);

uint32_t ttpc_check_controller_lifesign(void);

errval_t ttpc_init(void);

#endif
