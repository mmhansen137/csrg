/*
 * Copyright (c) 1989 The Regents of the University of California.
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
 *	@(#)ufs_lookup.c	7.47 (Berkeley) 06/25/92
 */

#include <sys/param.h>
#include <sys/namei.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

struct	nchstats nchstats;
#ifdef DIAGNOSTIC
int	dirchk = 1;
#else
int	dirchk = 0;
#endif

/*
 * Convert a component of a pathname into a pointer to a locked inode.
 * This is a very central and rather complicated routine.
 * If the file system is not maintained in a strict tree hierarchy,
 * this can result in a deadlock situation (see comments in code below).
 *
 * The cnp->cn_nameiop argument is LOOKUP, CREATE, RENAME, or DELETE depending
 * on whether the name is to be looked up, created, renamed, or deleted.
 * When CREATE, RENAME, or DELETE is specified, information usable in
 * creating, renaming, or deleting a directory entry may be calculated.
 * If flag has LOCKPARENT or'ed into it and the target of the pathname
 * exists, lookup returns both the target and its parent directory locked.
 * When creating or renaming and LOCKPARENT is specified, the target may
 * not be ".".  When deleting and LOCKPARENT is specified, the target may
 * be "."., but the caller must check to ensure it does an vrele and iput
 * instead of two iputs.
 *
 * Overall outline of ufs_lookup:
 *
 *	check accessibility of directory
 *	look for name in cache, if found, then if at end of path
 *	  and deleting or creating, drop it, else return name
 *	search for name in directory, to found or notfound
 * notfound:
 *	if creating, return locked directory, leaving info on available slots
 *	else return error
 * found:
 *	if at end of path and deleting, return information to allow delete
 *	if at end of path and rewriting (RENAME and LOCKPARENT), lock target
 *	  inode and return info to allow rewrite
 *	if not at end, add name to cache; if at end and neither creating
 *	  nor deleting, add name to cache
 *
 * NOTE: (LOOKUP | LOCKPARENT) currently returns the parent inode unlocked.
 */
