/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CPUINFO_H
#define CPUINFO_H

#define MAX_PSTATE	20U	/* max num of supported Px count */
#define MAX_CSTATE	8U	/* max num of supported Cx count */
/* We support MAX_CSTATE num of Cx, means have (MAX_CSTATE - 1) Cx entries,
 * i.e. supported Cx entry index range from 1 to MAX_CX_ENTRY.
 */
#define MAX_CX_ENTRY	(MAX_CSTATE - 1U)

/* CPUID feature words */
#define	FEAT_1_ECX		0U     /* CPUID[1].ECX */
#define	FEAT_1_EDX		1U     /* CPUID[1].EDX */
#define	FEAT_7_0_EBX		2U     /* CPUID[EAX=7,ECX=0].EBX */
#define	FEAT_7_0_ECX		3U     /* CPUID[EAX=7,ECX=0].ECX */
#define	FEAT_7_0_EDX		4U     /* CPUID[EAX=7,ECX=0].EDX */
#define	FEAT_8000_0001_ECX	5U     /* CPUID[8000_0001].ECX */
#define	FEAT_8000_0001_EDX	6U     /* CPUID[8000_0001].EDX */
#define	FEAT_8000_0007_EDX	7U     /* CPUID[8000_0007].EDX */
#define	FEAT_8000_0008_EBX	8U     /* CPUID[8000_0008].EBX */
#define	FEAT_D_0_EAX		9U     /* CPUID[D][0].EAX */
#define	FEAT_D_0_EDX		10U    /* CPUID[D][0].EDX */
#define	FEAT_D_1_EAX		11U    /* CPUID[D][1].EAX */
#define	FEAT_D_1_ECX		13U    /* CPUID[D][1].ECX */
#define	FEAT_D_1_EDX		14U    /* CPUID[D][1].EDX */
#define	FEATURE_WORDS		15U

/* Intel-defined CPU features, CPUID level 0x00000001 (ECX)*/
#define X86_FEATURE_SSE3	((FEAT_1_ECX << 5U) +  0U)
#define X86_FEATURE_PCLMUL	((FEAT_1_ECX << 5U) +  1U)
#define X86_FEATURE_DTES64	((FEAT_1_ECX << 5U) +  2U)
#define X86_FEATURE_MONITOR	((FEAT_1_ECX << 5U) +  3U)
#define X86_FEATURE_DS_CPL	((FEAT_1_ECX << 5U) +  4U)
#define X86_FEATURE_VMX		((FEAT_1_ECX << 5U) +  5U)
#define X86_FEATURE_SMX		((FEAT_1_ECX << 5U) +  6U)
#define X86_FEATURE_EST		((FEAT_1_ECX << 5U) +  7U)
#define X86_FEATURE_TM2		((FEAT_1_ECX << 5U) +  8U)
#define X86_FEATURE_SSSE3	((FEAT_1_ECX << 5U) +  9U)
#define X86_FEATURE_CID		((FEAT_1_ECX << 5U) + 10U)
#define X86_FEATURE_FMA		((FEAT_1_ECX << 5U) + 12U)
#define X86_FEATURE_CX16	((FEAT_1_ECX << 5U) + 13U)
#define X86_FEATURE_ETPRD	((FEAT_1_ECX << 5U) + 14U)
#define X86_FEATURE_PDCM	((FEAT_1_ECX << 5U) + 15U)
#define X86_FEATURE_PCID	((FEAT_1_ECX << 5U) + 17U)
#define X86_FEATURE_DCA		((FEAT_1_ECX << 5U) + 18U)
#define X86_FEATURE_SSE4_1	((FEAT_1_ECX << 5U) + 19U)
#define X86_FEATURE_SSE4_2	((FEAT_1_ECX << 5U) + 20U)
#define X86_FEATURE_X2APIC	((FEAT_1_ECX << 5U) + 21U)
#define X86_FEATURE_MOVBE	((FEAT_1_ECX << 5U) + 22U)
#define X86_FEATURE_POPCNT	((FEAT_1_ECX << 5U) + 23U)
#define X86_FEATURE_TSC_DEADLINE	((FEAT_1_ECX << 5U) + 24U)
#define X86_FEATURE_AES		((FEAT_1_ECX << 5U) + 25U)
#define X86_FEATURE_XSAVE	((FEAT_1_ECX << 5U) + 26U)
#define X86_FEATURE_OSXSAVE	((FEAT_1_ECX << 5U) + 27U)
#define X86_FEATURE_AVX		((FEAT_1_ECX << 5U) + 28U)
#define X86_FEATURE_RDRAND	((FEAT_1_ECX << 5U) + 30U)

