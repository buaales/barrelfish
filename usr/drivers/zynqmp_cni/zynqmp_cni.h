#ifndef __CNI_CONTROLLER_H__
#define __CNI_CONTROLLER_H__

#define CNI_INT_BR_MASK 0x00000001
#define CNI_INT_PE_MASK 0x00000002
#define CNI_INT_HE_MASK 0x00000004
#define CNI_INT_CR_MASK 0x00000008
#define CNI_INT_CV_MASK 0x00000100
#define CNI_INT_ML_MASK 0x00000200
#define CNI_INT_UI_MASK 0x00000400
#define CNI_INT_TI_MASK 0x00010000

#define CNI_SEND_SLOTSIZE_MASK			0x0000fff0
#define CNI_SEND_NSLOTS_MASK			0x0000000f
#define CNI_SEND_EN_BIT					0x00000001
#define CNI_SEND_BLOCK_PERIOD_MASK		0xffff0000

#define CNI_RECV_SLOTSIZE_MASK			0x0000fff0
#define CNI_RECV_NSLOTS_MASK			0x0000000f
#define CNI_RECV_EN_BIT					0x00000001
#define CNI_RECV_BLOCK_PERIOD_MASK		0xffff0000

#define ZYNQMP_CNI_IRQ

struct zynqmp_cni_state {
	bool initialized;
	bool controller_ready;

	/* For use with the net_queue_manager */
	char *service_name;
	struct zynqmp_cni_devif_binding binding;
};

#endif