int
ufs_lookup (ap)
	struct vop_lookup_args /* {
		struct vnode * a_dvp;
		struct vnode ** a_vpp;
		struct componentname * a_cnp;
	} */ *ap;
{
	USES_VOP_ACCESS;
	USES_VOP_BLKATOFF;
	USES_VOP_VGET;
	register struct inode *dp;	/* the directory we are searching */
	struct buf *bp;			/* a buffer of directory entries */
	register struct direct *ep;	/* the current directory entry */
	int entryoffsetinblock;		/* offset of ep in bp's buffer */
	enum {NONE, COMPACT, FOUND} slotstatus;
	doff_t slotoffset;		/* offset of area with free space */
	int slotsize;			/* size of area at slotoffset */
	int slotfreespace;		/* amount of space free in slot */
	int slotneeded;			/* size of the entry we're seeking */
	int numdirpasses;		/* strategy for directory search */
	doff_t endsearch;		/* offset to end directory search */
	doff_t prevoff;			/* prev entry dp->i_offset */
	struct inode *pdp;		/* saved dp during symlink work */
	struct vnode *tdp;		/* returned by VOP_VGET */
	doff_t enduseful;		/* pointer past last used dir slot */
	u_long bmask;			/* block offset mask */
	int lockparent;			/* 1 => lockparent flag is set */
	int wantparent;			/* 1 => wantparent or lockparent flag */
	int error;
	struct vnode *vdp = ap->a_dvp;	/* saved for one special case */

	bp = NULL;
	slotoffset = -1;
	*ap->a_vpp = NULL;
	dp = VTOI(ap->a_dvp);
	lockparent = ap->a_cnp->cn_flags & LOCKPARENT;
	wantparent = ap->a_cnp->cn_flags & (LOCKPARENT|WANTPARENT);

	/*
	 * Check accessiblity of directory.
	 */
	if ((dp->i_mode & IFMT) != IFDIR)
		return (ENOTDIR);
	if (error = VOP_ACCESS(ap->a_dvp, VEXEC, ap->a_cnp->cn_cred, ap->a_cnp->cn_proc))
		return (error);

	/*
	 * We now have a segment name to search for, and a directory to search.
	 *
	 * Before tediously performing a linear scan of the directory,
	 * check the name cache to see if the directory/name pair
	 * we are looking for is known already.
	 */
	if (error = cache_lookup(ap->a_dvp, ap->a_vpp, ap->a_cnp)) {
		int vpid;	/* capability number of vnode */

		if (error == ENOENT)
			return (error);
#ifdef PARANOID
		if (ap->a_dvp == ndp->ni_rdir && (ap->a_cnp->cn_flags & ISDOTDOT))
			panic("ufs_lookup: .. through root");
#endif
		/*
		 * Get the next vnode in the path.
		 * See comment below starting `Step through' for
		 * an explaination of the locking protocol.
		 */
		/*
		 * The borrowing of variables
		 * here is somewhat confusing.  Usually, ap->a_dvp/dp
		 * is the directory being searched.
		 * Here it's the target returned from the cache.
		 */
		pdp = dp;
		dp = VTOI(*ap->a_vpp);
		ap->a_dvp = *ap->a_vpp;
		vpid = ap->a_dvp->v_id;
		if (pdp == dp) {   /* lookup on "." */
			VREF(ap->a_dvp);
			error = 0;
		} else if (ap->a_cnp->cn_flags & ISDOTDOT) {
			IUNLOCK(pdp);
			error = vget(ap->a_dvp);
			if (!error && lockparent && (ap->a_cnp->cn_flags & ISLASTCN))
				ILOCK(pdp);
		} else {
			error = vget(ap->a_dvp);
			if (!lockparent || error || !(ap->a_cnp->cn_flags & ISLASTCN))
				IUNLOCK(pdp);
		}
		/*
		 * Check that the capability number did not change
		 * while we were waiting for the lock.
		 */
		if (!error) {
			if (vpid == ap->a_dvp->v_id)
				return (0);
			ufs_iput(dp);
			if (lockparent && pdp != dp &&
			    (ap->a_cnp->cn_flags & ISLASTCN))
				IUNLOCK(pdp);
		}
		ILOCK(pdp);
		dp = pdp;
		ap->a_dvp = ITOV(dp);
		*ap->a_vpp = NULL;
	}

	/*
	 * Suppress search for slots unless creating
	 * file and at end of pathname, in which case
	 * we watch for a place to put the new file in
	 * case it doesn't already exist.
	 */
	slotstatus = FOUND;
	slotfreespace = slotsize = slotneeded = 0;
	if ((ap->a_cnp->cn_nameiop == CREATE || ap->a_cnp->cn_nameiop == RENAME) &&
	    (ap->a_cnp->cn_flags & ISLASTCN)) {
		slotstatus = NONE;
		slotneeded = (sizeof(struct direct) - MAXNAMLEN +
			ap->a_cnp->cn_namelen + 3) &~ 3;
	}

	/*
	 * If there is cached information on a previous search of
	 * this directory, pick up where we last left off.
	 * We cache only lookups as these are the most common
	 * and have the greatest payoff. Caching CREATE has little
	 * benefit as it usually must search the entire directory
	 * to determine that the entry does not exist. Caching the
	 * location of the last DELETE or RENAME has not reduced
	 * profiling time and hence has been removed in the interest
	 * of simplicity.
	 */
	bmask = VFSTOUFS(ap->a_dvp->v_mount)->um_mountp->mnt_stat.f_iosize - 1;
	if (ap->a_cnp->cn_nameiop != LOOKUP || dp->i_diroff == 0 ||
	    dp->i_diroff > dp->i_size) {
		entryoffsetinblock = 0;
		dp->i_offset = 0;
		numdirpasses = 1;
	} else {
		dp->i_offset = dp->i_diroff;
		if ((entryoffsetinblock = dp->i_offset & bmask) &&
		    (error = VOP_BLKATOFF(ap->a_dvp, (off_t)dp->i_offset, NULL, &bp)))
			return (error);
		numdirpasses = 2;
		nchstats.ncs_2passes++;
	}
	prevoff = dp->i_offset;
	endsearch = roundup(dp->i_size, DIRBLKSIZ);
	enduseful = 0;

searchloop:
	while (dp->i_offset < endsearch) {
		/*
		 * If offset is on a block boundary, read the next directory
		 * block.  Release previous if it exists.
		 */
		if ((dp->i_offset & bmask) == 0) {
			if (bp != NULL)
				brelse(bp);
			if (error =
			    VOP_BLKATOFF(ap->a_dvp, (off_t)dp->i_offset, NULL, &bp))
				return (error);
			entryoffsetinblock = 0;
		}
		/*
		 * If still looking for a slot, and at a DIRBLKSIZE
		 * boundary, have to start looking for free space again.
		 */
		if (slotstatus == NONE &&
		    (entryoffsetinblock & (DIRBLKSIZ - 1)) == 0) {
			slotoffset = -1;
			slotfreespace = 0;
		}
		/*
		 * Get pointer to next entry.
		 * Full validation checks are slow, so we only check
		 * enough to insure forward progress through the
		 * directory. Complete checks can be run by patching
		 * "dirchk" to be true.
		 */
		ep = (struct direct *)(bp->b_un.b_addr + entryoffsetinblock);
		if (ep->d_reclen == 0 ||
		    dirchk && ufs_dirbadentry(ep, entryoffsetinblock)) {
			int i;

			ufs_dirbad(dp, dp->i_offset, "mangled entry");
			i = DIRBLKSIZ - (entryoffsetinblock & (DIRBLKSIZ - 1));
			dp->i_offset += i;
			entryoffsetinblock += i;
			continue;
		}

		/*
		 * If an appropriate sized slot has not yet been found,
		 * check to see if one is available. Also accumulate space
		 * in the current block so that we can determine if
		 * compaction is viable.
		 */
		if (slotstatus != FOUND) {
			int size = ep->d_reclen;

			if (ep->d_ino != 0)
				size -= DIRSIZ(ep);
			if (size > 0) {
				if (size >= slotneeded) {
					slotstatus = FOUND;
					slotoffset = dp->i_offset;
					slotsize = ep->d_reclen;
				} else if (slotstatus == NONE) {
					slotfreespace += size;
					if (slotoffset == -1)
						slotoffset = dp->i_offset;
					if (slotfreespace >= slotneeded) {
						slotstatus = COMPACT;
						slotsize = dp->i_offset +
						      ep->d_reclen - slotoffset;
					}
				}
			}
		}

		/*
		 * Check for a name match.
		 */
		if (ep->d_ino) {
			if (ep->d_namlen == ap->a_cnp->cn_namelen &&
			    !bcmp(ap->a_cnp->cn_nameptr, ep->d_name,
				(unsigned)ep->d_namlen)) {
				/*
				 * Save directory entry's inode number and
				 * reclen in ndp->ni_ufs area, and release
				 * directory buffer.
				 */
				dp->i_ino = ep->d_ino;
				dp->i_reclen = ep->d_reclen;
				brelse(bp);
				goto found;
			}
		}
		prevoff = dp->i_offset;
		dp->i_offset += ep->d_reclen;
		entryoffsetinblock += ep->d_reclen;
		if (ep->d_ino)
			enduseful = dp->i_offset;
	}
/* notfound: */
	/*
	 * If we started in the middle of the directory and failed
	 * to find our target, we must check the beginning as well.
	 */
	if (numdirpasses == 2) {
		numdirpasses--;
		dp->i_offset = 0;
		endsearch = dp->i_diroff;
		goto searchloop;
	}
	if (bp != NULL)
		brelse(bp);
	/*
	 * If creating, and at end of pathname and current
	 * directory has not been removed, then can consider
	 * allowing file to be created.
	 */
	if ((ap->a_cnp->cn_nameiop == CREATE || ap->a_cnp->cn_nameiop == RENAME) &&
	    (ap->a_cnp->cn_flags & ISLASTCN) && dp->i_nlink != 0) {
		/*
		 * Access for write is interpreted as allowing
		 * creation of files in the directory.
		 */
		if (error = VOP_ACCESS(ap->a_dvp, VWRITE, ap->a_cnp->cn_cred, ap->a_cnp->cn_proc))
			return (error);
		/*
		 * Return an indication of where the new directory
		 * entry should be put.  If we didn't find a slot,
		 * then set dp->i_count to 0 indicating
		 * that the new slot belongs at the end of the
		 * directory. If we found a slot, then the new entry
		 * can be put in the range from dp->i_offset to
		 * dp->i_offset + dp->i_count.
		 */
		if (slotstatus == NONE) {
			dp->i_offset = roundup(dp->i_size, DIRBLKSIZ);
			dp->i_count = 0;
			enduseful = dp->i_offset;
		} else {
			dp->i_offset = slotoffset;
			dp->i_count = slotsize;
			if (enduseful < slotoffset + slotsize)
				enduseful = slotoffset + slotsize;
		}
		dp->i_endoff = roundup(enduseful, DIRBLKSIZ);
		dp->i_flag |= IUPD|ICHG;
		/*
		 * We return with the directory locked, so that
		 * the parameters we set up above will still be
		 * valid if we actually decide to do a direnter().
		 * We return ni_vp == NULL to indicate that the entry
		 * does not currently exist; we leave a pointer to
		 * the (locked) directory inode in ndp->ni_dvp.
		 * The pathname buffer is saved so that the name
		 * can be obtained later.
		 *
		 * NB - if the directory is unlocked, then this
		 * information cannot be used.
		 */
		ap->a_cnp->cn_flags |= SAVENAME;
		if (!lockparent)
			IUNLOCK(dp);
		return (EJUSTRETURN);
	}
	/*
	 * Insert name into cache (as non-existent) if appropriate.
	 */
	if ((ap->a_cnp->cn_flags & MAKEENTRY) && ap->a_cnp->cn_nameiop != CREATE)
		cache_enter(ap->a_dvp, *ap->a_vpp, ap->a_cnp);
	return (ENOENT);

found:
	if (numdirpasses == 2)
		nchstats.ncs_pass2++;
	/*
	 * Check that directory length properly reflects presence
	 * of this entry.
	 */
	if (entryoffsetinblock + DIRSIZ(ep) > dp->i_size) {
		ufs_dirbad(dp, dp->i_offset, "i_size too small");
		dp->i_size = entryoffsetinblock + DIRSIZ(ep);
		dp->i_flag |= IUPD|ICHG;
	}

	/*
	 * Found component in pathname.
	 * If the final component of path name, save information
	 * in the cache as to where the entry was found.
	 */
	if ((ap->a_cnp->cn_flags & ISLASTCN) && ap->a_cnp->cn_nameiop == LOOKUP)
		dp->i_diroff = dp->i_offset &~ (DIRBLKSIZ - 1);

	/*
	 * If deleting, and at end of pathname, return
	 * parameters which can be used to remove file.
	 * If the wantparent flag isn't set, we return only
	 * the directory (in ndp->ni_dvp), otherwise we go
	 * on and lock the inode, being careful with ".".
	 */
	if (ap->a_cnp->cn_nameiop == DELETE && (ap->a_cnp->cn_flags & ISLASTCN)) {
		/*
		 * Write access to directory required to delete files.
		 */
		if (error = VOP_ACCESS(ap->a_dvp, VWRITE, ap->a_cnp->cn_cred, ap->a_cnp->cn_proc))
			return (error);
		/*
		 * Return pointer to current entry in dp->i_offset,
		 * and distance past previous entry (if there
		 * is a previous entry in this block) in dp->i_count.
		 * Save directory inode pointer in ndp->ni_dvp for dirremove().
		 */
		if ((dp->i_offset & (DIRBLKSIZ - 1)) == 0)
			dp->i_count = 0;
		else
			dp->i_count = dp->i_offset - prevoff;
		if (dp->i_number == dp->i_ino) {
			VREF(vdp);
			*ap->a_vpp = vdp;
			return (0);
		}
		if (error = VOP_VGET(ap->a_dvp, dp->i_ino, &tdp))
			return (error);
		/*
		 * If directory is "sticky", then user must own
		 * the directory, or the file in it, else she
		 * may not delete it (unless she's root). This
		 * implements append-only directories.
		 */
		if ((dp->i_mode & ISVTX) &&
		    ap->a_cnp->cn_cred->cr_uid != 0 &&
		    ap->a_cnp->cn_cred->cr_uid != dp->i_uid &&
		    VTOI(tdp)->i_uid != ap->a_cnp->cn_cred->cr_uid) {
			vput(tdp);
			return (EPERM);
		}
		*ap->a_vpp = tdp;
		if (!lockparent)
			IUNLOCK(dp);
		return (0);
	}

	/*
	 * If rewriting (RENAME), return the inode and the
	 * information required to rewrite the present directory
	 * Must get inode of directory entry to verify it's a
	 * regular file, or empty directory.
	 */
	if (ap->a_cnp->cn_nameiop == RENAME && wantparent &&
	    (ap->a_cnp->cn_flags & ISLASTCN)) {
		if (error = VOP_ACCESS(ap->a_dvp, VWRITE, ap->a_cnp->cn_cred, ap->a_cnp->cn_proc))
			return (error);
		/*
		 * Careful about locking second inode.
		 * This can only occur if the target is ".".
		 */
		if (dp->i_number == dp->i_ino)
			return (EISDIR);
		if (error = VOP_VGET(ap->a_dvp, dp->i_ino, &tdp))
			return (error);
		*ap->a_vpp = tdp;
		ap->a_cnp->cn_flags |= SAVENAME;
		if (!lockparent)
			IUNLOCK(dp);
		return (0);
	}

	/*
	 * Step through the translation in the name.  We do not `iput' the
	 * directory because we may need it again if a symbolic link
	 * is relative to the current directory.  Instead we save it
	 * unlocked as "pdp".  We must get the target inode before unlocking
	 * the directory to insure that the inode will not be removed
	 * before we get it.  We prevent deadlock by always fetching
	 * inodes from the root, moving down the directory tree. Thus
	 * when following backward pointers ".." we must unlock the
	 * parent directory before getting the requested directory.
	 * There is a potential race condition here if both the current
	 * and parent directories are removed before the `iget' for the
	 * inode associated with ".." returns.  We hope that this occurs
	 * infrequently since we cannot avoid this race condition without
	 * implementing a sophisticated deadlock detection algorithm.
	 * Note also that this simple deadlock detection scheme will not
	 * work if the file system has any hard links other than ".."
	 * that point backwards in the directory structure.
	 */
	pdp = dp;
	if (ap->a_cnp->cn_flags & ISDOTDOT) {
		IUNLOCK(pdp);	/* race to get the inode */
		if (error = VOP_VGET(ap->a_dvp, dp->i_ino, &tdp)) {
			ILOCK(pdp);
			return (error);
		}
		if (lockparent && (ap->a_cnp->cn_flags & ISLASTCN))
			ILOCK(pdp);
		*ap->a_vpp = tdp;
	} else if (dp->i_number == dp->i_ino) {
		VREF(ap->a_dvp);	/* we want ourself, ie "." */
		*ap->a_vpp = ap->a_dvp;
	} else {
		if (error = VOP_VGET(ap->a_dvp, dp->i_ino, &tdp))
			return (error);
		if (!lockparent || !(ap->a_cnp->cn_flags & ISLASTCN))
			IUNLOCK(pdp);
		*ap->a_vpp = tdp;
	}

	/*
	 * Insert name into cache if appropriate.
	 */
	if (ap->a_cnp->cn_flags & MAKEENTRY)
		cache_enter(ap->a_dvp, *ap->a_vpp, ap->a_cnp);
	return (0);
}