/* Intel-defined CPU features, CPUID level 0x00000001 (EDX)*/
#define X86_FEATURE_FPU		((FEAT_1_EDX << 5U) +  0U)
#define X86_FEATURE_VME		((FEAT_1_EDX << 5U) +  1U)
#define X86_FEATURE_DE		((FEAT_1_EDX << 5U) +  2U)
#define X86_FEATURE_PSE		((FEAT_1_EDX << 5U) +  3U)
#define X86_FEATURE_TSC		((FEAT_1_EDX << 5U) +  4U)
#define X86_FEATURE_MSR		((FEAT_1_EDX << 5U) +  5U)
#define X86_FEATURE_PAE		((FEAT_1_EDX << 5U) +  6U)
#define X86_FEATURE_MCE		((FEAT_1_EDX << 5U) +  7U)
#define X86_FEATURE_CX8		((FEAT_1_EDX << 5U) +  8U)
#define X86_FEATURE_APIC	((FEAT_1_EDX << 5U) +  9U)
#define X86_FEATURE_SEP		((FEAT_1_EDX << 5U) + 11U)
#define X86_FEATURE_MTRR	((FEAT_1_EDX << 5U) + 12U)
#define X86_FEATURE_PGE		((FEAT_1_EDX << 5U) + 13U)
#define X86_FEATURE_MCA		((FEAT_1_EDX << 5U) + 14U)
#define X86_FEATURE_CMOV	((FEAT_1_EDX << 5U) + 15U)
#define X86_FEATURE_PAT		((FEAT_1_EDX << 5U) + 16U)
#define X86_FEATURE_PSE36	((FEAT_1_EDX << 5U) + 17U)
#define X86_FEATURE_PSN		((FEAT_1_EDX << 5U) + 18U)
#define X86_FEATURE_CLF		((FEAT_1_EDX << 5U) + 19U)
#define X86_FEATURE_DTES	((FEAT_1_EDX << 5U) + 21U)
#define X86_FEATURE_ACPI	((FEAT_1_EDX << 5U) + 22U)
#define X86_FEATURE_MMX		((FEAT_1_EDX << 5U) + 23U)
#define X86_FEATURE_FXSR	((FEAT_1_EDX << 5U) + 24U)
#define X86_FEATURE_SSE		((FEAT_1_EDX << 5U) + 25U)
#define X86_FEATURE_SSE2	((FEAT_1_EDX << 5U) + 26U)
#define X86_FEATURE_SS		((FEAT_1_EDX << 5U) + 27U)
#define X86_FEATURE_HTT		((FEAT_1_EDX << 5U) + 28U)
#define X86_FEATURE_TM1		((FEAT_1_EDX << 5U) + 29U)
#define X86_FEATURE_IA64	((FEAT_1_EDX << 5U) + 30U)
#define X86_FEATURE_PBE		((FEAT_1_EDX << 5U) + 31U)

/* Intel-defined CPU features, CPUID level 0x00000007 (EBX)*/
#define X86_FEATURE_TSC_ADJ	((FEAT_7_0_EBX << 5U) +  1U)
#define X86_FEATURE_SGX		((FEAT_7_0_EBX << 5U) +  2U)
#define X86_FEATURE_SMEP	((FEAT_7_0_EBX << 5U) +  7U)
#define X86_FEATURE_ERMS	((FEAT_7_0_EBX << 5U) +  9U)
#define X86_FEATURE_INVPCID	((FEAT_7_0_EBX << 5U) + 10U)
#define X86_FEATURE_RDT_A	((FEAT_7_0_EBX << 5U) + 15U)
#define X86_FEATURE_SMAP	((FEAT_7_0_EBX << 5U) + 20U)
#define X86_FEATURE_CLFLUSHOPT	((FEAT_7_0_EBX << 5U) + 23U)

/* Intel-defined CPU features, CPUID level 0x00000007 (ECX)*/
#define X86_FEATURE_KEYLOCKER	((FEAT_7_0_ECX << 5U) + 23U)

/* Intel-defined CPU features, CPUID level 0x00000007 (EDX)*/
#define X86_FEATURE_MDS_CLEAR	((FEAT_7_0_EDX << 5U) + 10U)
#define X86_FEATURE_IBRS_IBPB	((FEAT_7_0_EDX << 5U) + 26U)
#define X86_FEATURE_STIBP	((FEAT_7_0_EDX << 5U) + 27U)
#define X86_FEATURE_L1D_FLUSH	((FEAT_7_0_EDX << 5U) + 28U)
#define X86_FEATURE_ARCH_CAP	((FEAT_7_0_EDX << 5U) + 29U)
#define X86_FEATURE_CORE_CAP	((FEAT_7_0_EDX << 5U) + 30U)
#define X86_FEATURE_SSBD	((FEAT_7_0_EDX << 5U) + 31U)

/* Intel-defined CPU features, CPUID level 0x80000001 (EDX)*/
#define X86_FEATURE_NX		((FEAT_8000_0001_EDX << 5U) + 20U)
#define X86_FEATURE_PAGE1GB	((FEAT_8000_0001_EDX << 5U) + 26U)
#define X86_FEATURE_LM		((FEAT_8000_0001_EDX << 5U) + 29U)

/* Intel-defined CPU features, CPUID level 0x80000007 (EDX)*/
#define X86_FEATURE_INVA_TSC	((FEAT_8000_0007_EDX << 5U) + 8U)

/* Intel-defined CPU features, CPUID level 0x0000000D, sub 0x1 */
#define X86_FEATURE_COMPACTION_EXT	((FEAT_D_1_EAX << 5U) + 1U)
#define X86_FEATURE_XSAVES		((FEAT_D_1_EAX << 5U) + 3U)

bool has_monitor_cap(void);
uint8_t pcpu_family_id(void);
uint8_t pcpu_model_id(void);
char *pcpu_model_name(void);
bool is_apicv_advanced_feature_supported(void);
uint8_t pcpu_physaddr_bits(void);
uint8_t pcpu_virtaddr_bits(void);
uint32_t pcpu_cpuid_level(void);
bool pcpu_set_cap(uint32_t bit);
bool pcpu_has_cap(uint32_t bit);
bool pcpu_has_vmx_ept_cap(uint32_t bit_mask);
bool pcpu_has_vmx_vpid_cap(uint32_t bit_mask);
bool has_core_cap(uint32_t bit_mask);
bool is_ac_enabled(void);
void init_pcpu_capabilities(void);
int32_t detect_hardware_support(void);

#endif /* CPUINFO_H */
