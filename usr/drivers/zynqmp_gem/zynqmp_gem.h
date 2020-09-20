#ifndef __ZYNQMP_GEM_H__
#define __ZYNQMP_GEM_H__

#include <dev/zynqmp_gem_dev.h>
#include <memory_maps.h>

#define ZYNQMP_GEM_BUFSIZE 1024
#define ZYNQMP_GEM_FRAMESIZE 256
#define ZYNQMP_GEM_N_BUFS 8

struct zynqmp_gem_state {
    bool initialized;

    lvaddr_t vars_base;
    struct capref vars_cap;

    /* For use with the net_queue_manager */
    char *service_name;
    struct zynqmp_gem_devif_binding* binding;
};

void poll(void);

#endif //ZYNQ_GEM_H
