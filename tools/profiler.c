/*
 * Jailhouse, a Linux-based partitioning hypervisor
 * 
 * DDR Profiling user-space utility for NXP S32V234
 * 
 * Copyright (c) Boston University
 *
 * Authors:
 *  Renato Mancuso <rmancuso@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <signal.h>
#include <sys/wait.h>

/* Common structures between user-space tool and inmate */
#include "../inmates/demos/arm/profiler.h"

/* Location of the control & data interface of the profiler inmate.
   NOTE: this depends on how the log area is mapped in the rootcell
   configuration. 
 */
#define PROFILER_MEM_BASE 0x100000000UL

#define USAGE_STR							\
	"Usage: %s -o <output file> [-p cycles]"			\
	" [-d DRAM ctrl] [-m max count]"				\
	" [-i AXI_ID] [-x AXI_MASK] [-b] [-t]\n"

#define CALC_DIFF(cur, prev, res)				\
	do {							\
		if (prev > cur)					\
			res = cur + (0xFFFFFFFFUL - prev);	\
		else						\
			res = cur - prev;			\
	} while(0)

#define BUFLEN           256
#define DEFAULT_CYCLES   256
#define DEFAULT_MMDC     0
#define DEFAULT_MAXCOUNT 41943039 /* Not stopping until buffer full or stop command issued */
#define MAX_BENCHMARKS   10
#define MAX_PARAMS       10

/* === Global Variables === */
int flag_rt = 1;
int flag_isol = 0;
int flag_bytes = 0;
int flag_onlytime = 0;
int flag_noprof = 0;

int max_prio;
int running_bms = 0;
pid_t pids[MAX_BENCHMARKS];
uint64_t start_ts[MAX_BENCHMARKS];
volatile int done = 0;

/* === Function Prototypes === */
void launch_benchmarks (char * bms[], int bm_count);
void proc_exit_handler (int signo, siginfo_t * info, void * extra);
void wait_completion(void);
void change_rt_prio(int prio, int cpu);
void set_realtime(int prio, int cpu);
static inline unsigned long arm_v8_get_timing(void);

