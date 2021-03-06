/*
 * Copyright (c) 1993, 1994 by Chris Provenzano, proven@mit.edu
 * Copyright (c) 1995-1998 by John Birrell <jb@cimlogic.com.au>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Chris Provenzano.
 * 4. The name of Chris Provenzano may not be used to endorse or promote 
 *	  products derived from this software without specific prior written
 *	  permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/include/pthread.h,v 1.20.2.4 2003/05/27 18:18:01 jdp Exp $
 */
#ifndef _PTHREAD_H_
#define _PTHREAD_H_

/*
 * Header files.
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <limits.h>
#include <sys/sched.h>

/*
 * Run-time invariant values:
 */
#define PTHREAD_DESTRUCTOR_ITERATIONS		4
#define PTHREAD_KEYS_MAX			256
#define PTHREAD_STACK_MIN			1024
#define PTHREAD_THREADS_MAX			ULONG_MAX
#define PTHREAD_BARRIER_SERIAL_THREAD		-1

/*
 * Flags for threads and thread attributes.
 */
#define PTHREAD_DETACHED            0x1
#define PTHREAD_SCOPE_SYSTEM        0x2
#define PTHREAD_INHERIT_SCHED       0x4
#define PTHREAD_NOFLOAT             0x8

#define PTHREAD_CREATE_DETACHED     PTHREAD_DETACHED
#define PTHREAD_CREATE_JOINABLE     0
#define PTHREAD_SCOPE_PROCESS       0
#define PTHREAD_EXPLICIT_SCHED      0

/*
 * Flags for read/write lock attributes
 */
#define PTHREAD_PROCESS_PRIVATE     0
#define PTHREAD_PROCESS_SHARED      1	

/*
 * Flags for cancelling threads
 */
#define PTHREAD_CANCEL_ENABLE		0
#define PTHREAD_CANCEL_DISABLE		1
#define PTHREAD_CANCEL_DEFERRED		0
#define PTHREAD_CANCEL_ASYNCHRONOUS	2
#define PTHREAD_CANCELED		((void *) 1)

/*
 * Flags for once initialization.
 */
#define PTHREAD_NEEDS_INIT  0
#define PTHREAD_DONE_INIT   1

/*
 * Static once initialization values. 
 */
#define PTHREAD_ONCE_INIT   { PTHREAD_NEEDS_INIT, NULL }

/*
 * Static initialization values. 
 */
#define PTHREAD_MUTEX_INITIALIZER	NULL
#define PTHREAD_COND_INITIALIZER	NULL
#define PTHREAD_RWLOCK_INITIALIZER	NULL

/*
 * Default attribute arguments (draft 4, deprecated).
 */
#ifndef PTHREAD_KERNEL
#define pthread_condattr_default    NULL
#define pthread_mutexattr_default   NULL
#define pthread_attr_default        NULL
#endif

#define PTHREAD_PRIO_NONE	0
#define PTHREAD_PRIO_INHERIT	1
#define PTHREAD_PRIO_PROTECT	2

/*
 * Mutex types (Single UNIX Specification, Version 2, 1997).
 *
 * Note that a mutex attribute with one of the following types:
 *
 *	PTHREAD_MUTEX_NORMAL
 *	PTHREAD_MUTEX_RECURSIVE
 *      MUTEX_TYPE_FAST (deprecated)
 *	MUTEX_TYPE_COUNTING_FAST (deprecated)
 *
 * will deviate from POSIX specified semantics.
 */
enum pthread_mutextype {
	PTHREAD_MUTEX_ERRORCHECK	= 1,	/* Default POSIX mutex */
	PTHREAD_MUTEX_RECURSIVE		= 2,	/* Recursive mutex */
	PTHREAD_MUTEX_NORMAL		= 3,	/* No error checking */
	MUTEX_TYPE_MAX
};

#define PTHREAD_MUTEX_DEFAULT		PTHREAD_MUTEX_ERRORCHECK
#define MUTEX_TYPE_FAST			PTHREAD_MUTEX_NORMAL
#define MUTEX_TYPE_COUNTING_FAST	PTHREAD_MUTEX_RECURSIVE

