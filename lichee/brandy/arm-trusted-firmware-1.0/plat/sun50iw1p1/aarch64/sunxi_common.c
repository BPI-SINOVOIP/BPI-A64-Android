/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch.h>
#include <arch_helpers.h>
#include <arm_gic.h>
#include <bl_common.h>
#include <cci400.h>
#include <debug.h>
#include <mmio.h>
#include <platform.h>
#include <plat_config.h>
#include <xlat_tables.h>
#include "../sunxi_def.h"
#include "../sunxi_private.h"

/*******************************************************************************
 * plat_config holds the characteristics of the differences between the three
 * FVP platforms (Base, A53_A57 & Foundation). It will be populated during cold
 * boot at each boot stage by the primary before enabling the MMU (to allow cci
 * configuration) & used thereafter. Each BL will have its own copy to allow
 * independent operation.
 ******************************************************************************/
plat_config_t plat_config;

/*
 * Table of regions to map using the MMU.
 * This doesn't include TZRAM as the 'mem_layout' argument passed to
 * configure_mmu_elx() will give the available subset of that,
 */
const mmap_region_t sunxi_mmap[] = {

	//1G
	{ 0,				0,				DRAM1_BASE,			MT_DEVICE | MT_RW | MT_SECURE },
	//2G
	{ DRAM1_BASE,			DRAM1_BASE,			SUNXI_MAX_DRAM_SIZE,		MT_MEMORY | MT_RW | MT_NS},
	//TRUSTED dram for secure os and shared memory
	{ SUNXI_TRUSTED_DRAM_BASE,	SUNXI_TRUSTED_DRAM_BASE,	SUNXI_TRUSTED_DRAM_SIZE,	MT_MEMORY | MT_RW | MT_SECURE },
	{ MEMRES_BASE,			MEMRES_BASE,			MEMRES_SIZE,			MT_DEVICE | MT_RW | MT_SECURE },
	{0}
};




/*******************************************************************************
 * Macro generating the code for the function setting up the pagetables as per
 * the platform memory map & initialize the mmu, for the given exception level
 ******************************************************************************/
#define DEFINE_CONFIGURE_MMU_EL(_el)					\
	void sunxi_configure_mmu_el##_el(unsigned long total_base,	\
				   unsigned long total_size,		\
				   unsigned long ro_start,		\
				   unsigned long ro_limit,		\
				   unsigned long coh_start,		\
				   unsigned long coh_limit)		\
	{								\
		mmap_add_region(total_base, total_base,			\
				total_size,				\
				MT_MEMORY | MT_RW | MT_SECURE);		\
		mmap_add_region(ro_start, ro_start,			\
				ro_limit - ro_start,			\
				MT_MEMORY | MT_RO | MT_SECURE);		\
		mmap_add_region(coh_start, coh_start,			\
				coh_limit - coh_start,			\
				MT_DEVICE | MT_RW | MT_SECURE);		\
		mmap_add(sunxi_mmap);					\
		init_xlat_tables();					\
									\
		enable_mmu_el##_el(0);					\
	}

void sunxi_configure_mmu_el3(unsigned long total_base,
				   unsigned long total_size,
				   unsigned long ro_start,
				   unsigned long ro_limit,
				   unsigned long coh_start,
				   unsigned long coh_limit)
{
	mmap_add_region(total_base, total_base,
			total_size,
			MT_MEMORY | MT_RW | MT_SECURE);
	mmap_add_region(ro_start, ro_start,
			ro_limit - ro_start,
			MT_MEMORY | MT_RO | MT_SECURE);
	mmap_add_region(coh_start, coh_start,
			coh_limit - coh_start,
			MT_DEVICE | MT_RW | MT_SECURE);
	mmap_add(sunxi_mmap);
	init_xlat_tables();

	enable_mmu_el3(0);
}

void sunxi_configure_mmu_el1(unsigned long total_base,
				   unsigned long total_size,
				   unsigned long ro_start,
				   unsigned long ro_limit,
				   unsigned long coh_start,
				   unsigned long coh_limit)
{
	mmap_add_region(total_base, total_base,
			total_size,
			MT_MEMORY | MT_RW | MT_SECURE);
	mmap_add_region(ro_start, ro_start,
			ro_limit - ro_start,
			MT_MEMORY | MT_RO | MT_SECURE);
	mmap_add_region(coh_start, coh_start,
			coh_limit - coh_start,
			MT_DEVICE | MT_RW | MT_SECURE);
	mmap_add(sunxi_mmap);
	init_xlat_tables();

	enable_mmu_el1(0);
}


/* Define EL1 and EL3 variants of the function initialising the MMU */
//DEFINE_CONFIGURE_MMU_EL(1)
//DEFINE_CONFIGURE_MMU_EL(3)

/*******************************************************************************
 * A single boot loader stack is expected to work on both the Foundation FVP
 * models and the two flavours of the Base FVP models (AEMv8 & Cortex). The
 * SYS_ID register provides a mechanism for detecting the differences between
 * these platforms. This information is stored in a per-BL array to allow the
 * code to take the correct path.Per BL platform configuration.
 ******************************************************************************/
int sunxi_config_setup(void)
{

	return 0;
}

unsigned long plat_get_ns_image_entrypoint(void)
{
	return NS_IMAGE_OFFSET;
}

uint64_t plat_get_syscnt_freq(void)
{
	uint64_t counter_base_frequency;

	/* Read the frequency from Frequency modes table */
	counter_base_frequency = 24000000;//mmio_read_32(SYS_CNTCTL_BASE + CNTFID_OFF);

	/* The first entry of the frequency modes table must not be 0 */
	if (counter_base_frequency == 0)
		panic();

	return counter_base_frequency;
}

void sunxi_cci_init(void)
{
	/*
	 * Initialize CCI-400 driver
	 */

}

void sunxi_cci_enable(void)
{
	/*
	 * Enable CCI-400 coherency for this cluster. No need
	 * for locks as no other cpu is active at the
	 * moment
	 */

}


void sunxi_gic_init(void)
{
	gic_setup();
}


/*******************************************************************************
 * Gets SPSR for BL32 entry
 ******************************************************************************/
uint32_t sunxi_get_spsr_for_bl32_entry(void)
{
	/*
	 * The Secure Payload Dispatcher service is responsible for
	 * setting the SPSR prior to entry into the BL32 image.
	 */
	//return 0;
	return SPSR_MODE32(MODE32_svc,SPSR_T_ARM,SPSR_E_LITTLE,DISABLE_ALL_EXCEPTIONS);
}

/*******************************************************************************
 * Gets SPSR for BL33 entry
 ******************************************************************************/
uint32_t sunxi_get_spsr_for_bl33_entry(void)
{
	#if 0
	unsigned long el_status;
	unsigned int mode;
	uint32_t spsr;

	/* Figure out what mode we enter the non-secure world in */
	el_status = read_id_aa64pfr0_el1() >> ID_AA64PFR0_EL2_SHIFT;
	el_status &= ID_AA64PFR0_ELX_MASK;

	if (el_status)
		mode = MODE_EL2;
	else
		mode = MODE_EL1;

	/*
	 * TODO: Consider the possibility of specifying the SPSR in
	 * the FIP ToC and allowing the platform to have a say as
	 * well.
	 */
	spsr = SPSR_64(mode, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS);
	return spsr;
	#endif
	return SPSR_MODE32(MODE32_svc,SPSR_T_ARM,SPSR_E_LITTLE,DISABLE_ALL_EXCEPTIONS);
}
