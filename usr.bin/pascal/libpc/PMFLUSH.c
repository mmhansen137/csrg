/* Copyright (c) 1979 Regents of the University of California */

static char sccsid[] = "@(#)PMFLUSH.c 1.2 01/13/81";

#include "h00vars.h"

PMFLUSH(cntrs, rtns, bufaddr)

	long cntrs;	/* total number of counters (stmt + routine) */
	long rtns;	/* number of func and proc counters */
	long *bufaddr;	/* address of count buffers */
{
	register FILE	*filep;

	bufaddr[0] = 0426;
	bufaddr[1] = time();
	bufaddr[2] = cntrs;
	bufaddr[3] = rtns;
	filep = fopen(PXPFILE, "w");
	if (filep == NULL)
		goto ioerr;
	fwrite(bufaddr, cntrs + 1, sizeof(long), filep);
	if (ferror(filep))
		goto ioerr;
	fclose(filep);
	if (!ferror(filep))
		return;
ioerr:
	perror(PXPFILE);
}