/*
 * Thread function prototype definitions:
 */
__BEGIN_DECLS
int		pthread_atfork(void (*)(void), void (*)(void), void (*)(void)) __exported;
int		pthread_attr_destroy(pthread_attr_t *) __exported;
int		pthread_attr_getguardsize(const pthread_attr_t * __restrict,
			size_t *) __exported;
int		pthread_attr_getstack(const pthread_attr_t * __restrict,
				      void ** __restrict, size_t * __restrict) __exported;
int		pthread_attr_getstacksize(const pthread_attr_t *, size_t *) __exported;
int		pthread_attr_getstackaddr(const pthread_attr_t *, void **) __exported;
int		pthread_attr_getdetachstate(const pthread_attr_t *, int *) __exported;
int		pthread_attr_init(pthread_attr_t *) __exported;
int		pthread_attr_setguardsize(pthread_attr_t *, size_t) __exported;
int		pthread_attr_setstack(pthread_attr_t *, void *, size_t) __exported;
int		pthread_attr_setstacksize(pthread_attr_t *, size_t) __exported;
int		pthread_attr_setstackaddr(pthread_attr_t *, void *) __exported;
int		pthread_attr_setdetachstate(pthread_attr_t *, int) __exported;
int		pthread_barrier_destroy(pthread_barrier_t *) __exported;
int		pthread_barrier_init(pthread_barrier_t *,
			const pthread_barrierattr_t *, unsigned) __exported;
int		pthread_barrier_wait(pthread_barrier_t *) __exported;
int		pthread_barrierattr_destroy(pthread_barrierattr_t *) __exported;
int		pthread_barrierattr_init(pthread_barrierattr_t *) __exported;
int		pthread_barrierattr_getpshared(const pthread_barrierattr_t *,
			int *) __exported;
int		pthread_barrierattr_setpshared(pthread_barrierattr_t *, int) __exported;
void		pthread_cleanup_pop(int) __exported;
void		pthread_cleanup_push(void (*) (void *), void *) __exported;
int		pthread_condattr_destroy(pthread_condattr_t *) __exported;
int		pthread_condattr_init(pthread_condattr_t *) __exported;

int		pthread_condattr_getpshared(const pthread_condattr_t *, int *) __exported;
int		pthread_condattr_setpshared(pthread_condattr_t *, int) __exported;

int		pthread_condattr_getclock(const pthread_condattr_t *, clockid_t *) __exported;
int		pthread_condattr_setclock(pthread_condattr_t *, clockid_t) __exported;

int		pthread_cond_broadcast(pthread_cond_t *) __exported;
int		pthread_cond_destroy(pthread_cond_t *) __exported;
int		pthread_cond_init(pthread_cond_t *,
				  const pthread_condattr_t *) __exported;
int		pthread_cond_signal(pthread_cond_t *) __exported;
int		pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
				       const struct timespec *) __exported;
int		pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *) __exported;
int		pthread_create(pthread_t *, const pthread_attr_t *,
			       void *(*) (void *), void *) __exported;
int		pthread_detach(pthread_t) __exported;
int		pthread_equal(pthread_t, pthread_t) __exported;
void		pthread_exit(void *) __dead2;
void		*pthread_getspecific(pthread_key_t) __exported;
int		pthread_join(pthread_t, void **) __exported;
int		pthread_key_create(pthread_key_t *, void (*) (void *)) __exported;
int		pthread_key_delete(pthread_key_t) __exported;
int		pthread_mutexattr_init(pthread_mutexattr_t *) __exported;
int		pthread_mutexattr_destroy(pthread_mutexattr_t *) __exported;
int		pthread_mutexattr_gettype(pthread_mutexattr_t *, int *) __exported;
int		pthread_mutexattr_settype(pthread_mutexattr_t *, int) __exported;
int		pthread_mutex_destroy(pthread_mutex_t *) __exported;
int		pthread_mutex_init(pthread_mutex_t *,
				   const pthread_mutexattr_t *) __exported;
int		pthread_mutex_lock(pthread_mutex_t *) __exported;
int		pthread_mutex_timedlock(pthread_mutex_t *,
					const struct timespec *) __exported;
