/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <cpu.h>
#include <pgtable.h>
#include <rtl.h>
#include <mmu.h>
#include <sprintf.h>
#include <ept.h>
#include <logmsg.h>
#include <multiboot.h>
#include <crypto_api.h>
#include <seed.h>

#define BOOTLOADER_SBL  0U
#define BOOTLOADER_ABL  1U
#define BOOTLOADER_INVD (~0U)

struct seed_argument {
	const char *str;
	uint32_t bootloader_id;
	uint64_t addr;
};

/* Structure of physical seed */
struct physical_seed {
	struct seed_info seed_list[BOOTLOADER_SEED_MAX_ENTRIES];
	uint32_t num_seeds;
	uint32_t pad;
};

#define SEED_ARG_NUM 4U
static struct seed_argument seed_arg[SEED_ARG_NUM] = {
	{ "ImageBootParamsAddr=",        BOOTLOADER_SBL, 0UL },
	{ "ABL.svnseed=",                BOOTLOADER_ABL, 0UL },
	{ "dev_sec_info.param_addr=",    BOOTLOADER_ABL, 0UL },
	{ NULL, BOOTLOADER_INVD, 0UL }
};

static struct physical_seed g_phy_seed;

#define SEED_ENTRY_TYPE_SVNSEED         0x1U
/* #define SEED_ENTRY_TYPE_RPMBSEED        0x2U */

/* #define SEED_ENTRY_USAGE_USEED          0x1U */
#define SEED_ENTRY_USAGE_DSEED          0x2U

struct seed_list_hob {
	uint8_t revision;
	uint8_t reserved0[3];
	uint32_t buffer_size;
	uint8_t total_seed_count;
	uint8_t reserved1[3];
};

struct seed_entry {
	/* SVN based seed or RPMB seed or attestation key_box */
	uint8_t type;
	/* For SVN seed: useed or dseed
	 * For RPMB seed: serial number based or not
	 */
	uint8_t usage;
	/* index for the same type and usage seed */
	uint8_t index;
	uint8_t reserved;
	/* reserved for future use */
	uint16_t flags;
	/* Total size of this seed entry */
	uint16_t seed_entry_size;
	/* SVN seed: struct seed_info
	 * RPMB seed: uint8_t rpmb_key[key_len]
	 */
	uint8_t seed[0];
};

struct image_boot_params {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint64_t p_seed_list;
	uint64_t p_platform_info;
	uint64_t reserved;
};

#define ABL_SEED_LEN 32U
struct abl_seed_info {
	uint8_t svn;
	uint8_t reserved[3];
	uint8_t seed[ABL_SEED_LEN];
};

#define ABL_SEED_LIST_MAX 4U
struct abl_svn_seed {
	uint32_t size_of_this_struct;
	uint32_t version;
	uint32_t num_seeds;
	struct abl_seed_info seed_list[ABL_SEED_LIST_MAX];
};

/*
 * parse_seed_abl
 *
 * description:
 *    This function parse seed_list which provided by ABL.
 *
 * input:
 *    cmdline       pointer to cmdline string
 *
 * output:
 *    phy_seed      pointer to physical seed structure
 *
 * return value:
 *    true if parse successfully, otherwise false.
 */
static bool parse_seed_abl(uint64_t addr, struct physical_seed *phy_seed)
{
	uint32_t i;
	uint32_t legacy_seed_index = 0U;
	struct seed_info *seed_list;
	struct abl_svn_seed *abl_seed = (struct abl_svn_seed *)hpa2hva(addr);
	bool status = false;

	if ((phy_seed != NULL) && (abl_seed != NULL) &&
	    (abl_seed->num_seeds >= 2U) && (abl_seed->num_seeds <= ABL_SEED_LIST_MAX)) {

		seed_list = phy_seed->seed_list;
		/*
		 * The seed_list from ABL contains several seeds which based on SVN
		 * and one legacy seed which is not based on SVN. The legacy seed's
		 * svn value is minimum in the seed list. And CSE ensures at least two
		 * seeds will be generated which will contain the legacy seed.
		 * Here find the legacy seed index first.
		 */
		for (i = 1U; i < abl_seed->num_seeds; i++) {
			if (abl_seed->seed_list[i].svn < abl_seed->seed_list[legacy_seed_index].svn) {
				legacy_seed_index = i;
			}
		}

		/*
		 * Copy out abl_seed for trusty and clear the original seed in memory.
		 * The SOS requires the legacy seed to derive RPMB key. So skip the
		 * legacy seed when clear original seed.
		 */
		(void)memset((void *)&phy_seed->seed_list[0U], 0U, sizeof(phy_seed->seed_list));
		for (i = 0U; i < abl_seed->num_seeds; i++) {
			seed_list[i].cse_svn = abl_seed->seed_list[i].svn;
			(void)memcpy_s((void *)&seed_list[i].seed[0U], sizeof(seed_list[i].seed),
					(void *)&abl_seed->seed_list[i].seed[0U], sizeof(abl_seed->seed_list[i].seed));

			if (i == legacy_seed_index) {
				continue;
			}

			(void)memset((void *)&abl_seed->seed_list[i].seed[0U], 0U,
							sizeof(abl_seed->seed_list[i].seed));
		}

		phy_seed->num_seeds = abl_seed->num_seeds;
		status = true;
	}

	return status;
}

