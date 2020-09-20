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

    struct capref ram;
    assert(ram_alloc_state->ram_alloc_func != NULL);
    errval_t err = ram_alloc_state->
        ram_alloc_func(&ram, size_bits, address,
            address + (1UL << size_bits));
    if (!err_is_ok(err)) {
        return err;
    }

    struct capref frame;
    err = slot_alloc(&frame);
    if (!err_is_ok(err)) {
        return err;
    }
    err = cap_retype(frame, ram, 0, ObjType_Frame, (1UL << size_bits), 1);
    if (!err_is_ok(err)) {
        return err;
    }
    *ret = frame;
    return SYS_ERR_OK;
}