int main (int argc, char ** argv)
{
	char * strbuffer = NULL;
	int outfd = -1, memfd, opt;
	unsigned long cycles = DEFAULT_CYCLES;
	unsigned long mmdc = DEFAULT_MMDC;
	unsigned long maxcount = DEFAULT_MAXCOUNT;
	unsigned long i;
	void * mem;
	char * bms [MAX_BENCHMARKS];
	int bm_count = 0;
	ssize_t len;
	uint64_t tot_cycles;
	uint64_t tot_reads;
	uint64_t tot_writes;
	
	/* Default values should be for Cluster 0 */
	/* uint16_t axi_id = 0x2020; */
	/* uint16_t axi_mask = 0xE037; */

	/* Default values should be for any CPU */
	uint16_t axi_id   = 0x2000;
	uint16_t axi_mask = 0xE007;
	
	while ((opt = getopt(argc, argv, "-o:p:d:m:i:x:btcn")) != -1) {
		switch (opt) {
		case 1:
			/* Benchmark to run parameter */
			bms[bm_count++] = argv[optind - 1];
			break;			
		case 'o':
			strbuffer = (char *)malloc(BUFLEN);
			sprintf(strbuffer, "%s", optarg);

			if((outfd = open(strbuffer, O_CREAT | O_TRUNC | O_RDWR, 0660)) < 0) {
				perror("Unable to open/create output file.");
				exit(EXIT_FAILURE);
			}

			free(strbuffer);
			break;
		case 'p':
			cycles = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			opt = atoi(optarg);
			if (opt == 1)
				mmdc = 1;
			else if (opt != DEFAULT_MMDC) {
				fprintf(stderr, "Parameter -d only accepts a value of 0 or 1\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'm':
			maxcount = strtoul(optarg, NULL, 0);
			break;
		case 'b':
			flag_bytes = 1;
			break;
		case 'c':
			flag_isol = 1;
			break;
		case 'n':
			flag_noprof = 1;
			break;
		case 't':
			flag_onlytime = 1;
			break;
		case 'i':
			axi_id = strtoul(optarg, NULL, 0);
			break;
		case 'x':
			axi_mask = strtoul(optarg, NULL, 0);
			break;			
		default: /* '?' */
			fprintf(stderr, USAGE_STR, argv[0]);
			exit(EXIT_FAILURE);
		}
	}	


	if (outfd < 0 && !flag_onlytime) {
		fprintf(stderr, USAGE_STR, argv[0]);
		exit(EXIT_FAILURE);		
	}

	/* All good here with input parameters. Set max prio and pin to CPU 0 */
	max_prio = sched_get_priority_max(SCHED_FIFO);
	set_realtime(max_prio, 0);

	/* Level the playing field by trashing the cache */
	memset(pids, 0, MAX_BENCHMARKS * sizeof(pid_t));
	volatile unsigned char * trash = (volatile unsigned char *)malloc(8*1024*1024);
	for (i = 0; i < 8*1024*1024; i+=64)
		trash[i] += i;
	
	/* If this flag is specified, skip profiling entirely */
	if (!flag_noprof) {	
		/* Open physical memory */
		memfd = open("/dev/mem", O_RDWR);

		if(memfd < 0) {
			perror("Unable to open /dev/mem. Are you root?");
			exit(EXIT_FAILURE);
		}

		mem = mmap(0, 0x3c000000, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, PROFILER_MEM_BASE);

		if (mem == ((void *)-1)) {
			perror("Unable to map control & log memory.");
			exit(EXIT_FAILURE);		
		}

		/* We can now interact with the profiler */
	
		
		/* First off, verify signature */
		volatile struct config * ctrl = (struct config *)mem;
		struct sample * log = (struct sample *)(((void *)ctrl) + sizeof(struct config));
		struct sample * prev = log;
	
		if((ctrl->control & (0xFFUL << 56)) != PROF_SIGNATURE) {
			fprintf(stderr, "Profiler not active.\n");
			exit(EXIT_FAILURE);
		} else {
			printf("Profiler READY!\n");		
		}


		/* Start sampling with given parameters */
		ctrl->maxcount = maxcount;

		/* For cluster 0, see Figure 34-1 in S32 Manual. */
		ctrl->axi_value = axi_id;
		ctrl->axi_mask  = axi_mask;			
		ctrl->control = (flag_bytes?PROF_BYTES:0) | (cycles << 4) | (mmdc << 2) | PROF_AUTOSTOP;
	
	
		/* Now that profiling has been started, kick off the benchmarks */
		launch_benchmarks(bms, bm_count);

		/* Enable profile acquisition only after all the BMs have been started */
		ctrl->control |= PROF_ENABLED;
	
		/* Wait for all the benchmarks to complete */
		wait_completion();
	
		/* Stop acquisition */
		ctrl->control &= ~PROF_ENABLED;
	
		/* Check that the profiler is done and read number of samples acquired  */
		printf("Profiler %s.\n", (ctrl->control & PROF_ENABLED ? "ACTIVE" : "DONE"));
		printf("Number of samples: %ld\n", ctrl->count);

		tot_cycles = 0;
		tot_reads = 0;
		tot_writes = 0;

		/* Post-process profile and write to disk if output requested */
		strbuffer = (char *)malloc(BUFLEN);	
		for (i = 0; i < ctrl->count; ++i, ++log) {
			int to_write, res;

			uint32_t cpu_cycles, dram_cycles, busy_cycles, reads, writes;

			CALC_DIFF(log->cycles, prev->cycles, cpu_cycles);
			CALC_DIFF(log->total_cycles, prev->total_cycles, dram_cycles);
			CALC_DIFF(log->busy_cycles, prev->busy_cycles, busy_cycles);
			CALC_DIFF(log->reads, prev->reads, reads);
			CALC_DIFF(log->writes, prev->writes, writes);

			tot_cycles += cpu_cycles;
			tot_reads += reads;
			tot_writes += writes;

			if (!flag_onlytime) {
				len = sprintf(strbuffer, "%ld,%d,%d,%d,%d,%d\n",
					      i, cpu_cycles, dram_cycles,
					      busy_cycles, reads, writes);
			
				to_write = len;
			
				while (to_write > 0) {
					res = write(outfd, strbuffer, to_write);
					if (res < 0) {
						perror("Ubable to write to output file");
						exit(EXIT_FAILURE);
					}
					to_write -= res;
				}
			}
		
			prev = log;
		}
		free(strbuffer);

		printf("PSTATS\t %ld, %ld, %ld\n", tot_cycles, tot_reads, tot_writes);

		/* Printout total cycles per PID */
		i = 0;
		while (pids[i] != 0) {
			printf("PID %d RUNTIME: %ld\n", pids[i], start_ts[i]);
			++i;
		}

		
	} else {
		/* The noprof flag skips the profiling entirely. In
		 * this case, just launch the benchmarks
		 * simultaneously and report their timing */
		int i = 0;
		
		launch_benchmarks(bms, bm_count);		

		/* Wait for all the benchmarks to complete */
		wait_completion();

		/* Printout total cycles per PID */
		while (pids[i] != 0) {
			printf("PID %d RUNTIME: %ld\n", pids[i], start_ts[i]);
			++i;
		}
		
	}
	
	if (outfd >= 0)
		close(outfd);
		
	return EXIT_SUCCESS;
		
}

/* Function to spawn all the listed benchmarks */
void launch_benchmarks (char * bms[], int bm_count)
{
	int i;
	
	for (i = 0; i < bm_count; ++i) {

		/* Launch all the BMs one by one */		
		pid_t cpid = fork();
		if (cpid == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		/* Child process */
		if (cpid == 0) {
			int p = 0;
			/* Assume that there is only at most one paramer */
			char ** args = (char **)malloc(MAX_PARAMS * sizeof(char *));

			/* To follow execv's convention*/
			while(bms[i] && p < MAX_PARAMS - 1) {
				args[p++] = strsep(&bms[i], " ");
			}			
			args[p] = NULL;

			/* Set SCHED_FIFO priority if necessary, schedule on CPU i */
			if (flag_rt)
				change_rt_prio(max_prio -1 -i, i);
					
			sched_yield();
						
			execv(args[0], args);
			
			/* This point can only be reached if execl fails. */
			perror("Unable to run benchmark");
			exit(EXIT_FAILURE);
		}
		/* Parent process */	       
		else {
			/* Keep track of the new bm that has been launched */
			printf("Running: %s (PID = %d, prio = %d)\n", bms[i], cpid,
			       (flag_rt?(max_prio -1 -i):0));

			start_ts[i] = arm_v8_get_timing();
			pids[running_bms++] = cpid;
			//cpid_arr[i*NUM_SD_VBS_BENCHMARKS_DATASETS+j] = cpid;
		}
		
	}

	(void)pids;
	
}


/* Handler for SIGCHLD signal to detect benchmark termination */
/* Adapted from https://docs.oracle.com/cd/E19455-01/806-4750/signals-7/index.html */
void proc_exit_handler (int signo, siginfo_t * info, void * extra)
{
	int wstat;
	pid_t pid;
	
	(void)signo;
	(void)info;
	(void)extra;
	
	for (;;) {
		pid = waitpid (-1, &wstat, WNOHANG);
		if (pid == 0)
			/* No change in the state of the child(ren) */
			return;
		else if (pid == -1) {
			/* Something went wrong */
			perror("Waitpid() exited with error");
			exit(EXIT_FAILURE);
			return;
		}
		else {
			uint64_t end = arm_v8_get_timing();
			int i;
			printf ("PID %d Done. Return code: %d\n", pid, WEXITSTATUS(wstat));

			/* Record runtime of this benchmark */
			for (i = 0; i < MAX_BENCHMARKS; ++i) {
				if (pids[i] == pid) {
					start_ts[i] = end - start_ts[i];
					break;
				}
			}
			
			/* Detect completion of all the benchmarks */
			if(--running_bms == 0) {
				done = 1;
				return;
			}
		}
	}
}


/* Wait for completion using signals */
void wait_completion(void)
{
	sigset_t waitmask;
	struct sigaction chld_sa;
	
	/* Use RT POSIX extension */
	chld_sa.sa_flags = SA_SIGINFO;
	chld_sa.sa_sigaction = proc_exit_handler;
	sigemptyset(&chld_sa.sa_mask);
	sigaddset(&chld_sa.sa_mask, SIGCHLD);

	/* Install SIGCHLD signal handler */
	sigaction(SIGCHLD, &chld_sa, NULL);
	
	/* Wait for any signal */
	sigemptyset(&waitmask);
	while(!done){
		sigsuspend(&waitmask);
	}

}


/* Only change RT prio of calling process and its CPU affinity */
void change_rt_prio(int prio, int cpu)
{
	struct sched_param sp;
	
	/* Initialize attributes */
	memset(&sp, 0, sizeof(struct sched_param));
	sp.sched_priority = prio;
	
	if(sched_setparam(0, &sp) < 0) {
		perror("Unable to set new RT priority");
		exit(EXIT_FAILURE);		
	}

	/* Set CPU affinity if isolate flag specified */
	if (flag_isol) {
		cpu_set_t set;
		
		CPU_ZERO(&set);

		CPU_SET(cpu, &set);
		
		if (sched_setaffinity(getpid(), sizeof(set), &set) == -1) {
			perror("Unable to set CPU affinity.");
			exit(EXIT_FAILURE);			
		}

	}
	
}


/* Set real-time SCHED_FIFO scheduler with given priority and pin to CPU*/
void set_realtime(int prio, int cpu)
{
	struct sched_param sp;
	
	/* Initialize parameters */
	memset(&sp, 0, sizeof(struct sched_param));
	sp.sched_priority = prio;
	
	/* Attempt to set the scheduler for current process */
	if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) {
		perror("Unable to set SCHED_FIFO scheduler");
		exit(EXIT_FAILURE);
	}

	/* Set CPU affinity if isolate flag specified */
	if (flag_isol) {
		cpu_set_t set;
		CPU_ZERO(&set);

		CPU_SET(cpu, &set);
		
		if (sched_setaffinity(getpid(), sizeof(set), &set) == -1) {
			perror("Unable to set CPU affinity.");
			exit(EXIT_FAILURE);			
		}

	}

}

static inline unsigned long arm_v8_get_timing(void)
{
	unsigned long volatile result = 0;
	asm volatile("MRS %0, cntvct_el0" : "=r" (result));
	return result;
}
