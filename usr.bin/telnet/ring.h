/*
 * Copyright (c) 1988 Regents of the University of California.
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
 *	@(#)ring.h	1.9 (Berkeley) 06/01/90
 */

/*
 * This defines a structure for a ring buffer.
 *
 * The circular buffer has two parts:
 *(((
 *	full:	[consume, supply)
 *	empty:	[supply, consume)
 *]]]
 *
 */
typedef struct {
    char	*consume,	/* where data comes out of */
    		*supply,	/* where data comes in to */
		*bottom,	/* lowest address in buffer */
		*top,		/* highest address+1 in buffer */
		*mark;		/* marker (user defined) */
    int		size;		/* size in bytes of buffer */
    u_long	consumetime,	/* help us keep straight full, empty, etc. */
		supplytime;
} Ring;

/* Here are some functions and macros to deal with the ring buffer */


#if	defined(LINT_ARGS)

/* Initialization routine */
extern int
	ring_init(Ring *ring, char *buffer, int count);

/* Data movement routines */
extern void
	ring_supply_data(Ring *ring, char *buffer, int count),
	ring_consume_data(Ring *ring, char *buffer, int count);

/* Buffer state transition routines */
extern void
	ring_supplied(Ring *ring, int count),
	ring_consumed(Ring *ring, int count);

/* Buffer state query routines */
extern int
	ring_empty_count(Ring *ring),
	ring_empty_consecutive(Ring *ring),
	ring_full_count(Ring *ring),
	ring_full_consecutive(Ring *ring);

#else /* LINT_ARGS */
extern int
	ring_init();

extern void
    ring_supply_data(),
    ring_consume_data();

extern void
    ring_supplied(),
    ring_consumed();

extern void
    ring_clear_mark(),
    ring_mark();

extern int
    ring_empty_count(),
    ring_empty_consecutive(),
    ring_full_count(),
    ring_full_consecutive();
#endif	/* defined(LINT_ARGS) */