void
ufs_dirbad(ip, offset, how)
	struct inode *ip;
	doff_t offset;
	char *how;
{
	struct mount *mp;

	mp = ITOV(ip)->v_mount;
	(void)printf("%s: bad dir ino %d at offset %d: %s\n",
	    mp->mnt_stat.f_mntonname, ip->i_number, offset, how);
	if ((mp->mnt_stat.f_flags & MNT_RDONLY) == 0)
		panic("bad dir");
}

/*
 * Do consistency checking on a directory entry:
 *	record length must be multiple of 4
 *	entry must fit in rest of its DIRBLKSIZ block
 *	record must be large enough to contain entry
 *	name is not longer than MAXNAMLEN
 *	name must be as long as advertised, and null terminated
 */
int
ufs_dirbadentry(ep, entryoffsetinblock)
	register struct direct *ep;
	int entryoffsetinblock;
{
	register int i;

	if ((ep->d_reclen & 0x3) != 0 ||
	    ep->d_reclen > DIRBLKSIZ - (entryoffsetinblock & (DIRBLKSIZ - 1)) ||
	    ep->d_reclen < DIRSIZ(ep) || ep->d_namlen > MAXNAMLEN) {
		/*return (1); */
		printf("First bad\n");
		goto bad;
	}
	for (i = 0; i < ep->d_namlen; i++)
		if (ep->d_name[i] == '\0') {
			/*return (1); */
			printf("Second bad\n");
			goto bad;
	}
	if (ep->d_name[i])
		goto bad;
	return (ep->d_name[i]);
bad:
	return(1);
}

