/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah $Hdr: hpux_compat.c 1.33 89/08/23$
 *
 *	@(#)hpux_sig.c	7.2 (Berkeley) 06/08/90
 */

/*
 * Signal related HPUX compatibility routines
 */

#ifdef HPUXCOMPAT

#include "param.h"
#include "systm.h"
#include "user.h"
#include "kernel.h"
#include "proc.h"
#include "hpux.h"

/* indexed by HPUX signal number - 1 */
char hpuxtobsdsigmap[NSIG] = {
/*01*/	SIGHUP,  SIGINT, SIGQUIT, SIGILL,   SIGTRAP, SIGIOT,  SIGEMT,   SIGFPE,
/*09*/  SIGKILL, SIGBUS, SIGSEGV, SIGSYS,   SIGPIPE, SIGALRM, SIGTERM,  SIGUSR1,
/*17*/  SIGUSR2, SIGCHLD, 0,      SIGVTALRM,SIGPROF, SIGIO,   SIGWINCH, SIGSTOP,
/*25*/	SIGTSTP, SIGCONT,SIGTTIN, SIGTTOU,  SIGURG,  0,       0,        0
};

/* indexed by BSD signal number - 1 */
char bsdtohpuxsigmap[NSIG] = {
/*01*/	 1,  2,  3,  4,  5,  6,  7,  8,
/*09*/   9, 10, 11, 12, 13, 14, 15, 29,
/*17*/  24, 25, 26, 18, 27, 28, 22,  0,
/*25*/	 0, 20, 21, 23,  0, 16, 17,  0
};

/*
 * XXX: In addition to mapping the signal number we also have
 * to see if the "old" style signal mechinism is needed.
 * If so, we set the OUSIG flag.  This is not really correct
 * as under HP-UX "old" style handling can be set on a per
 * signal basis and we are setting it for all signals in one
 * swell foop.  I suspect we can get away with this since I
 * doubt any program of interest mixes the two semantics.
 */
hpuxsigvec()
{
	register struct a {
		int	signo;
		struct	sigvec *nsv;
		struct	sigvec *osv;
	} *uap = (struct a  *)u.u_ap;
	struct sigvec vec;
	register struct sigvec *sv;
	register int sig;
	int bit;

	sig = hpuxtobsdsig(uap->signo);
	if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
		u.u_error = EINVAL;
		return;
	}
	sv = &vec;
	if (uap->osv) {
		sv->sv_handler = u.u_signal[sig];
		sv->sv_mask = u.u_sigmask[sig];
		bit = sigmask(sig);
		sv->sv_flags = 0;
		if ((u.u_sigonstack & bit) != 0)
			sv->sv_flags |= SV_ONSTACK;
		if ((u.u_sigintr & bit) != 0)
			sv->sv_flags |= SV_INTERRUPT;
#if 0
/* XXX -- SOUSIG no longer exists, do something here */
		if (u.u_procp->p_flag & SOUSIG)
			sv->sv_flags |= HPUXSV_RESET;		/* XXX */
#endif
		u.u_error =
		    copyout((caddr_t)sv, (caddr_t)uap->osv, sizeof (vec));
		if (u.u_error)
			return;
	}
	if (uap->nsv) {
		u.u_error =
		    copyin((caddr_t)uap->nsv, (caddr_t)sv, sizeof (vec));
		if (u.u_error)
			return;
		if (sig == SIGCONT && sv->sv_handler == SIG_IGN) {
			u.u_error = EINVAL;
			return;
		}
		setsigvec(u.u_procp, sig, (struct sigaction *)sv);
#if 0
/* XXX -- SOUSIG no longer exists, do something here */
		if (sv->sv_flags & HPUXSV_RESET)
			u.u_procp->p_flag |= SOUSIG;		/* XXX */
#endif
	}
}

