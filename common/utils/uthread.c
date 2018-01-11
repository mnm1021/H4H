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

#include "debug.h"
#include "usync.h"
#include "umemory.h"
#include "uthread.h"

#if defined(KERNEL_MODE)
#include <linux/init.h>
#include <linux/module.h>
/* kernel version compatibility for 4.14 */
#include <linux/signal.h>
#include <linux/sched/signal.h>

int h4h_thread_fn (void *data) 
{
	h4h_thread_t* k = (h4h_thread_t*)data;

	/* initialize a kernel thread */
	DECLARE_WAITQUEUE (wait, current);
	k->wait = &wait;

	h4h_daemonize ("h4h_thread_fn");
	allow_signal (SIGKILL); 

	h4h_mutex_lock (&k->thread_done);
	
	/* invoke a user call-back function */
	k->user_threadfn (k->user_data);


	/* a thread stops */
	k->wait = NULL;
	h4h_mutex_unlock (&k->thread_done);

	return 0;
};

h4h_thread_t* h4h_thread_create (
	int (*user_threadfn)(void *data), 
	void* user_data, 
	char* name)
{
	h4h_thread_t* k = NULL;

	/* create h4h_thread_t */
	if ((k = (h4h_thread_t*)h4h_malloc_atomic (
			sizeof (h4h_thread_t))) == NULL) {
		h4h_error ("h4h_malloc_atomic failed");
		return NULL;
	}

	/* initialize h4h_thread_t */
	k->user_threadfn = user_threadfn;
	k->user_data = (void*)user_data;
	k->wait = NULL;
	h4h_mutex_init (&k->thread_done);
	init_waitqueue_head (&k->wq);
	if ((k->thread = kthread_create (
			h4h_thread_fn, (void*)k, name)) == NULL) {
		h4h_error ("kthread_create failed");
		h4h_free_atomic (k);
		return NULL;
	} 

	return k;
}

int h4h_thread_run (h4h_thread_t* k)
{
	/* wake up thread! */
	wake_up_process (k->thread);

	return 0;
}

int h4h_thread_schedule (h4h_thread_t* k)
{
	if (k == NULL || k->wait == NULL) {
		h4h_error ("oops! k or k->wait is NULL (%p, %p)", k, k->wait);
		return 0;
	}

	add_wait_queue (&k->wq, k->wait);
	set_current_state (TASK_INTERRUPTIBLE);
	schedule (); /* go to sleep */
	remove_wait_queue (&k->wq, k->wait);

	if (signal_pending (current)) {
		/* get a kill signal */
		return SIGKILL;
	}

	return 0;
}

void h4h_thread_schedule_setup (h4h_thread_t* k)
{	
	add_wait_queue (&k->wq, k->wait);
	set_current_state (TASK_INTERRUPTIBLE);
}

void h4h_thread_schedule_cancel (h4h_thread_t* k)
{
	/* FIXME: I'm not sure wether or not to remove wait from queue */
	remove_wait_queue (&k->wq, k->wait);
}

int h4h_thread_schedule_sleep (h4h_thread_t* k)
{
	schedule (); /* go to sleep */
	remove_wait_queue (&k->wq, k->wait);

	if (signal_pending (current)) {
		/* get a kill signal */
		return SIGKILL;
	}

	return 0;
}

void h4h_thread_wakeup (h4h_thread_t* k)
{
	if (k == NULL) {
		h4h_error ("oops! k is NULL");
		return;
	}

	wake_up_interruptible (&k->wq);
}

void h4h_thread_stop (h4h_thread_t* k)
{
	if (k == NULL) {
		h4h_error ("oops! k is NULL");
		return;
	}

	/* send a KILL signal to the thread */
	send_sig (SIGKILL, k->thread, 0);
	h4h_mutex_lock (&k->thread_done);

	/* free h4h_thread_t */
	h4h_free_atomic (k);
}

void h4h_thread_msleep (uint32_t ms) 
{
	msleep (ms);
}

void h4h_thread_yield ()
{
	yield ();
}

#endif /* KERNEL_MODE */


#if defined(USER_MODE)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

#include <inttypes.h>
#include <pthread.h>
#include <string.h>

void h4h_thread_fn (void *data) 
{
	h4h_thread_t* k = (h4h_thread_t*)data;

	/* initialize a kernel thread */
	h4h_mutex_lock (&k->thread_done);
	
	/* invoke a user call-back function */
	k->user_threadfn (k->user_data);

	/* a thread stops */
	h4h_mutex_unlock (&k->thread_done);

	pthread_exit (0);
};