/*
 * Write a directory entry after a call to namei, using the parameters
 * that it left in nameidata.  The argument ip is the inode which the new
 * directory entry will refer to.  Dvp is a pointer to the directory to
 * be written, which was left locked by namei. Remaining parameters
 * (dp->i_offset, dp->i_count) indicate how the space for the new
 * entry is to be obtained.
 */
int
ufs_direnter(ip, dvp, cnp)
	struct inode *ip;
	struct vnode *dvp;
	register struct componentname *cnp;
{
	USES_VOP_BLKATOFF;
	USES_VOP_BWRITE;
	USES_VOP_TRUNCATE;
	USES_VOP_WRITE;
	register struct direct *ep, *nep;
	register struct inode *dp;
	struct buf *bp;
	struct direct newdir;
	struct iovec aiov;
	struct uio auio;
	u_int dsize;
	int error, loc, newentrysize, spacefree;
	char *dirbuf;

#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & SAVENAME) == 0)
		panic("direnter: missing name");
#endif
	dp = VTOI(dvp);
	newdir.d_ino = ip->i_number;
	newdir.d_namlen = cnp->cn_namelen;
	bcopy(cnp->cn_nameptr, newdir.d_name, (unsigned)cnp->cn_namelen + 1);
	newentrysize = DIRSIZ(&newdir);
	if (dp->i_count == 0) {
		/*
		 * If dp->i_count is 0, then namei could find no
		 * space in the directory. Here, dp->i_offset will
		 * be on a directory block boundary and we will write the
		 * new entry into a fresh block.
		 */
		if (dp->i_offset & (DIRBLKSIZ - 1))
			panic("wdir: newblk");
		auio.uio_offset = dp->i_offset;
		newdir.d_reclen = DIRBLKSIZ;
		auio.uio_resid = newentrysize;
		aiov.iov_len = newentrysize;
		aiov.iov_base = (caddr_t)&newdir;
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_rw = UIO_WRITE;
		auio.uio_segflg = UIO_SYSSPACE;
		auio.uio_procp = (struct proc *)0;
		error = VOP_WRITE(dvp, &auio, IO_SYNC, cnp->cn_cred);
		if (DIRBLKSIZ >
		    VFSTOUFS(dvp->v_mount)->um_mountp->mnt_stat.f_bsize)
			/* XXX should grow with balloc() */
			panic("ufs_direnter: frag size");
		else if (!error) {
			dp->i_size = roundup(dp->i_size, DIRBLKSIZ);
			dp->i_flag |= ICHG;
		}
		return (error);
	}

	/*
	 * If dp->i_count is non-zero, then namei found space
	 * for the new entry in the range dp->i_offset to
	 * dp->i_offset + dp->i_count in the directory.
	 * To use this space, we may have to compact the entries located
	 * there, by copying them together towards the beginning of the
	 * block, leaving the free space in one usable chunk at the end.
	 */

	/*
	 * Increase size of directory if entry eats into new space.
	 * This should never push the size past a new multiple of
	 * DIRBLKSIZE.
	 *
	 * N.B. - THIS IS AN ARTIFACT OF 4.2 AND SHOULD NEVER HAPPEN.
	 */
	if (dp->i_offset + dp->i_count > dp->i_size)
		dp->i_size = dp->i_offset + dp->i_count;
	/*
	 * Get the block containing the space for the new directory entry.
	 */
	if (error = VOP_BLKATOFF(dvp, (off_t)dp->i_offset, &dirbuf, &bp))
		return (error);
	/*
	 * Find space for the new entry. In the simple case, the entry at
	 * offset base will have the space. If it does not, then namei
	 * arranged that compacting the region dp->i_offset to
	 * dp->i_offset + dp->i_count would yield the
	 * space.
	 */
	ep = (struct direct *)dirbuf;
	dsize = DIRSIZ(ep);
	spacefree = ep->d_reclen - dsize;
	for (loc = ep->d_reclen; loc < dp->i_count; ) {
		nep = (struct direct *)(dirbuf + loc);
		if (ep->d_ino) {
			/* trim the existing slot */
			ep->d_reclen = dsize;
			ep = (struct direct *)((char *)ep + dsize);
		} else {
			/* overwrite; nothing there; header is ours */
			spacefree += dsize;
		}
		dsize = DIRSIZ(nep);
		spacefree += nep->d_reclen - dsize;
		loc += nep->d_reclen;
		bcopy((caddr_t)nep, (caddr_t)ep, dsize);
	}
	/*
	 * Update the pointer fields in the previous entry (if any),
	 * copy in the new entry, and write out the block.
	 */
	if (ep->d_ino == 0) {
		if (spacefree + dsize < newentrysize)
			panic("wdir: compact1");
		newdir.d_reclen = spacefree + dsize;
	} else {
		if (spacefree < newentrysize)
			panic("wdir: compact2");
		newdir.d_reclen = spacefree;
		ep->d_reclen = dsize;
		ep = (struct direct *)((char *)ep + dsize);
	}
	bcopy((caddr_t)&newdir, (caddr_t)ep, (u_int)newentrysize);
	error = VOP_BWRITE(bp);
	dp->i_flag |= IUPD|ICHG;
	if (!error && dp->i_endoff && dp->i_endoff < dp->i_size)
		error = VOP_TRUNCATE(dvp, (off_t)dp->i_endoff, IO_SYNC,
		    cnp->cn_cred, cnp->cn_proc);
	return (error);
}

