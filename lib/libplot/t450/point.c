#ifndef lint
static char sccsid[] = "@(#)point.c	4.1 (Berkeley) 06/27/83";
#endif

point(xi,yi){
		move(xi,yi);
		label(".");
		return;
}