h4h_thread_t* h4h_thread_create (
	int (*user_threadfn)(void *data), 
	void* user_data, 
	char* name)
{
	h4h_thread_t* k = NULL;

	/* create h4h_thread_t */
	if ((k = (h4h_thread_t*)h4h_malloc_atomic (
			sizeof (h4h_thread_t))) == NULL) {
		h4h_error ("h4h_malloc_atomic failed");
		return NULL;
	}

	/* initialize h4h_thread_t */
	k->user_threadfn = user_threadfn;
	k->user_data = (void*)user_data;
	h4h_mutex_init (&k->thread_done);
	h4h_mutex_init (&k->thread_sleep);
	pthread_cond_init (&k->thread_con, NULL);

	h4h_msg ("new thread created: %p", k);

	return k;
}

int h4h_thread_run (h4h_thread_t* k)
{
	return pthread_create (&k->thread, NULL, (void*)&h4h_thread_fn, (void*)k);
}

int h4h_thread_schedule (h4h_thread_t* k)
{
	int ret = 0;
#ifdef PTHREAD_TIMEOUT
	struct timespec ts;

	/* setup waiting time */
	clock_gettime (CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
#endif

	if (k == NULL) {
		h4h_warning ("k is NULL");
		return 0;
	}

	/* sleep until wake-up signal */
	if ((ret = h4h_mutex_lock (&k->thread_sleep)) == 0) {
		/* FIXME: need to fix a time-out bug that occasionally occurs in an exceptional case */
#ifdef PTHREAD_TIMEOUT
		if ((ret = pthread_cond_timedwait 
				(&k->thread_con, &k->thread_sleep, &ts)) != 0) {
			h4h_warning ("pthread timeout: %u %s", ret, strerror (ret));
		}
#else
		if ((ret = pthread_cond_wait
				(&k->thread_con, &k->thread_sleep)) != 0) {
			h4h_warning ("pthread timeout: %u %s", ret, strerror (ret));
		}
#endif
		h4h_mutex_unlock (&k->thread_sleep);
	} else {
		h4h_warning ("pthread lock failed: %u %s", ret, strerror (ret));
	}

	return 0;
}

void h4h_thread_schedule_setup (h4h_thread_t* k)
{	
	int ret = 0;
	if ((ret = h4h_mutex_lock (&k->thread_sleep)) != 0) {
		h4h_warning ("pthread lock failed: %u %s", ret, strerror (ret));
	}
}

void h4h_thread_schedule_cancel (h4h_thread_t* k)
{
	h4h_mutex_unlock (&k->thread_sleep);
}

int h4h_thread_schedule_sleep (h4h_thread_t* k)
{
	int ret = 0;
#ifdef PTHREAD_TIMEOUT
	struct timespec ts;

	/* setup waiting time */
	clock_gettime (CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

	if ((ret = pthread_cond_timedwait 
			(&k->thread_con, &k->thread_sleep, &ts)) != 0) {
		h4h_warning ("pthread timeout: %u %s", ret, strerror (ret));
	}
#else
	if ((ret = pthread_cond_wait 
			(&k->thread_con, &k->thread_sleep)) != 0) {
		h4h_warning ("pthread timeout: %u %s", ret, strerror (ret));
	}
#endif

	h4h_mutex_unlock (&k->thread_sleep);

	return ret;
}

void h4h_thread_wakeup (h4h_thread_t* k)
{
	int ret = 0;

	if (k == NULL) {
		h4h_warning ("k is NULL");
		return;
	}

	/* send a wake-up signal */
	if ((ret = h4h_mutex_try_lock (&k->thread_sleep)) == 1) {
		pthread_cond_signal (&k->thread_con);
		h4h_mutex_unlock (&k->thread_sleep);
	} else {
		/*h4h_warning ("pthread lock failed: %u %s", ret, strerror (ret));*/
	}
}

void h4h_thread_stop (h4h_thread_t* k)
{
	int ret;

	if (k == NULL) {
		h4h_warning ("k is NULL");
		return;
	}

	h4h_msg ("thread destroyed: %p", k);

	/* send a kill signal */
	if ((ret = pthread_cancel (k->thread)) != 0)
		h4h_msg ("pthread_cancel is %d", ret);

	if ((ret = pthread_join (k->thread, NULL)) != 0)
		h4h_msg ("pthread_join is %d", ret);

	/* clean up */
	h4h_mutex_free (&k->thread_done);
	h4h_mutex_free (&k->thread_sleep);
	pthread_cond_destroy (&k->thread_con);
	h4h_free_atomic (k);
}

void h4h_thread_msleep (uint32_t ms) 
{
	int microsecs;
	struct timeval tv;
	microsecs = ms * 1000;
	tv.tv_sec  = microsecs / 1000000;
	tv.tv_usec = microsecs % 1000000;
	select (0, NULL, NULL, NULL, &tv);  
}

void h4h_thread_yield (void)
{
	pthread_yield ();
}

#endif