/*
 * Remove a directory entry after a call to namei, using
 * the parameters which it left in nameidata. The entry
 * dp->i_offset contains the offset into the directory of the
 * entry to be eliminated.  The dp->i_count field contains the
 * size of the previous record in the directory.  If this
 * is 0, the first entry is being deleted, so we need only
 * zero the inode number to mark the entry as free.  If the
 * entry is not the first in the directory, we must reclaim
 * the space of the now empty record by adding the record size
 * to the size of the previous entry.
 */
int
ufs_dirremove(dvp, cnp)
	struct vnode *dvp;
	struct componentname *cnp;
{
	USES_VOP_BLKATOFF;
	USES_VOP_BWRITE;
	register struct inode *dp;
	struct direct *ep;
	struct buf *bp;
	int error;

	dp = VTOI(dvp);
	if (dp->i_count == 0) {
		/*
		 * First entry in block: set d_ino to zero.
		 */
		if (error =
		    VOP_BLKATOFF(dvp, (off_t)dp->i_offset, (char **)&ep, &bp))
			return (error);
		ep->d_ino = 0;
		error = VOP_BWRITE(bp);
		dp->i_flag |= IUPD|ICHG;
		return (error);
	}
	/*
	 * Collapse new free space into previous entry.
	 */
	if (error = VOP_BLKATOFF(dvp, (off_t)(dp->i_offset - dp->i_count),
	    (char **)&ep, &bp))
		return (error);
	ep->d_reclen += dp->i_reclen;
	error = VOP_BWRITE(bp);
	dp->i_flag |= IUPD|ICHG;
	return (error);
}

