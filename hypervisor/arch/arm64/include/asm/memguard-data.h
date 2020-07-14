#ifndef MEMGUARD_DATA_H
#define MEMGUARD_DATA_H

struct memguard {
	unsigned long start_time;
	unsigned long last_time;
	unsigned long pmu_evt_cnt;
	unsigned long budget_time;
	unsigned long budget_memory;
	unsigned long flags;
	bool memory_overrun;
	bool time_overrun;
	volatile u8 block;
};

#endif
