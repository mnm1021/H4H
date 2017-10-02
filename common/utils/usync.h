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

#ifndef _H4H_SYNC_H
#define _H4H_SYNC_H

#if defined(KERNEL_MODE)

/* completion lock */
#include <linux/version.h>
#include <linux/completion.h>
#define h4h_completion_t struct completion
#define	h4h_init_completion(a)	init_completion(&a)
#define h4h_wait_for_completion(a)	wait_for_completion(&a)
#define h4h_try_wait_for_completion(a) try_wait_for_completion(&a)
#define h4h_complete(a) complete(&a)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,12,0)
#define h4h_reinit_completion(a) INIT_COMPLETION(a)
#else
#define	h4h_reinit_completion(a) reinit_completion(&a)
#endif

/* synchronization (semaphore) */
#include <linux/semaphore.h>
#define h4h_sema_t struct semaphore
#define h4h_sema_init(a) sema_init (a, 1)
#define h4h_sema_lock(a) down (a)
#define h4h_sema_lock_interruptible(a) down_interruptible(a)
#define h4h_sema_unlock(a) up (a)
#define h4h_sema_try_lock(a) ({  /* 0: busy, 1: idle */ \
	int z = down_trylock(a); int ret; \
	if (z == 0) ret = 1; \
	else ret = 0; \
	ret; })
#define h4h_sema_free(a)

/* synchronization (mutex) 
 * NOTE: simply reuse semaphore; it should be replaced with kernel mutex later */
#define h4h_mutex_t h4h_sema_t
#define h4h_mutex_init(a) h4h_sema_init(a)
#define h4h_mutex_lock(a) h4h_sema_lock(a)
#define h4h_mutex_lock_interruptible(a) h4h_sema_lock_interruptible(a)
#define h4h_mutex_unlock(a) h4h_sema_unlock(a)
#define h4h_mutex_try_lock(a) h4h_sema_try_lock(a)
#define h4h_mutex_free(a) h4h_sema_free(a)

/* spinlock */
#define h4h_spinlock_t spinlock_t
#define h4h_spin_lock_init(a) spin_lock_init(a)
#define h4h_spin_lock(a) spin_lock(a)
#define h4h_spin_lock_irqsave(a,flag) spin_lock_irqsave(a,flag)
#define h4h_spin_unlock(a) spin_unlock(a)
#define h4h_spin_unlock_irqrestore(a,flag) spin_unlock_irqrestore(a,flag)
#define h4h_spin_lock_destory(a)


#elif defined(USER_MODE) 


/* spinlock */
#include <pthread.h>
#define h4h_spinlock_t pthread_spinlock_t
#define h4h_spin_lock_init(a) pthread_spin_init(a,0)
#define h4h_spin_lock(a) pthread_spin_lock(a)
#define h4h_spin_lock_irqsave(a,flag) pthread_spin_lock(a);
#define h4h_spin_unlock(a) pthread_spin_unlock(a)
#define h4h_spin_unlock_irqrestore(a,flag) pthread_spin_unlock(a)
#define h4h_spin_lock_destory(a) pthread_spin_destroy(a)

/* synchronization (semaphore) */
#include <semaphore.h>  /* Semaphore */
#define h4h_sema_t sem_t 
#define h4h_sema_init(a) sem_init(a, 0, 1)
#define h4h_sema_lock(a) sem_wait(a)
#define h4h_sema_lock_interruptible(a) sem_wait(a)
#define h4h_sema_unlock(a) sem_post(a)
#define h4h_sema_try_lock(a) ({ /* 0: busy, 1: idle */ \
	int z = sem_trywait(a); int ret; \
	if (z == 0) ret = 1; \
	else ret = 0; \
	ret; })
#define h4h_sema_free(a) sem_destroy(a)

/* synchronization (mutex) */
#define h4h_mutex_t pthread_mutex_t 
#define h4h_mutex_init(a) pthread_mutex_init(a, NULL)
#define h4h_mutex_lock(a) pthread_mutex_lock(a)
#define h4h_mutex_lock_interruptible(a) pthread_mutex_lock(a)
#define h4h_mutex_unlock(a) pthread_mutex_unlock(a)
#define h4h_mutex_try_lock(a) ({ /* 0: busy, 1: idle */ \
	int z = pthread_mutex_trylock(a); int ret; \
	if (z == 0) ret = 1; \
	else ret = 0; \
	ret; })
#define h4h_mutex_free(a) pthread_mutex_destroy(a)

#else
/* ERROR CASE */
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

#endif /* _H4H_SYNC_H */ 
