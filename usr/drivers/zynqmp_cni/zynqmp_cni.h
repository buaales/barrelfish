#ifndef __CNI_CONTROLLER_H__
#define __CNI_CONTROLLER_H__

/*
#define CNI_INT_BR_MASK 0x00000001
#define CNI_INT_PE_MASK 0x00000002
#define CNI_INT_HE_MASK 0x00000004
#define CNI_INT_CR_MASK 0x00000008
#define CNI_INT_CV_MASK 0x00000100
#define CNI_INT_ML_MASK 0x00000200
#define CNI_INT_UI_MASK 0x00000400
#define CNI_INT_TI_MASK 0x00010000
 */

#define CNI_MSG_STAT_SEND_EN_BIT	0x00000001
#define CNI_MSG_STAT_RECV_EN_BIT	0x00000002

#define CNI_MSG_SLOT_SIZE 256

#define SHARED_REGION_BASE 0x70000000
#define SHARED_REGION_CNI_MSG_OFFSET 0x1000
#define SHARED_REGION_CNI_MSG_BASE (SHARED_REGION_BASE + SHARED_REGION_CNI_MSG_OFFSET)

#define SHARED_REGION_CNI_MSG_SIZE (0xe000 - SHARED_REGION_CNI_MSG_OFFSET)

struct zynqmp_cni_state {
	bool initialized;
	bool controller_ready;
	lvaddr_t msg_vbase;

	/* For use with the net_queue_manager */
	char *service_name;
	struct zynqmp_cni_devif_binding *binding;
};

#endif
