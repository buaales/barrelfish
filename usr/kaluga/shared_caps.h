/*
 * Copyright (c) 2013, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef SHARED_CAPS_H
#define SHARED_CAPS_H
 
errval_t get_shared_cap(genpaddr_t address, size_t size, struct capref* frame);
errval_t init_shared_caps_manager(void);
 
#endif // SHARED_CAPS_H