int		pthread_mutex_trylock(pthread_mutex_t *) __exported;
int		pthread_mutex_unlock(pthread_mutex_t *) __exported;
int		pthread_once(pthread_once_t *, void (*) (void)) __exported;
int		pthread_rwlock_destroy(pthread_rwlock_t *) __exported;
int		pthread_rwlock_init(pthread_rwlock_t *,
				    const pthread_rwlockattr_t *) __exported;
int		pthread_rwlock_rdlock(pthread_rwlock_t *) __exported;
int		pthread_rwlock_timedrdlock(pthread_rwlock_t *,
				    const struct timespec *) __exported;
int		pthread_rwlock_tryrdlock(pthread_rwlock_t *) __exported;
int		pthread_rwlock_trywrlock(pthread_rwlock_t *) __exported;
int		pthread_rwlock_unlock(pthread_rwlock_t *) __exported;
int		pthread_rwlock_wrlock(pthread_rwlock_t *) __exported;
int		pthread_rwlock_timedwrlock(pthread_rwlock_t *,
				    const struct timespec *) __exported;
int		pthread_rwlockattr_init(pthread_rwlockattr_t *) __exported;
int		pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *,
					      int *) __exported;
int		pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int) __exported;
int		pthread_rwlockattr_destroy(pthread_rwlockattr_t *) __exported;
pthread_t	pthread_self(void) __exported;
int		pthread_setspecific(pthread_key_t, const void *) __exported;

int		pthread_spin_destroy(pthread_spinlock_t *) __exported;
int		pthread_spin_init(pthread_spinlock_t *, int) __exported;
int		pthread_spin_lock(pthread_spinlock_t *) __exported;
int		pthread_spin_trylock(pthread_spinlock_t *) __exported;
int		pthread_spin_unlock(pthread_spinlock_t *) __exported;

int		pthread_cancel(pthread_t) __exported;
int		pthread_setcancelstate(int, int *) __exported;
int		pthread_setcanceltype(int, int *) __exported;
void		pthread_testcancel(void) __exported;

int		pthread_getprio(pthread_t) __exported;
int		pthread_setprio(pthread_t, int) __exported;
void		pthread_yield(void) __exported;

int		pthread_mutexattr_getpshared(const pthread_mutexattr_t *, int *) __exported;
int		pthread_mutexattr_setpshared(pthread_mutexattr_t *, int) __exported;

int		pthread_mutexattr_getprioceiling(pthread_mutexattr_t *, int *) __exported;
int		pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int) __exported;
int		pthread_mutex_getprioceiling(pthread_mutex_t *, int *) __exported;
int		pthread_mutex_setprioceiling(pthread_mutex_t *, int, int *) __exported;

int		pthread_mutexattr_getprotocol(pthread_mutexattr_t *, int *) __exported;
int		pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int) __exported;

int		pthread_attr_getinheritsched(const pthread_attr_t *, int *) __exported;
int		pthread_attr_getschedparam(const pthread_attr_t *,
					   struct sched_param *) __exported;
int		pthread_attr_getschedpolicy(const pthread_attr_t *, int *) __exported;
int		pthread_attr_getscope(const pthread_attr_t *, int *) __exported;
int		pthread_attr_setinheritsched(pthread_attr_t *, int) __exported;
int		pthread_attr_setschedparam(pthread_attr_t *,
					   const struct sched_param *) __exported;
int		pthread_attr_setschedpolicy(pthread_attr_t *, int) __exported;
int		pthread_attr_setscope(pthread_attr_t *, int) __exported;
int		pthread_getschedparam(pthread_t pthread, int *,
				      struct sched_param *) __exported;
int		pthread_setschedparam(pthread_t, int,
				      const struct sched_param *) __exported;
int		pthread_getconcurrency(void) __exported;
int		pthread_setconcurrency(int) __exported;

int		pthread_attr_setfloatstate(pthread_attr_t *, int) __exported;
int		pthread_attr_getfloatstate(pthread_attr_t *, int *) __exported;
__END_DECLS

#endif
