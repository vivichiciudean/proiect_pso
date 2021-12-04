/*
 * my_test_sync.c
 *
 *  Created on: Nov 4, 2012
 *      Author: Adrian Colesa
 */

#include <threads/thread.h>
#include <stddef.h>
#include <stdio.h>
#include "devices/timer.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/malloc.h"

#define NO_OF_THREADS_SEMA 3
#define NO_OF_THREADS_LOCK 10
#define NO_OF_THREADS_COND 10

#define NO_OF_THREADS_PRODCONS_SEMA 10
#define PRODCONS_BUFF_SIZE 3

#define INIT_VALUE_SEMA 1

#define TIME_TO_SLEEP 100

static void thread_sema(void*);
static void thread_lock(void*);
static void thread_cond(void*);


/* Study SEMAPHORES */

struct sema_arg {
	int th_id;
	struct semaphore *sema;
};

void
my_test_synch_sema(void)
{
	int i;
	struct semaphore s;

	sema_init_name(&s, INIT_VALUE_SEMA, "sema-1");

	for (i=1; i<=NO_OF_THREADS_SEMA; i++) {
		char name[16];
		snprintf(name, sizeof (name), "Th-%d sema", i);
		struct sema_arg *arg = (struct sema_arg*) malloc (sizeof(struct sema_arg));
		arg->th_id = i;
		arg->sema = &s;
		thread_create(name, PRI_DEFAULT, thread_sema, (void*) arg);
	}

	timer_sleep(TIME_TO_SLEEP*(NO_OF_THREADS_SEMA+1));

}


static void thread_sema(void* arg)
{
	struct sema_arg *sa = (struct sema_arg*) arg;

	int th_id = sa->th_id;
	struct semaphore *s = sa->sema;

	printf("[thread_sema] Thread \"%s\" BEFORE the critical section\n", thread_current()->name);

	sema_down(s);

	printf("[thread_sema] Thread \"%s\" IN the critical region\n", thread_current()->name);

	//timer_sleep(TIME_TO_SLEEP);

	sema_up(s);

	printf("[thread_sema] Thread \"%s\" AFTER the critical region\n", thread_current()->name);
}


/* Study LOCKS */

struct lock_arg {
	int th_id;
	struct lock *l;
};

void
my_test_synch_lock(void)
{
	int i;
	struct lock mutex;

	lock_init_name(&mutex, "lock-1");

	for (i=1; i<=NO_OF_THREADS_LOCK; i++) {
		char name[16];
		snprintf(name, sizeof (name), "Th-%d lock", i);
		struct lock_arg *arg = (struct lock_arg*) malloc (sizeof(struct lock_arg));
		arg->th_id = i;
		arg->l = &mutex;
		thread_create(name, PRI_DEFAULT, thread_lock, (void*) arg);
	}

	timer_sleep(TIME_TO_SLEEP*(NO_OF_THREADS_LOCK+1));
}

static void thread_lock(void* arg)
{
	struct lock_arg *la = (struct lock_arg*) arg;

	int th_id = la->th_id;
	struct lock *mutex = la->l;

	printf("[thread_lock] Thread \"%s\" BEFORE the critical section\n", thread_current()->name);

	lock_acquire(mutex);

	printf("[thread_lock] Thread \"%s\" IN the critical region\n", thread_current()->name);

	//timer_sleep(TIME_TO_SLEEP);

	lock_release(mutex);

	printf("[thread_lock] Thread \"%s\" AFTER the critical region\n", thread_current()->name);
}


struct semaphore sema_prod;
struct semaphore sema_cons;
struct semaphore sema_mutex;
int prodcond_buff[PRODCONS_BUFF_SIZE];
int prod_index;
int cons_index;

/* Producers/Consumers problem with semaphores */
void
my_test_synch_prodcons_sema(void)
{
	int i;

	sema_init_name(&sema_prod, PRODCONS_BUFF_SIZE, "sema_prod");
	sema_init_name(&sema_cons, 0, "sema_cons");
	sema_init_name(&sema_mutex, 1, "sema_mutex");

	prod_index = 0;
	cons_index = 0;

	for (i=1; i<=NO_OF_THREADS_PRODCONS_SEMA; i++) {
		char name[16];
		struct sema_arg *arg = (struct sema_arg*) malloc (sizeof(struct sema_arg));
		arg->th_id = i;
		arg->sema = NULL;
		if (i%2 == 0) {
			snprintf(name, sizeof (name), "Prod-%d-sem", i);
			thread_create(name, PRI_DEFAULT, thread_sema_prod, (void*) arg);
		} else {
			snprintf(name, sizeof (name), "Cons-%d-sem", i);
			thread_create(name, PRI_DEFAULT, thread_sema_cons, (void*) arg);
		}
	}

	timer_sleep(TIME_TO_SLEEP*(NO_OF_THREADS_PRODCONS_SEMA+1));

}

static
void thread_sema_prod(void* arg) {
	struct sema_arg sa_prod = (struct sema_arg *) arg;

}


/* Study CONDITION VARIABLES */

void
my_test_synch_cond(void)
{

}

static void thread_cond(void* arg)
{
	int th_id = (int) arg;

	printf("Thread %d in function\n", th_id);
}
