/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Copying or redistribution in any form is explicitly forbidden
 * unless prior written permission is obtained from William Jolitz or an
 * authorized representative of the University of California, Berkeley.
 *
 * Freely redistributable copies of this code will be available in
 * the near future; for more information contact William Jolitz or
 * the Computer Systems Research Group at the University of California,
 * Berkeley.
 *
 * The name of the University may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 *
 *	@(#)autoconf.c	5.4 (Berkeley) 11/14/90
 */

/*
 * Setup the system to run on the current machine.
 *
 * Configure() is called at boot time and initializes the vba 
 * device tables and the memory controller monitoring.  Available
 * devices are determined (from possibilities mentioned in ioconf.c),
 * and the drivers are initialized.
 */
#include "param.h"
#include "systm.h"
#include "map.h"
#include "buf.h"
#include "dkstat.h"
#include "vm.h"
#include "conf.h"
#include "dmap.h"
#include "reboot.h"

#include "machine/pte.h"

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
int	dkn;		/* number of iostat dk numbers assigned so far */
int	cold;		/* cold start flag initialized in locore.s */

/*
 * Determine i/o configuration for a machine.
 */
configure()
{
#include "isa.h"
#if NISA > 0
	isa_configure();
#endif

#if GENERICxxx
	if ((boothowto & RB_ASKNAME) == 0)
		setroot();
	setconf();
#else
	setroot();
#endif
	/*
	 * Configure swap area and related system
	 * parameter based on device(s) used.
	 */
	swapconf();
	cold = 0;
}

#if NISA > 0
#include "machine/isa/device.h"
#include "machine/isa/icu.h"
isa_configure() {
	struct isa_device *dvp;
	struct isa_driver *dp;

	splhigh();
	INTREN(IRQ_SLAVE);
	for (dvp = isa_devtab_bio; config_isadev(dvp,&biomask); dvp++);
	for (dvp = isa_devtab_tty; config_isadev(dvp,&ttymask); dvp++);
	for (dvp = isa_devtab_net; config_isadev(dvp,&netmask); dvp++);
	for (dvp = isa_devtab_null; config_isadev(dvp,0); dvp++);
#include "sl.h"
#if NSL > 0
	netmask |= ttymask;
	ttymask |= netmask;
#endif
	/* biomask |= ttymask ;  can some tty devices use buffers? */
	printf("biomask %x ttymask %x netmask %x\n", biomask, ttymask, netmask);
	splnone();
}

config_isadev(isdp, mp)
	struct isa_device *isdp;
	int *mp;
{
	struct isa_driver *dp;
 
	if (dp = isdp->id_driver) {
		if (isdp->id_maddr) {
			extern int atdevbase[];

			isdp->id_maddr -= 0xa0000;
			isdp->id_maddr += (int)&atdevbase;
		}
		isdp->id_alive = (*dp->probe)(isdp);
		if (isdp->id_alive) {
			printf("%s%d", dp->name, isdp->id_unit);
			(*dp->attach)(isdp);
			printf(" at 0x%x ", isdp->id_iobase);
			if(isdp->id_irq) {
				printf("irq %d ", ffs(isdp->id_irq)-1);
				INTREN(isdp->id_irq);
				if(mp)INTRMASK(*mp,isdp->id_irq);
			}
			if (isdp->id_drq != -1) printf("drq %d ", isdp->id_drq);
			printf("on isa0\n");
		}
		return (1);
	} else	return(0);
}
#endif

/*
 * Configure swap space and related parameters.
 */
swapconf()
{
	register struct swdevt *swp;
	register int nblks;

	for (swp = swdevt; swp->sw_dev > 0; swp++)
	{
		if ( swp->sw_dev < 0 || swp->sw_dev > nblkdev ) break;
		if (bdevsw[major(swp->sw_dev)].d_psize) {
			nblks =
			  (*bdevsw[major(swp->sw_dev)].d_psize)(swp->sw_dev);
			if (nblks != -1 &&
			    (swp->sw_nblks == 0 || swp->sw_nblks > nblks))
				swp->sw_nblks = nblks;
		}
	}
	if (dumplo == 0 && bdevsw[major(dumpdev)].d_psize)
		dumplo = (*bdevsw[major(dumpdev)].d_psize)(dumpdev) - physmem;
	if (dumplo < 0)
		dumplo = 0;
}

#define	DOSWAP			/* change swdevt, argdev, and dumpdev too */
u_long	bootdev;		/* should be dev_t, but not until 32 bits */

static	char devname[][2] = {
	'w','d',	/* 0 = wd */
	's','w',	/* 1 = sw */
	'f','d',	/* 2 = fd */
	'w','t',	/* 3 = wt */
	'x','d',	/* 4 = xd */
};

#define	PARTITIONMASK	0x7
#define	PARTITIONSHIFT	3

/*
 * Attempt to find the device from which we were booted.
 * If we can do so, and not instructed not to do so,
 * change rootdev to correspond to the load device.
 */
setroot()
{
	int  majdev, mindev, unit, part, adaptor;
	dev_t temp, orootdev;
	struct swdevt *swp;

	if (boothowto & RB_DFLTROOT ||
	    (bootdev & B_MAGICMASK) != (u_long)B_DEVMAGIC)
		return;
	majdev = (bootdev >> B_TYPESHIFT) & B_TYPEMASK;
	if (majdev > sizeof(devname) / sizeof(devname[0]))
		return;
	adaptor = (bootdev >> B_ADAPTORSHIFT) & B_ADAPTORMASK;
	part = (bootdev >> B_PARTITIONSHIFT) & B_PARTITIONMASK;
	unit = (bootdev >> B_UNITSHIFT) & B_UNITMASK;
	mindev = (unit << PARTITIONSHIFT) + part;
	orootdev = rootdev;
	rootdev = makedev(majdev, mindev);
	/*
	 * If the original rootdev is the same as the one
	 * just calculated, don't need to adjust the swap configuration.
	 */
	if (rootdev == orootdev)
		return;
	printf("changing root device to %c%c%d%c\n",
		devname[majdev][0], devname[majdev][1],
		mindev >> PARTITIONSHIFT, part + 'a');
#ifdef DOSWAP
	mindev &= ~PARTITIONMASK;
	for (swp = swdevt; swp->sw_dev; swp++) {
		if (majdev == major(swp->sw_dev) &&
		    mindev == (minor(swp->sw_dev) & ~PARTITIONMASK)) {
			temp = swdevt[0].sw_dev;
			swdevt[0].sw_dev = swp->sw_dev;
			swp->sw_dev = temp;
			break;
		}
	}
	if (swp->sw_dev == 0)
		return;
	/*
	 * If argdev and dumpdev were the same as the old primary swap
	 * device, move them to the new primary swap device.
	 */
	if (temp == dumpdev)
		dumpdev = swdevt[0].sw_dev;
	if (temp == argdev)
		argdev = swdevt[0].sw_dev;
#endif
}
