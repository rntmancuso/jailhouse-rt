/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) ARM Limited, 2014
 * Copyright (c) Siemens AG, 2014-2017
 *
 * Authors:
 *  Jean-Philippe Brucker <jean-philippe.brucker@arm.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <inmate.h>
#include <gic.h>
#include <jailhouse/memguard-common.h>
#include <asm/sysregs.h>

#define MEM_SIZE_MB         4 /* Memory size will be 4 MB */
#define MEM_SIZE            (MEM_SIZE_MB * 1024 * 1024)
#define LINE_SIZE           (64) //Cache line size is 64 bytes
#define BIT(x)              (1UL << x)

#define print(fmt, ...)							\
	printk("[BOMB %d] " fmt, id, ##__VA_ARGS__)

/* Virtual address of command and control interface */
#define CMD_REGION_BASE     0x500000
#define CMD_ENABLE          BIT(0)
#define CMD_DO_READS        BIT(1)
#define CMD_DO_WRITES       BIT(2)
#define CMD_VERBOSE         BIT(3)
#define CMD_BOMB_ID_SHIFT   4

/* Functions to interact with CPU cycle counter */
#define magic_timing_begin(cycles)\
	do{								\
		asm volatile("mrs %0, CNTVCT_EL0": "=r"(*(cycles)) );	\
	}while(0)

#define magic_timing_end(cycles)					\
	do{								\
		unsigned long tempCycleLo;				\
		asm volatile("mrs %0, CNTVCT_EL0":"=r"(tempCycleLo) );  \
		*(cycles) = tempCycleLo - *(cycles);			\
	}while(0)

static volatile unsigned char buffer[MEM_SIZE];
u64 crc;
u32 id;
struct memguard_params mg_params;

/* Structure of the command & control interface  */
struct control {
	u32 command;
	u32 size;
	u32 mg_budget;
};

/* Perform write-only iterations over the memory buffer */
void do_reads(volatile struct control * ctrl);

/* Perform write-only iterations over the memory buffer */
void do_writes(volatile struct control * ctrl);

/* Perform write-only iterations over the memory buffer */
void do_reads_writes(volatile struct control * ctrl);


/* Perform read-only iterations over the memory buffer */
void do_reads(volatile struct control * ctrl)
{
	int i = 0;
	int size = ctrl->size;

	crc = 0;

	if (ctrl->command & CMD_VERBOSE)
		print("Started READ accesses with size %d.\n", size);

	while (ctrl->command & CMD_ENABLE) {
		for (i = 0; i < size; i += LINE_SIZE) {
			crc += buffer[i];
		}
	}

	if (ctrl->command & CMD_VERBOSE)
		print("Done with READ accesses. Check = 0x%08llx\n", crc);

}

/* Perform write-only iterations over the memory buffer */
void do_writes(volatile struct control * ctrl)
{
	int i = 0;
	int size = ctrl->size;

	crc = 0;

	if (ctrl->command & CMD_VERBOSE)
		print("Started WRITE accesses with size %d.\n", size);

	while (ctrl->command & CMD_ENABLE) {
		for (i = 0; i < size; i += LINE_SIZE) {
			buffer[i] += i;
		}
	}

	if (ctrl->command & CMD_VERBOSE)
		print("Done with WRITE accesses.\n");

}

/* Perform write-only iterations over the memory buffer */
void do_reads_writes(volatile struct control * ctrl)
{
	int i = 0;
	int size = ctrl->size;

	crc = 0;

	if (ctrl->command & CMD_VERBOSE)
		print("Started READ+WRITE accesses with size %d.\n", size);

	/* The top half will be written, the bottom half will be read */
	size = size / 2;

	while (ctrl->command & CMD_ENABLE) {
		for (i = 0; i < size; i += LINE_SIZE) {
			buffer[i] += buffer[i+size];
		}
	}

	if (ctrl->command & CMD_VERBOSE)
		print("Done with READ+WRITE accesses.\n");

}

static void test_translation(unsigned long addr)
{
        unsigned long par;
        asm volatile("at s1e1r, %0" : : "r"(addr));
        arm_read_sysreg(PAR_EL1, par);
        print("Translated 0x%08lx -> 0x%08lx\n", addr, par);
}



void inmate_main(void)
{
	printk("Test\n");
	volatile struct control * ctrl = (volatile struct control *)(CMD_REGION_BASE);

	/* Set the ID of this bomb from the passed command */
	id = ctrl->command >> CMD_BOMB_ID_SHIFT;
	mg_params.budget_time = 1000;
	mg_params.flags = 1;

	print("Memory Bomb Started.\n");

	test_translation((unsigned long)buffer);
	jailhouse_call_arg1(JAILHOUSE_HC_QOS+1, (unsigned long)buffer);

	/* Main loop */
	while(1) {
		/* Idle while the enable bit is cleared */
		while(!(ctrl->command & CMD_ENABLE));

		if (ctrl->mg_budget > 0) {
			print("Setting MG budget %d\n", ctrl->mg_budget);
			mg_params.budget_memory = ctrl->mg_budget;
			jailhouse_call_arg1(JAILHOUSE_HC_MEMGUARD, (unsigned long)&mg_params);
		}

		if ((ctrl->command & CMD_DO_READS) &&
		    (ctrl->command & CMD_DO_WRITES))
			do_reads_writes(ctrl);
		else if (ctrl->command & CMD_DO_READS)
			do_reads(ctrl);
		else if (ctrl->command & CMD_DO_WRITES)
			do_writes(ctrl);
		else {
			print("Invalid command (0x%08x)\n",
			      ctrl->command);

			/* Clear the enable bit so we do not print and
			 * endless list of errors. */
			ctrl->command &= ~CMD_ENABLE;
		}
	}

	halt();
}
