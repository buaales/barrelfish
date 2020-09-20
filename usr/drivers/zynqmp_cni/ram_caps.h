/*
 * Copyright (c) 2013, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef DEVICE_CAPS_H
#define DEVICE_CAPS_H

#include <barrelfish/core_state.h>
#include <barrelfish/barrelfish.h>

errval_t get_ram_cap(lpaddr_t address, uint8_t size_bits, struct capref* retcap);
 
#endif // DEVICE_CAPS_H
