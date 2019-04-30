/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VM_CONFIGURATIONS_H
#define VM_CONFIGURATIONS_H

#include <pci_devices.h>

/* Bits mask of guest flags that can be programmed by device model. Other bits are set by hypervisor only */
#define DM_OWNED_GUEST_FLAG_MASK	0UL

#define CONFIG_MAX_VM_NUM	3U

/* The VM CONFIGs like:
 *	VMX_CONFIG_PCPU_BITMAP
 *	VMX_CONFIG_MEM_START_HPA
 *	VMX_CONFIG_MEM_SIZE
 * might be different on your board, please modify them per your needs.
 */

#define SAFE_CONFIG_PCPU_BITMAP			(PLUG_CPU(0))
#define SAFE_CONFIG_NUM_CPUS			1U
#define SAFE_CONFIG_MEM_START_HPA		0x100000000UL
#define SAFE_CONFIG_MEM_SIZE			0x20000000UL

/* VM pass-through devices assign policy:
 */
#define SAFE_CONFIG_PCI_PTDEV_NUM		0U

#endif /* VM_CONFIGURATIONS_H */