/*
 * Rewrite an existing directory entry to point at the inode
 * supplied.  The parameters describing the directory entry are
 * set up by a call to namei.
 */
int
ufs_dirrewrite(dp, ip, cnp)
	struct inode *dp, *ip;
	struct componentname *cnp;
{
	USES_VOP_BLKATOFF;
	USES_VOP_BWRITE;
	struct buf *bp;
	struct direct *ep;
	int error;

	if (error =
	    VOP_BLKATOFF(ITOV(dp), (off_t)dp->i_offset, (char **)&ep, &bp))
		return (error);
	ep->d_ino = ip->i_number;
	error = VOP_BWRITE(bp);
	dp->i_flag |= IUPD|ICHG;
	return (error);
}

/*
 * Check if a directory is empty or not.
 * Inode supplied must be locked.
 *
 * Using a struct dirtemplate here is not precisely
 * what we want, but better than using a struct direct.
 *
 * NB: does not handle corrupted directories.
 */
int
ufs_dirempty(ip, parentino, cred)
	register struct inode *ip;
	ino_t parentino;
	struct ucred *cred;
{
	register off_t off;
	struct dirtemplate dbuf;
	register struct direct *dp = (struct direct *)&dbuf;
	int error, count;
#define	MINDIRSIZ (sizeof (struct dirtemplate) / 2)

