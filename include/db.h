/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	@(#)db.h	5.12 (Berkeley) 09/04/91
 */

#ifndef _DB_H_
#define	_DB_H_

#include <sys/cdefs.h>

#define	RET_ERROR	-1		/* Return values. */
#define	RET_SUCCESS	 0
#define	RET_SPECIAL	 1

#define	MAX_PAGE_NUMBER	ULONG_MAX	/* >= # of pages in a file */
typedef unsigned long	pgno_t;
#define	MAX_PAGE_OFFSET	USHRT_MAX	/* >= # of bytes in a page */
typedef unsigned short	index_t;
#define	MAX_REC_NUMBER	ULONG_MAX	/* >= # of records in a tree */
typedef unsigned long	recno_t;

/* Key/data structure -- a Data-Base Thang. */
typedef struct {
	void	*data;			/* data */
	size_t	 size;			/* data length */
} DBT;

/* Flags for DB.put() call. */
#define	R_IBEFORE	1		/* RECNO */
#define	R_IAFTER	2		/* RECNO */
#define	R_NOOVERWRITE	3		/* BTREE, HASH, RECNO */
#define	R_PUT		4		/* BTREE, HASH, RECNO */

/* Flags for DB.seq() call. */
#define	R_CURSOR	1		/* BTREE, RECNO */
#define	R_FIRST		2		/* BTREE, HASH, RECNO */
#define	R_LAST		3		/* BTREE, RECNO */
#define	R_NEXT		4		/* BTREE, HASH, RECNO */
#define	R_PREV		5		/* BTREE, RECNO */

typedef enum { DB_BTREE, DB_HASH, DB_RECNO } DBTYPE;

/* Access method description structure. */
typedef struct __db {
	DBTYPE type;		/* type of underlying db */
	void *internal;		/* access method private */
	int (*close)	__P((struct __db *));
	int (*del)	__P((const struct __db *, const DBT *, unsigned int));
	int (*get)	__P((const struct __db *, DBT *, DBT *, unsigned int));
	int (*put)	__P((const struct __db *, const DBT *, const DBT *,
			     unsigned int));
	int (*seq)	__P((const struct __db *, DBT *, DBT *, unsigned int));
	int (*sync)	__P((const struct __db *));
} DB;

#define	BTREEMAGIC	0x053162
#define	BTREEVERSION	3

/* Structure used to pass parameters to the btree routines. */
typedef struct {
#define	R_DUP		0x01	/* duplicate keys */
	u_long flags;
	int cachesize;		/* bytes to cache */
	int maxkeypage;		/* maximum keys per page */
	int minkeypage;		/* minimum keys per page */
	int psize;		/* page size */
				/* comparison, prefix functions */
	int (*compare)	__P((const DBT *, const DBT *));
	int (*prefix)	__P((const DBT *, const DBT *));
	int lorder;		/* byte order */
} BTREEINFO;

#define	HASHMAGIC	0x061561
#define	HASHVERSION	1

/* Structure used to pass parameters to the hashing routines. */
typedef struct {
	int bsize;		/* bucket size */
	int ffactor;		/* fill factor */
	int nelem;		/* number of elements */
	int cachesize;		/* bytes to cache */
	int (*hash)();		/* hash function */
	int lorder;		/* byte order */
} HASHINFO;

/* Structure used to pass parameters to the record routines. */
typedef struct {
#define	R_FIXEDLEN	0x01	/* fixed-length records */
#define	R_NOKEY		0x02	/* key not required */
#define	R_SNAPSHOT	0x04	/* snapshot the input */
	u_long flags;
	int cachesize;		/* bytes to cache */
	int lorder;		/* byte order */
	size_t reclen;		/* record length (fixed-length records) */
	u_char bval;		/* delimiting byte (variable-length records */
} RECNOINFO;

/* Key structure for the record routines. */
typedef struct {
	u_long number;
	u_long offset;
	u_long length;
#define	R_LENGTH	0x01	/* length is valid */
#define	R_NUMBER	0x02	/* record number is valid */
#define	R_OFFSET	0x04	/* offset is valid */
	u_char valid;
} RECNOKEY;

/* Little endian <--> big endian long swap macros. */
#define BLSWAP(a) { \
	u_long _tmp = a; \
	((char *)&a)[0] = ((char *)&_tmp)[3]; \
	((char *)&a)[1] = ((char *)&_tmp)[2]; \
	((char *)&a)[2] = ((char *)&_tmp)[1]; \
	((char *)&a)[3] = ((char *)&_tmp)[0]; \
}
#define	BLSWAP_COPY(a,b) { \
	((char *)&(b))[0] = ((char *)&(a))[3]; \
	((char *)&(b))[1] = ((char *)&(a))[2]; \
	((char *)&(b))[2] = ((char *)&(a))[1]; \
	((char *)&(b))[3] = ((char *)&(a))[0]; \
}

/* Little endian <--> big endian short swap macros. */
#define BSSWAP(a) { \
	u_short _tmp = a; \
	((char *)&a)[0] = ((char *)&_tmp)[1]; \
	((char *)&a)[1] = ((char *)&_tmp)[0]; \
}
#define BSSWAP_COPY(a,b) { \
	((char *)&(b))[0] = ((char *)&(a))[1]; \
	((char *)&(b))[1] = ((char *)&(a))[0]; \
}

__BEGIN_DECLS
DB *dbopen __P((const char *, int, int, DBTYPE, const void *));

#ifdef __DBINTERFACE_PRIVATE
DB	*__bt_open __P((const char *, int, int, const BTREEINFO *));
DB	*__hash_open __P((const char *, int, int, const HASHINFO *));
DB	*__rec_open __P((const char *, int, int, const RECNOINFO *));
void	 __dbpanic __P((DB *dbp));
#endif
__END_DECLS
#endif /* !_DB_H_ */
