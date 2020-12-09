/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_PTINTR_H
#define ARCH_PTINTR_H

#include <ptintr.h>

int32_t ptintr_add_intx_arch(struct acrn_vm *vm, union source_id *virt_sid);
int32_t ptintr_remap_msix_arch(struct ptintr *intr, struct ptintr_remap_msix *args);
int32_t ptintr_remap_intx_arch(struct ptintr *intr, struct ptintr_remap_intx *args);
void ptintr_remove_msix_arch(struct ptintr *intr);
void ptintr_remove_intx_arch(struct ptintr *intr);
void ptintr_init_arch(struct ptintr *(*find)(uint32_t, const union source_id *,
			const struct acrn_vm *));
#endif /* ARCH_PTINTR_H */
