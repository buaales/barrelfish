struct ttpc_state {
	char* p_cni_msg;
	uint32_t controller_lifesign;

	struct zynqmp_cni_devif_binding binding;
};