#ifndef __ZYNQMP_GEM_H__
#define __ZYNQMP_GEM_H__

#include <dev/zynqmp_gem_dev.h>
#include <memory_maps.h>

#define ZYNQMP_GEM_BUFSIZE 1024
#define ZYNQMP_GEM_FRAMESIZE 256
#define ZYNQMP_GEM_N_BUFS 1

struct zynqmp_gem_state {
    bool initialized;

    struct capref shared_vars_region;
    struct capref shared_tx_region;
    struct capref shared_rx_region;
    lvaddr_t shared_vars_base;
    uint64_t mac;


    /* For use with the net_queue_manager */
    char *service_name;
    struct zynqmp_gem_devif_binding* binding;
};

void poll(void);

#endif //ZYNQ_GEM_H
