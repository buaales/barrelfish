#ifndef __ZYNQMP_GEM_H__
#define __ZYNQMP_GEM_H__

#define ZYNQMP_GEM_BUFSIZE 1024
#define ZYNQMP_GEM_FRAMESIZE 256
#define ZYNQMP_GEM_N_BUFS 8

#define PRESET_DATA_BASE 0x60000000
#define PRESET_DATA_MAC_OFFSET 0

#define SHARED_REGION_BASE 0x70000000
#define SHARED_REGION_VARIABLES_OFFSET 0
#define SHARED_REGION_ETH_TX_FRAMES_OFFSET 0x10000
#define SHARED_REGION_ETH_RX_FRAMES_OFFSET 0x20000
#define SHARED_REGION_VARIABLES_BASE (SHARED_REGION_BASE + SHARED_REGION_VARIABLES_OFFSET)
#define SHARED_REGION_ETH_TX_BASE (SHARED_REGION_BASE + SHARED_REGION_ETH_TX_FRAMES_OFFSET)
#define SHARED_REGION_ETH_RX_BASE (SHARED_REGION_BASE + SHARED_REGION_ETH_RX_FRAMES_OFFSET)

struct zynqmp_gem_state {
    bool initialized;

    lvaddr_t vars_base;
    struct capref vars_cap;

    /* For use with the net_queue_manager */
    char *service_name;
    zynqmp_gem_devif_binding* binding;
};

void poll(void);

#endif //ZYNQ_GEM_H
