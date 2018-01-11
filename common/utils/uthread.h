/*
The MIT License (MIT)

Copyright (c) 2014-2015 CSAIL, MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef _H4H_THREAD_H
#define _H4H_THREAD_H

/*
 * Thread Implementation for Kernel Mode 
 */
#if defined(KERNEL_MODE)

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,5,0)
#define h4h_daemonize(a) daemonize(a)
#else
#define h4h_daemonize(a)
#endif

/* kernel version compatibility for 4.14 */
typedef struct wait_queue_entry wait_queue_t;

typedef struct {
	/* thread management */
	h4h_mutex_t thread_done;
	wait_queue_head_t wq;
	wait_queue_t* wait;
	struct task_struct* thread;
	
	/* user management */
	void* user_data;
	int (*user_threadfn)(void *data);
} h4h_thread_t;

#elif defined(USER_MODE)

#include <stdint.h>
#include <pthread.h>
#include "uatomic64.h"

#define SIGKILL	0xCCCC

typedef struct {
	/* thread management */
	h4h_mutex_t thread_done;
	h4h_mutex_t thread_sleep;
	pthread_cond_t thread_con;
	pthread_t thread;

	/* user management */
	void* user_data;
	int (*user_threadfn)(void *data);
} h4h_thread_t;

#endif /* USER_MODE */

h4h_thread_t* h4h_thread_create (int (*threadfn)(void *data), void* data, char* name);
int h4h_thread_run (h4h_thread_t* k);
int h4h_thread_schedule (h4h_thread_t* k);
void h4h_thread_wakeup (h4h_thread_t* k);
void h4h_thread_stop (h4h_thread_t* k);
void h4h_thread_msleep (uint32_t ms);
void h4h_thread_yield (void);
void h4h_thread_schedule_setup (h4h_thread_t* k);
void h4h_thread_schedule_cancel (h4h_thread_t* k);
int h4h_thread_schedule_sleep (h4h_thread_t* k);

#endif /* _H4H_THREAD_H */

