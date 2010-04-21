/*
 * Copyright (c) 2009, 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <machine/atomic.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/spinlock2.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <machine/varargs.h>
#include <machine/param.h>

#include <dsched/fq/dsched_fq.h>

MALLOC_DECLARE(M_DSCHEDFQ);

dsched_new_buf_t	fq_new_buf;
dsched_new_proc_t	fq_new_proc;
dsched_new_thread_t	fq_new_thread;
dsched_exit_buf_t	fq_exit_buf;
dsched_exit_proc_t	fq_exit_proc;
dsched_exit_thread_t	fq_exit_thread;

extern struct dsched_fq_stats	fq_stats;

void
fq_new_buf(struct buf *bp)
{
	struct fq_thread_ctx	*tdctx = NULL;

	if (curproc != NULL) {
		tdctx = dsched_get_proc_priv(curproc);
	} else {
		/* This is a kernel thread, so no proc info is available */
		tdctx = dsched_get_thread_priv(curthread);
	}

#if 0
	/*
	 * XXX: hack. we don't want this assert because we aren't catching all
	 *	threads. mi_startup() is still getting away without an tdctx.
	 */

	/* by now we should have an tdctx. if not, something bad is going on */
	KKASSERT(tdctx != NULL);
#endif

	if (tdctx) {
		fq_thread_ctx_ref(tdctx);
	}
	dsched_set_buf_priv(bp, tdctx);
	
}

void
fq_exit_buf(struct buf *bp)
{
	struct fq_thread_ctx	*tdctx;

	tdctx = dsched_get_buf_priv(bp);
	if (tdctx != NULL) {
		dsched_clr_buf_priv(bp);
		fq_thread_ctx_unref(tdctx);
	}
}

void
fq_new_proc(struct proc *p)
{
	struct fq_thread_ctx	*tdctx;

	KKASSERT(p != NULL);

	tdctx = fq_thread_ctx_alloc(p);
	fq_thread_ctx_ref(tdctx);
	dsched_set_proc_priv(p, tdctx);
	atomic_add_int(&fq_stats.nprocs, 1);
	tdctx->p = p;
}

void
fq_new_thread(struct thread *td)
{
	struct fq_thread_ctx	*tdctx;

	KKASSERT(td != NULL);

	tdctx = fq_thread_ctx_alloc(NULL);
	fq_thread_ctx_ref(tdctx);
	dsched_set_thread_priv(td, tdctx);
	atomic_add_int(&fq_stats.nthreads, 1);
	tdctx->td = td;
}

void
fq_exit_proc(struct proc *p)
{
	struct fq_thread_ctx	*tdctx;

	KKASSERT(p != NULL);

	tdctx = dsched_get_proc_priv(p);
	KKASSERT(tdctx != NULL);
#if 0
	kprintf("exit_proc: tdctx = %p\n", tdctx);
#endif
	tdctx->dead = 0x1337;
	dsched_set_proc_priv(p, 0);
	fq_thread_ctx_unref(tdctx); /* one for alloc, */
	fq_thread_ctx_unref(tdctx); /* one for ref */
	atomic_subtract_int(&fq_stats.nprocs, 1);
}

void
fq_exit_thread(struct thread *td)
{
	struct fq_thread_ctx	*tdctx;

	KKASSERT(td != NULL);

	tdctx = dsched_get_thread_priv(td);
	KKASSERT(tdctx != NULL);
#if 0
	kprintf("exit_thread: tdctx = %p\n", tdctx);
#endif
	tdctx->dead = 0x1337;
	dsched_set_thread_priv(td, 0);
	fq_thread_ctx_unref(tdctx); /* one for alloc, */
	fq_thread_ctx_unref(tdctx); /* one for ref */
	atomic_subtract_int(&fq_stats.nthreads, 1);
}