	for (off = 0; off < ip->i_size; off += dp->d_reclen) {
		error = vn_rdwr(UIO_READ, ITOV(ip), (caddr_t)dp, MINDIRSIZ, off,
		   UIO_SYSSPACE, IO_NODELOCKED, cred, &count, (struct proc *)0);
		/*
		 * Since we read MINDIRSIZ, residual must
		 * be 0 unless we're at end of file.
		 */
		if (error || count != 0)
			return (0);
		/* avoid infinite loops */
		if (dp->d_reclen == 0)
			return (0);
		/* skip empty entries */
		if (dp->d_ino == 0)
			continue;
		/* accept only "." and ".." */
		if (dp->d_namlen > 2)
			return (0);
		if (dp->d_name[0] != '.')
			return (0);
		/*
		 * At this point d_namlen must be 1 or 2.
		 * 1 implies ".", 2 implies ".." if second
		 * char is also "."
		 */
		if (dp->d_namlen == 1)
			continue;
		if (dp->d_name[1] == '.' && dp->d_ino == parentino)
			continue;
		return (0);
	}
	return (1);
}

/*
 * Check if source directory is in the path of the target directory.
 * Target is supplied locked, source is unlocked.
 * The target is always iput before returning.
 */
int
ufs_checkpath(source, target, cred)
	struct inode *source, *target;
	struct ucred *cred;
{
	USES_VOP_VGET;
	struct dirtemplate dirbuf;
	register struct inode *ip;
	struct vnode *vp;
	int error, rootino;

