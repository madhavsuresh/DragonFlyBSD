/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/usr.bin/make/globals.h,v 1.16 2005/08/03 19:48:44 okumoto Exp $
 */

#ifndef globals_h_1c1edb96
#define	globals_h_1c1edb96

/*
 * Global Variables
 */

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#include "lst.h"

struct GNode;
struct Path;

/* The list of directories to search when looking for targets */
extern struct Path dirSearchPath;

extern int	jobLimit;	/* -j argument: maximum number of jobs */
extern bool	jobsRunning;	/* True if jobs are running */
extern bool	compatMake;	/* True if we are make compatible */
extern bool	ignoreErrors;	/* True if should ignore all errors */
extern bool	beSilent;	/* True if should print no commands */
extern bool	beVerbose;	/* True if should print extra cruft */
extern bool	noExecute;	/* True if should execute nothing */
extern bool	allPrecious;	/* True if every target is precious */

/* True if should continue on unaffected portions of the graph
 * when have an error in one portion */
extern bool	keepgoing;

/* true if targets should just be 'touched'if out of date. Set by the -t flag */
extern bool	touchFlag;

/* true if should capture the output of subshells by means of pipes.
 * Otherwise it is routed to temporary files from which it is retrieved
 * when the shell exits */
extern bool	usePipes;

/* List of specific variables for which the environment should be
 * searched before the global context */
extern Lst envFirstVars;

extern struct GNode	*DEFAULT;	/* .DEFAULT rule */

/* The time at the start of this whole process */
extern time_t	now;

extern int debug;

/* warning flags */
extern uint32_t warn_cmd;	/* positive warning flags on command line */
extern uint32_t warn_nocmd;	/* negative warning flags on command line */
extern uint32_t warn_flags;	/* current warning flags */

#endif /* globals_h_1c1edb96 */
