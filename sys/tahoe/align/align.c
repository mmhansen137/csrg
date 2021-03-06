/*-
 * Copyright (c) 1986 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Computer Consoles Inc.
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
 *	@(#)align.c	7.1 (Berkeley) 12/06/90
 */

#include	"align.h"

/*
 * Part of the parameter list is set up by locore.s.
 * First 2 dummy variables MUST BE the first local
 * variables; leaving place for registers 0 and 1
 * which are not preserved by the current C compiler.
 * Then, the array of structures and the last_operand
 * HAVE to be in the given order, to correspond to the
 * description of process_info in 'alignment.h'. 
 */
alignment()
{
	long	space_4_Register_1;	/* register 1 */
	long	space_4_Register_0;	/* register 0 */
	struct	oprnd	space_4_decoded[4];
	long	space_4_opcode;
	long	space_4_last_operand;	/* Last operand # processed */
	long	space_4_saved_pc;
	long	space_4_saved_sp;

	register process_info *infop;

	infop = (process_info *)&space_4_saved_sp;
	saved_pc = pc;
	saved_sp = sp;	     			/* For possible exceptions */

	last_operand = -1;   /* To get the operand routine going correctly */

	opCODE = 0xff & *(char *)pc;
	pc++;
	(*Table[opCODE].routine) (infop);	/* Call relevant handler */
	/*
	 * NOTE: nothing should follow, except the return.
	 * The register variables cannot be trusted anymore,
	 * if an exception is signalled.  See 'exception.c'
	 * to understand why.
	 */
}