	ip = target;
	if (ip->i_number == source->i_number) {
		error = EEXIST;
		goto out;
	}
	rootino = ROOTINO;
	error = 0;
	if (ip->i_number == rootino)
		goto out;

	for (;;) {
		if ((ip->i_mode & IFMT) != IFDIR) {
			error = ENOTDIR;
			break;
		}
		vp = ITOV(ip);
		error = vn_rdwr(UIO_READ, vp, (caddr_t)&dirbuf,
			sizeof (struct dirtemplate), (off_t)0, UIO_SYSSPACE,
			IO_NODELOCKED, cred, (int *)0, (struct proc *)0);
		if (error != 0)
			break;
		if (dirbuf.dotdot_namlen != 2 ||
		    dirbuf.dotdot_name[0] != '.' ||
		    dirbuf.dotdot_name[1] != '.') {
			error = ENOTDIR;
			break;
		}
		if (dirbuf.dotdot_ino == source->i_number) {
			error = EINVAL;
			break;
		}
		if (dirbuf.dotdot_ino == rootino)
			break;
		ufs_iput(ip);
		if (error = VOP_VGET(vp, dirbuf.dotdot_ino, &vp))
			break;
		ip = VTOI(vp);
	}

out:
	if (error == ENOTDIR)
		printf("checkpath: .. not a directory\n");
	if (ip != NULL)
		ufs_iput(ip);
	return (error);
}
