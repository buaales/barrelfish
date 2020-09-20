#include "ram_caps.h"

/**
 * \brief Constructs a capability to a physical device range
 *
 * \param[in] address Physical address of register you want to map.
 * \param[in] size Size of register space to map
 * \param[out] frame Capability to device range
 *
 * \retval SYS_ERR_OK frame is a valid capability for the requested range.
 **/
errval_t get_ram_cap(lpaddr_t address, uint8_t size_bits, struct capref* ret) 
{
    struct ram_alloc_state* ram_alloc_state = get_ram_alloc_state();
    assert(ram_alloc_state->ram_alloc_func != NULL);
    errval_t err = ram_alloc_state->
        ram_alloc_func(ret, size_bits, address,
            address + 1 << size_bits);
    return err;
}