hpuxsigblock()
{
	struct a {
		int	mask;
	} *uap = (struct a *)u.u_ap;

	(void) splhigh();
	u.u_r.r_val1 = bsdtohpuxmask(u.u_procp->p_sigmask);
	u.u_procp->p_sigmask |= hpuxtobsdmask(uap->mask) &~ sigcantmask;
	(void) spl0();
}

hpuxsigsetmask()
{
	struct a {
		int	mask;
	} *uap = (struct a *)u.u_ap;

	(void) splhigh();
	u.u_r.r_val1 = bsdtohpuxmask(u.u_procp->p_sigmask);
	u.u_procp->p_sigmask = hpuxtobsdmask(uap->mask) &~ sigcantmask;
	(void) spl0();
}

hpuxsigpause()
{
	struct a {
		int	mask;
	} *uap = (struct a *)u.u_ap;

	uap->mask = hpuxtobsdmask(uap->mask);
	sigsuspend();
}

/* not totally correct, but close enuf' */
hpuxkill()
{
	struct a {
		int	pid;
		int	signo;
	} *uap = (struct a *)u.u_ap;

	if (uap->signo) {
		uap->signo = hpuxtobsdsig(uap->signo);
		if (uap->signo == 0)
			uap->signo = NSIG;
	}
	kill();
}

ohpuxssig()
{
	struct a {
		int	signo;
		sig_t	fun;
	} *uap = (struct a *)u.u_ap;
	register int a;
	struct sigvec vec;
	register struct sigvec *sv = &vec;
	struct proc *p = u.u_procp;

	a = hpuxtobsdsig(uap->signo);
	sv->sv_handler = uap->fun;
	/*
	 * Kill processes trying to use job control facilities
	 * (this'll help us find any vestiges of the old stuff).
	 */
	if ((a &~ 0377) ||
	    (sv->sv_handler != SIG_DFL && sv->sv_handler != SIG_IGN &&
	     ((int)sv->sv_handler) & 1)) {
		psignal(p, SIGSYS);
		return;
	}
	if (a <= 0 || a >= NSIG || a == SIGKILL || a == SIGSTOP ||
	    a == SIGCONT && sv->sv_handler == SIG_IGN) {
		u.u_error = EINVAL;
		return;
	}
	sv->sv_mask = 0;
	sv->sv_flags = SV_INTERRUPT;
	u.u_r.r_val1 = (int)u.u_signal[a];
	setsigvec(u.u_procp, a, (struct sigaction *)sv);
#if 0
	p->p_flag |= SOUSIG;		/* mark as simulating old stuff */
#endif
}

/* signal numbers: convert from HPUX to BSD */
hpuxtobsdsig(sig)
	register int sig;
{
	if (--sig < 0 || sig >= NSIG)
		return(0);
	return((int)hpuxtobsdsigmap[sig]);
}

/* signal numbers: convert from BSD to HPUX */
bsdtohpuxsig(sig)
	register int sig;
{
	if (--sig < 0 || sig >= NSIG)
		return(0);
	return((int)bsdtohpuxsigmap[sig]);
}

/* signal masks: convert from HPUX to BSD (not pretty or fast) */
hpuxtobsdmask(mask)
	register int mask;
{
	register int nmask, sig, nsig;

	if (mask == 0 || mask == -1)
		return(mask);
	nmask = 0;
	for (sig = 1; sig < NSIG; sig++)
		if ((mask & sigmask(sig)) && (nsig = hpuxtobsdsig(sig)))
			nmask |= sigmask(nsig);
	return(nmask);
}

bsdtohpuxmask(mask)
	register int mask;
{
	register int nmask, sig, nsig;

	if (mask == 0 || mask == -1)
		return(mask);
	nmask = 0;
	for (sig = 1; sig < NSIG; sig++)
		if ((mask & sigmask(sig)) && (nsig = bsdtohpuxsig(sig)))
			nmask |= sigmask(nsig);
	return(nmask);
}
#endif