/*
 * parse_seed_sbl
 *
 * description:
 *    This function parse seed_list which provided by SBL
 *
 * input:
 *    cmdline       pointer to cmdline string
 *
 * return value:
 *    true if parse successfully, otherwise false.
 */
static bool parse_seed_sbl(uint64_t addr, struct physical_seed *phy_seed)
{
	uint8_t i;
	uint8_t dseed_index = 0U;
	struct image_boot_params *boot_params = NULL;
	struct seed_list_hob *seed_hob = NULL;
	struct seed_entry *entry = NULL;
	struct seed_info *seed_list = NULL;
	bool status = false;

	boot_params = (struct image_boot_params *)hpa2hva(addr);

	if (boot_params != NULL) {
		seed_hob = (struct seed_list_hob *)hpa2hva(boot_params->p_seed_list);
	}

	if ((seed_hob != NULL) && (phy_seed != NULL)) {
		status = true;

		seed_list = phy_seed->seed_list;

		entry = (struct seed_entry *)((uint8_t *)seed_hob + sizeof(struct seed_list_hob));

		for (i = 0U; i < seed_hob->total_seed_count; i++) {
			if (entry != NULL) {
				/* retrieve dseed */
				if ((SEED_ENTRY_TYPE_SVNSEED == entry->type) &&
				    (SEED_ENTRY_USAGE_DSEED == entry->usage)) {

					/* The seed_entry with same type/usage are always
					 * arranged by index in order of 0~3.
					 */
					if ((entry->index != dseed_index) ||
					    (entry->index >= BOOTLOADER_SEED_MAX_ENTRIES)) {
						status = false;
						break;
					}

					(void)memcpy_s((void *)&seed_list[dseed_index], sizeof(struct seed_info),
							(void *)&entry->seed[0U], sizeof(struct seed_info));
					dseed_index++;

					/* erase original seed in seed entry */
					(void)memset((void *)&entry->seed[0U], 0U, sizeof(struct seed_info));
				}

				entry = (struct seed_entry *)((uint8_t *)entry + entry->seed_entry_size);
			}
		}

		if (status) {
			phy_seed->num_seeds = dseed_index;
		}
	}

	return status;
}


static uint32_t parse_seed_arg(void)
{
	const char *cmd_src = NULL;
	char *arg, *arg_end;
	struct acrn_multiboot_info *mbi = get_multiboot_info();
	uint32_t i = SEED_ARG_NUM - 1U;
	uint32_t len;

	if ((mbi->mi_flags & MULTIBOOT_INFO_HAS_CMDLINE) != 0U) {
		cmd_src = mbi->mi_cmdline;
	}

	if (cmd_src != NULL) {
		for (i = 0U; seed_arg[i].str != NULL; i++) {
			len = strnlen_s(seed_arg[i].str, MEM_1K);
			arg = strstr_s((const char *)cmd_src, MAX_BOOTARGS_SIZE, seed_arg[i].str, len);
			if (arg != NULL) {
				arg += len;
				seed_arg[i].addr = strtoul_hex(arg);

				/*
				 * Replace original arguments with spaces since Guest's GPA might not
				 * identity mapped to HPA. The argument will be appended later when
				 * compose cmdline for Guest.
				 */
				arg_end = strchr(arg, ' ');
				arg -= len;
				len = (arg_end != NULL) ? (uint32_t)(arg_end - arg) :
					strnlen_s(arg, MAX_BOOTARGS_SIZE);
				(void)memset((void *)arg, (uint8_t)' ', len);
				break;
			}
		}
	}

	return i;
}

/*
 * fill_seed_arg
 *
 * description:
 *     fill seed argument to cmdline buffer which has MAX size of MAX_SEED_ARG_SIZE
 *
 * input:
 *    cmd_dst   pointer to cmdline buffer
 *    cmd_sz    size of cmd_dst buffer
 *
 * output:
 *    cmd_dst   pointer to cmdline buffer
 *
 * return value:
 *    none
 *
 * @pre cmd_dst != NULL
 */
