struct ttpc_state {
	char* p_cni_msg;
	uint32_t controller_lifesign;

	struct zynqmp_cni_devif_binding *binding;
};

void ttpc_send_msg(uint32_t slot, char* src);

void ttpc_read_msg(uint32_t slot, char* dest);

void ttpc_update_host_lifesign(uint32_t lifesign);

uint32_t ttpc_check_controller_lifesign();

void ttpc_init();