/*
 * my_test_scheduler.c
 *
 *  Created on: Oct 27, 2012
 *      Author: acolesa
 */

#include <threads/thread.h>
#include <stddef.h>
#include <stdio.h>
#include "devices/timer.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/malloc.h"

#define THREAD_CPU_BOUND_NO 1
#define THREAD_IO_BOUND_NO 0
#define CPU_BURST 1000
#define CPU_IO_BURST 10
#define ACQUIRE_TIMES 10
#define BLOCKING_TIME 100

int test_thread_id[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
char test_thread_name[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1][30];
int thread_execution_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
int thread_running_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
int thread_ready_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
int thread_blocked_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
int thread_avg_reaction_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];
int thread_max_reaction_time[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];


struct semaphore* test_thread_semaphore[THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO+1];

struct thread_arg {
	int th_id;					// thread identifier
	struct semaphore *sem;		// semaphore to wait for for a IO bound thread; initially zero
	int acquire_times;			// number of times to wait for the semaphore
	int cpu_burst;				// length of the CPU burst between IO operations
};

static void thread_sched_test_CPU_bound (void *);
static void thread_sched_test_IO_bound (void *);

void get_thread_statistics(int th_id)
{
	thread_execution_time[th_id] = thread_get_execution_time(test_thread_id[th_id]);
	thread_running_time[th_id] = thread_get_running_time(test_thread_id[th_id]);
	thread_ready_time[th_id] = thread_get_ready_time(test_thread_id[th_id]);
	thread_blocked_time[th_id] = thread_get_blocked_time(test_thread_id[th_id]);
	thread_avg_reaction_time[th_id] = thread_get_avg_reaction_time(test_thread_id[th_id]);
	thread_max_reaction_time[th_id] = thread_get_max_reaction_time(test_thread_id[th_id]);
}

void
my_test_scheduler_1(void)
{
	int i;

	int th_id = 0;
	snprintf (test_thread_name[th_id], sizeof test_thread_name[th_id], "%s\0", thread_current()->name);
	msg ("Thread \"%s\" starts", test_thread_name[th_id]);

	msg ("Thread \"%s\" begins creating %d threads", thread_current()->name, THREAD_CPU_BOUND_NO);
	for (i=1; i<=THREAD_CPU_BOUND_NO; i++){
		snprintf (test_thread_name[i], sizeof test_thread_name[i], "sched_th_%d", i);
		struct thread_arg *arg = (struct thread_arg *) malloc(sizeof (struct thread_arg));
		arg->th_id = i;
		arg->sem = NULL;
		test_thread_semaphore[i] = NULL;
		arg->acquire_times = 0;
		arg->cpu_burst = CPU_BURST;
		int tid;
		tid = thread_create (test_thread_name[i], PRI_DEFAULT, thread_sched_test_CPU_bound, (void*)arg);
		test_thread_id[i] = tid;
	}

	for (; i<=THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO; i++){
			snprintf (test_thread_name[i], sizeof test_thread_name[i], "sched_th_%d", i);
			struct thread_arg *arg = (struct thread_arg *) malloc(sizeof (struct thread_arg));
			arg->th_id = i;
			test_thread_semaphore[i] = (struct semaphore*) malloc (sizeof (struct semaphore));
			arg->sem = test_thread_semaphore[i];
			ASSERT(arg->sem != NULL);
			sema_init(arg->sem, 0);
			arg->acquire_times = ACQUIRE_TIMES;
			arg->cpu_burst = CPU_IO_BURST;
			int tid;
			tid = thread_create (test_thread_name[i], PRI_DEFAULT, thread_sched_test_IO_bound, (void*)arg);
			test_thread_id[i] = tid;
	}

	// wait for a while to be sure the IO-threads were blocked waiting for the semaphores
	timer_sleep (THREAD_IO_BOUND_NO*CPU_IO_BURST + 100);

	int k;
	for (k = 1; k <= ACQUIRE_TIMES; k++) {
		for (i=THREAD_CPU_BOUND_NO+1; i<=THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO; i++){
			sema_up(test_thread_semaphore[i]);		// wakes up the blocked IO threads
		}
		timer_sleep(BLOCKING_TIME+CPU_IO_BURST+1);	// waits for at least the same period a IO-bound thread will run
	}

	msg ("Thread \"%s\" finished creating %d threads", thread_current()->name, THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO);

	/* Wait for the previously created threads to finish their execution */
	timer_sleep (THREAD_CPU_BOUND_NO/10*CPU_BURST + 100);

	msg ("Thread_ID\tThread_name\tExecution\tRunning\t    Ready\t  Blocked\tAvg._reaction\tMax_reaction");
	for (i=1; i<=THREAD_CPU_BOUND_NO+THREAD_IO_BOUND_NO; i++) {
		get_thread_statistics(i);
		msg ("%9d\t%11s\t%9d\t%7d\t%9d\t%9d\t%13d\t%12d", i, test_thread_name[i], thread_execution_time[i],
				thread_running_time[i], thread_ready_time[i], thread_blocked_time[i],
				thread_avg_reaction_time[i], thread_max_reaction_time[i]);
	}

	msg ("Thread \"%s\" exits its function", thread_current()->name);
}


/* CPU-bound threads */
static void thread_sched_test_CPU_bound (void *arg)
{
	struct thread_arg *th_arg = (struct io_arg*) arg;

	int th_id = th_arg->th_id;

	//msg ("Thread \"%s\" begins its execution", thread_current()->name);

	cpu_usage(th_arg->cpu_burst);

	//msg ("Thread \"%s\" ends its execution", thread_current()->name);
}


/* I/O-bound threads */
static void thread_sched_test_IO_bound (void *arg)
{
	struct thread_arg *th_arg = (struct io_arg*) arg;

	int th_id = th_arg->th_id;

	msg ("Thread \"%s\" begins its execution", thread_current()->name);

	int k;
	for (k = 1; k<=th_arg->acquire_times; k++) {
		sema_down(th_arg->sem);
		cpu_usage(th_arg->cpu_burst);
	}

	msg ("Thread \"%s\" ends its execution", thread_current()->name);
}