void fill_seed_arg(char *cmd_dst, size_t cmd_sz)
{
	uint32_t i;

	for (i = 0U; seed_arg[i].str != NULL; i++) {
		if (seed_arg[i].addr != 0UL) {

			snprintf(cmd_dst, cmd_sz, "%s0x%X ", seed_arg[i].str, sos_vm_hpa2gpa(seed_arg[i].addr));

			if (seed_arg[i].bootloader_id == BOOTLOADER_SBL) {
				struct image_boot_params *boot_params =
					(struct image_boot_params *)hpa2hva(seed_arg[i].addr);

				boot_params->p_seed_list = sos_vm_hpa2gpa(boot_params->p_seed_list);

				boot_params->p_platform_info = sos_vm_hpa2gpa(boot_params->p_platform_info);
			}

			break;
		}
	}
}

/*
 * derive_virtual_seed
 *
 * description:
 *     derive virtual seed list from physical seed list
 *
 * input:
 *    salt        pointer to salt
 *    salt_len    length of salt
 *    info        pointer to info
 *    info_len    length of info
 *
 * output:
 *    seed_list   pointer to seed_list
 *    num_seed    seed number in seed_list
 *
 * return value:
 *    true if derive successfully, otherwise false
 */
bool derive_virtual_seed(struct seed_info *seed_list, uint32_t *num_seeds,
			 const uint8_t *salt, size_t salt_len, const uint8_t *info, size_t info_len)
{
	uint32_t i;
	bool ret = true;

	if ((seed_list == NULL) || (g_phy_seed.num_seeds == 0U)) {
		ret = false;
	} else {
		for (i = 0U; i < g_phy_seed.num_seeds; i++) {
			if (hkdf_sha256(seed_list[i].seed,
					sizeof(seed_list[i].seed),
					g_phy_seed.seed_list[i].seed,
					sizeof(g_phy_seed.seed_list[i].seed),
					salt, salt_len,
					info, info_len) == 0) {
				*num_seeds = 0U;
				(void)memset(seed_list, 0U, sizeof(struct seed_info) * BOOTLOADER_SEED_MAX_ENTRIES);
				pr_err("%s: derive virtual seed list failed!", __func__);
				ret = false;
				break;
			}
			seed_list[i].cse_svn = g_phy_seed.seed_list[i].cse_svn;
		}
		*num_seeds = g_phy_seed.num_seeds;
	}

	return ret;
}

static inline uint32_t get_max_svn_index(void)
{
	uint32_t i, max_svn_idx = 0U;

	for (i = 1U; i < g_phy_seed.num_seeds; i++) {
		if (g_phy_seed.seed_list[i].cse_svn > g_phy_seed.seed_list[i - 1U].cse_svn) {
			max_svn_idx = i;
		}
	}

	return max_svn_idx;
}

/*
 * derive_attkb_enc_key
 *
 * description:
 *     derive attestation keybox encryption key from physical seed(max svn)
 *
 * input:
 *    none
 *
 * output:
 *    out_key     pointer to output key
 *
 * return value:
 *    true if derive successfully, otherwise false
 */
bool derive_attkb_enc_key(uint8_t *out_key)
{
	bool ret = true;
	const uint8_t *ikm;
	uint32_t ikm_len;
	uint32_t max_svn_idx;
	const uint8_t salt[] = "Attestation Keybox Encryption Key";

	if ((out_key == NULL) || (g_phy_seed.num_seeds == 0U) ||
	    (g_phy_seed.num_seeds > BOOTLOADER_SEED_MAX_ENTRIES)) {
		ret = false;
	} else {
		max_svn_idx = get_max_svn_index();
		ikm = &(g_phy_seed.seed_list[max_svn_idx].seed[0]);
		/* only the low 32 bytes of seed are valid */
		ikm_len = 32U;

		if (hmac_sha256(out_key, ikm, ikm_len, salt, sizeof(salt)) != 1) {
			pr_err("%s: failed to derive key!\n", __func__);
			ret = false;
		}
	}

	return ret;
}

void init_seed(void)
{
	bool status;
	uint32_t index;

	index = parse_seed_arg();

	switch (seed_arg[index].bootloader_id) {
	case BOOTLOADER_SBL:
		status = parse_seed_sbl(seed_arg[index].addr, &g_phy_seed);
		break;
	case BOOTLOADER_ABL:
		status = parse_seed_abl(seed_arg[index].addr, &g_phy_seed);
		break;
	default:
		status = false;
		break;
	}

	/* Failed to parse seed from Bootloader, using dummy seed */
	if (!status) {
		g_phy_seed.num_seeds = 1U;
		(void)memset(&g_phy_seed.seed_list[0], 0xA5U, sizeof(g_phy_seed.seed_list));
	}
}
