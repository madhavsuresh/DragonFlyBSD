/*-
 * Copyright (c) 1994 Bruce D. Evans.
 * All rights reserved.
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 *	from: @(#)wd.c	7.2 (Berkeley) 5/9/91
 *	from: wd.c,v 1.55 1994/10/22 01:57:12 phk Exp $
 *	from: @(#)ufs_disksubr.c	7.16 (Berkeley) 5/4/91
 *	from: ufs_disksubr.c,v 1.8 1994/06/07 01:21:39 phk Exp $
 * $FreeBSD: src/sys/kern/subr_diskslice.c,v 1.82.2.6 2001/07/24 09:49:41 dd Exp $
 * $DragonFly: src/sys/kern/subr_diskslice.c,v 1.32 2007/05/16 05:20:23 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disklabel.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <sys/diskmbr.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/device.h>
#include <sys/thread2.h>

#include <vfs/ufs/dinode.h>	/* XXX used only for fs.h */
#include <vfs/ufs/fs.h>		/* XXX used only to get BBSIZE/SBSIZE */

#define TRACE(str)	do { if (ds_debug) kprintf str; } while (0)

typedef	u_char	bool_t;

static volatile bool_t ds_debug;

static struct disklabel *clone_label (struct disk_info *info,
					struct diskslice *sp);
static void dsiodone (struct bio *bio);
static char *fixlabel (char *sname, struct diskslice *sp,
			   struct disklabel *lp, int writeflag);
static void free_ds_label (struct diskslices *ssp, int slice);
static void partition_info (char *sname, int part, struct partition *pp);
static void slice_info (char *sname, struct diskslice *sp);
static void set_ds_label (struct diskslices *ssp, int slice,
			      struct disklabel *lp);
static void set_ds_wlabel (struct diskslices *ssp, int slice, int wlabel);

/*
 * Create a disklabel based on a disk_info structure, initializing
 * the appropriate fields and creating a raw partition that covers the
 * whole disk.
 *
 * If a diskslice is passed, the label is truncated to the slice
 */
static struct disklabel *
clone_label(struct disk_info *info, struct diskslice *sp)
{
	struct disklabel *lp1;

	lp1 = kmalloc(sizeof *lp1, M_DEVBUF, M_WAITOK | M_ZERO);
	lp1->d_nsectors = info->d_secpertrack;
	lp1->d_ntracks = info->d_nheads;
	lp1->d_secpercyl = info->d_secpercyl;
	lp1->d_secsize = info->d_media_blksize;

	if (sp) {
		lp1->d_secperunit = (u_int)sp->ds_size;
		lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	} else {
		lp1->d_secperunit = (u_int)info->d_media_blocks;
		lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	}

	/*
	 * Used by the CD driver to create a compatibility slice which
	 * allows us to mount root from the CD.
	 */
	if (info->d_dsflags & DSO_COMPATPARTA) {
		lp1->d_partitions[0].p_size = lp1->d_secperunit;
		lp1->d_partitions[0].p_fstype = FS_OTHER;
	}

	if (lp1->d_typename[0] == '\0')
		strncpy(lp1->d_typename, "amnesiac", sizeof(lp1->d_typename));
	if (lp1->d_packname[0] == '\0')
		strncpy(lp1->d_packname, "fictitious", sizeof(lp1->d_packname));
	if (lp1->d_nsectors == 0)
		lp1->d_nsectors = 32;
	if (lp1->d_ntracks == 0)
		lp1->d_ntracks = 64;
	lp1->d_secpercyl = lp1->d_nsectors * lp1->d_ntracks;
	lp1->d_ncylinders = lp1->d_secperunit / lp1->d_secpercyl;
	if (lp1->d_rpm == 0)
		lp1->d_rpm = 3600;
	if (lp1->d_interleave == 0)
		lp1->d_interleave = 1;
	if (lp1->d_npartitions < RAW_PART + 1)
		lp1->d_npartitions = MAXPARTITIONS;
	if (lp1->d_bbsize == 0)
		lp1->d_bbsize = BBSIZE;
	if (lp1->d_sbsize == 0)
		lp1->d_sbsize = SBSIZE;
	lp1->d_partitions[RAW_PART].p_size = lp1->d_secperunit;
	lp1->d_magic = DISKMAGIC;
	lp1->d_magic2 = DISKMAGIC;
	lp1->d_checksum = dkcksum(lp1);
	return (lp1);
}

/*
 * Determine the size of the transfer, and make sure it is
 * within the boundaries of the partition. Adjust transfer
 * if needed, and signal errors or early completion.
 *
 * XXX TODO:
 *	o Split buffers that are too big for the device.
 *	o Check for overflow.
 *	o Finish cleaning this up.
 *
 * This function returns 1 on success, 0 if transfer equates
 * to EOF (end of disk) or -1 on failure.  The appropriate 
 * 'errno' value is also set in bp->b_error and bp->b_flags
 * is marked with B_ERROR.
 */
struct bio *
dscheck(cdev_t dev, struct bio *bio, struct diskslices *ssp)
{
	struct buf *bp = bio->bio_buf;
	struct bio *nbio;
	struct disklabel *lp;
	char *msg;
	long nsec;
	struct partition *pp;
	u_int64_t secno;
	u_int64_t endsecno;
	u_int64_t labelsect;
	u_int64_t slicerel_secno;
	struct diskslice *sp;
	int shift;
	int mask;

	if (bio->bio_offset < 0) {
		kprintf("dscheck(%s): negative bio_offset %lld\n", 
		    devtoname(dev), bio->bio_offset);
		goto bad;
	}
	sp = &ssp->dss_slices[dkslice(dev)];
	lp = sp->ds_label;

	if (ssp->dss_secmult == 1) {
		shift = DEV_BSHIFT;
		goto doshift;
	} else if (ssp->dss_secshift != -1) {
		shift = DEV_BSHIFT + ssp->dss_secshift;
doshift:
		mask = (1 << shift) - 1;
		if ((int)bp->b_bcount & mask)
			goto bad_bcount;
		if ((int)bio->bio_offset & mask)
			goto bad_blkno;
		secno = bio->bio_offset >> shift;
		nsec = bp->b_bcount >> shift;
	} else {
		if (bp->b_bcount % ssp->dss_secsize)
			goto bad_bcount;
		if (bio->bio_offset % ssp->dss_secsize)
			goto bad_blkno;
		secno = bio->bio_offset / ssp->dss_secsize;
		nsec = bp->b_bcount / ssp->dss_secsize;
	}
	if (lp == NULL) {
		labelsect = -LABELSECTOR - 1;
		endsecno = sp->ds_size;
		slicerel_secno = secno;
	} else {
		labelsect = lp->d_partitions[LABEL_PART].p_offset;
		if (labelsect != 0)
			Debugger("labelsect != 0 in dscheck()");
		pp = &lp->d_partitions[dkpart(dev)];
		endsecno = pp->p_size;
		slicerel_secno = pp->p_offset + secno;
	}

	/* overwriting disk label ? */
	/* XXX should also protect bootstrap in first 8K */
	if (slicerel_secno <= LABELSECTOR + labelsect &&
#if LABELSECTOR != 0
	    slicerel_secno + nsec > LABELSECTOR + labelsect &&
#endif
	    bp->b_cmd != BUF_CMD_READ && sp->ds_wlabel == 0) {
		bp->b_error = EROFS;
		goto error;
	}

#if defined(DOSBBSECTOR) && defined(notyet)
	/* overwriting master boot record? */
	if (slicerel_secno <= DOSBBSECTOR && bp->b_cmd != BUF_CMD_READ &&
	    sp->ds_wlabel == 0) {
		bp->b_error = EROFS;
		goto error;
	}
#endif

	/*
	 * EOF handling
	 */
	if (secno + nsec > endsecno) {
		/*
		 * Return an error if beyond the end of the disk, or
		 * if B_BNOCLIP is set.  Tell the system that we do not
		 * need to keep the buffer around.
		 */
		if (secno > endsecno || (bp->b_flags & B_BNOCLIP))
			goto bad;

		/*
		 * If exactly at end of disk, return an EOF.  Throw away
		 * the buffer contents, if any, by setting B_INVAL.
		 */
		if (secno == endsecno) {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_INVAL;
			goto done;
		}

		/*
		 * Else truncate
		 */
		nsec = endsecno - secno;
		bp->b_bcount = nsec * ssp->dss_secsize;
	}

	nbio = push_bio(bio);
	nbio->bio_offset = (off_t)(sp->ds_offset + slicerel_secno) * 
			   ssp->dss_secsize;

	/*
	 * Snoop on label accesses if the slice offset is nonzero.  Fudge
	 * offsets in the label to keep the in-core label coherent with
	 * the on-disk one.
	 */
	if (slicerel_secno <= LABELSECTOR + labelsect
#if LABELSECTOR != 0
	    && slicerel_secno + nsec > LABELSECTOR + labelsect
#endif
	    && sp->ds_offset != 0) {
		nbio->bio_done = dsiodone;
		nbio->bio_caller_info1.ptr = sp;
		nbio->bio_caller_info2.offset = 
			(off_t)(LABELSECTOR + labelsect - slicerel_secno) *
			ssp->dss_secsize;
		if (bp->b_cmd != BUF_CMD_READ) {
			/*
			 * XXX even disklabel(8) writes directly so we need
			 * to adjust writes.  Perhaps we should drop support
			 * for DIOCWLABEL (always write protect labels) and
			 * require the use of DIOCWDINFO.
			 *
			 * XXX probably need to copy the data to avoid even
			 * temporarily corrupting the in-core copy.
			 */
			/* XXX need name here. */
			msg = fixlabel(
				NULL, sp,
			       (struct disklabel *)
			       (bp->b_data + (int)nbio->bio_caller_info2.offset),
			       TRUE);
			if (msg != NULL) {
				kprintf("dscheck(%s): %s\n", 
				    devtoname(dev), msg);
				bp->b_error = EROFS;
				pop_bio(nbio);
				goto error;
			}
		}
	}
	return (nbio);

bad_bcount:
	kprintf(
	"dscheck(%s): b_bcount %d is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), bp->b_bcount, ssp->dss_secsize);
	goto bad;

bad_blkno:
	kprintf(
	"dscheck(%s): bio_offset %lld is not on a sector boundary (ssize %d)\n",
	    devtoname(dev), bio->bio_offset, ssp->dss_secsize);
bad:
	bp->b_error = EINVAL;
	/* fall through */
error:
	/*
	 * Terminate the I/O with a ranging error.  Since the buffer is
	 * either illegal or beyond the file EOF, mark it B_INVAL as well.
	 */
	bp->b_resid = bp->b_bcount;
	bp->b_flags |= B_ERROR | B_INVAL;
done:
	/*
	 * Caller must biodone() the originally passed bio if NULL is
	 * returned.
	 */
	return (NULL);
}

void
dsclose(cdev_t dev, int mode, struct diskslices *ssp)
{
	u_char mask;
	struct diskslice *sp;

	sp = &ssp->dss_slices[dkslice(dev)];
	mask = 1 << dkpart(dev);
	sp->ds_openmask &= ~mask;
}

void
dsgone(struct diskslices **sspp)
{
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;

	for (slice = 0, ssp = *sspp; slice < ssp->dss_nslices; slice++) {
		sp = &ssp->dss_slices[slice];
		free_ds_label(ssp, slice);
	}
	kfree(ssp, M_DEVBUF);
	*sspp = NULL;
}

/*
 * For the "write" commands (DIOCSDINFO and DIOCWDINFO), this
 * is subject to the same restriction as dsopen().
 */
int
dsioctl(cdev_t dev, u_long cmd, caddr_t data, int flags,
	struct diskslices **sspp, struct disk_info *info)
{
	int error;
	struct disklabel *lp;
	int old_wlabel;
	u_char openmask;
	int part;
	int slice;
	struct diskslice *sp;
	struct diskslices *ssp;
	struct partition *pp;

	slice = dkslice(dev);
	ssp = *sspp;
	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	switch (cmd) {

	case DIOCGDVIRGIN:
		lp = (struct disklabel *)data;
		if (ssp->dss_slices[WHOLE_DISK_SLICE].ds_label) {
			*lp = *ssp->dss_slices[WHOLE_DISK_SLICE].ds_label;
		} else {
			bzero(lp, sizeof(struct disklabel));
		}

		lp->d_magic = DISKMAGIC;
		lp->d_magic2 = DISKMAGIC;
		pp = &lp->d_partitions[RAW_PART];
		pp->p_offset = 0;
		pp->p_size = sp->ds_size;

		lp->d_npartitions = MAXPARTITIONS;
		if (lp->d_interleave == 0)
			lp->d_interleave = 1;
		if (lp->d_rpm == 0)
			lp->d_rpm = 3600;
		if (lp->d_nsectors == 0)
			lp->d_nsectors = 32;
		if (lp->d_ntracks == 0)
			lp->d_ntracks = 64;

		lp->d_bbsize = BBSIZE;
		lp->d_sbsize = SBSIZE;
		lp->d_secpercyl = lp->d_nsectors * lp->d_ntracks;
		lp->d_ncylinders = sp->ds_size / lp->d_secpercyl;
		lp->d_secperunit = sp->ds_size;
		lp->d_checksum = 0;
		lp->d_checksum = dkcksum(lp);
		return (0);

	case DIOCGDINFO:
		if (lp == NULL)
			return (EINVAL);
		*(struct disklabel *)data = *lp;
		return (0);

#ifdef notyet
	case DIOCGDINFOP:
		if (lp == NULL)
			return (EINVAL);
		*(struct disklabel **)data = lp;
		return (0);
#endif

	case DIOCGPART:
		{
			struct partinfo *dpart = (void *)data;

			bzero(dpart, sizeof(*dpart));
			dpart->media_offset   = (u_int64_t)sp->ds_offset *
						info->d_media_blksize;
			dpart->media_size     = (u_int64_t)sp->ds_size *
						info->d_media_blksize;
			dpart->media_blocks   = sp->ds_size;
			dpart->media_blksize  = info->d_media_blksize;
			dpart->skip_platform = sp->ds_skip_platform;
			dpart->skip_bsdlabel = sp->ds_skip_bsdlabel;
			if (lp && slice != WHOLE_DISK_SLICE) {
				struct partition *p;

				p = &lp->d_partitions[dkpart(dev)];
				dpart->fstype = p->p_fstype;
				dpart->media_offset += (u_int64_t)p->p_offset *
						       info->d_media_blksize;
				dpart->media_size = (u_int64_t)p->p_size *
						    info->d_media_blksize;

				/*
				 * partition starting sector (p_offset)
				 * requires slice's reserved areas to be
				 * adjusted.
				 */
				if (dpart->skip_platform > p->p_offset)
					dpart->skip_platform -= p->p_offset;
				else
					dpart->skip_platform = 0;
				if (dpart->skip_bsdlabel > p->p_offset)
					dpart->skip_bsdlabel -= p->p_offset;
				else
					dpart->skip_bsdlabel = 0;
			}
		}
		return (0);

	case DIOCGSLICEINFO:
		bcopy(ssp, data, (char *)&ssp->dss_slices[ssp->dss_nslices] -
				 (char *)ssp);
		return (0);

	case DIOCSDINFO:
		if (slice == WHOLE_DISK_SLICE)
			return (ENODEV);
		if (!(flags & FWRITE))
			return (EBADF);
		lp = kmalloc(sizeof *lp, M_DEVBUF, M_WAITOK);
		if (sp->ds_label == NULL)
			bzero(lp, sizeof *lp);
		else
			bcopy(sp->ds_label, lp, sizeof *lp);
		if (sp->ds_label == NULL) {
			openmask = 0;
		} else {
			openmask = sp->ds_openmask;
			if (slice == COMPATIBILITY_SLICE) {
				openmask |= ssp->dss_slices[
				    ssp->dss_first_bsd_slice].ds_openmask;
			} else if (slice == ssp->dss_first_bsd_slice) {
				openmask |= ssp->dss_slices[
				    COMPATIBILITY_SLICE].ds_openmask;
			}
		}
		error = setdisklabel(lp, (struct disklabel *)data,
				     (u_long)openmask);
		/* XXX why doesn't setdisklabel() check this? */
		if (error == 0 && lp->d_partitions[RAW_PART].p_offset != 0)
			error = EXDEV;
		if (error == 0) {
			if (lp->d_secperunit > sp->ds_size)
				error = ENOSPC;
			for (part = 0; part < lp->d_npartitions; part++)
				if (lp->d_partitions[part].p_size > sp->ds_size)
					error = ENOSPC;
		}
		if (error != 0) {
			kfree(lp, M_DEVBUF);
			return (error);
		}
		free_ds_label(ssp, slice);
		set_ds_label(ssp, slice, lp);
		return (0);

	case DIOCSYNCSLICEINFO:
		if (slice != WHOLE_DISK_SLICE || dkpart(dev) != RAW_PART)
			return (EINVAL);
		if (*(int *)data == 0) {
			for (slice = 0; slice < ssp->dss_nslices; slice++) {
				openmask = ssp->dss_slices[slice].ds_openmask;
				if (openmask &&
				    (slice != WHOLE_DISK_SLICE || 
				     openmask & ~(1 << RAW_PART))) {
					return (EBUSY);
				}
			}
		}

		/*
		 * Temporarily forget the current slices struct and read
		 * the current one.
		 *
		 * NOTE:
		 *
		 * XXX should wait for current accesses on this disk to
		 * complete, then lock out future accesses and opens.
		 */
		*sspp = NULL;
		lp = kmalloc(sizeof *lp, M_DEVBUF, M_WAITOK);
		*lp = *ssp->dss_slices[WHOLE_DISK_SLICE].ds_label;
		error = dsopen(dev, S_IFCHR, ssp->dss_oflags, sspp, info);
		if (error != 0) {
			kfree(lp, M_DEVBUF);
			*sspp = ssp;
			return (error);
		}

		/*
		 * Reopen everything.  This is a no-op except in the "force"
		 * case and when the raw bdev and cdev are both open.  Abort
		 * if anything fails.
		 */
		for (slice = 0; slice < ssp->dss_nslices; slice++) {
			for (openmask = ssp->dss_slices[slice].ds_openmask,
			     part = 0; openmask; openmask >>= 1, part++) {
				if (!(openmask & 1))
					continue;
				error = dsopen(dkmodslice(dkmodpart(dev, part),
							  slice),
					       S_IFCHR, ssp->dss_oflags, sspp,
					       info);
				if (error != 0) {
					kfree(lp, M_DEVBUF);
					*sspp = ssp;
					return (EBUSY);
				}
			}
		}

		kfree(lp, M_DEVBUF);
		dsgone(&ssp);
		return (0);

	case DIOCWDINFO:
		error = dsioctl(dev, DIOCSDINFO, data, flags, &ssp, info);
		if (error != 0)
			return (error);
		/*
		 * XXX this used to hack on dk_openpart to fake opening
		 * partition 0 in case that is used instead of dkpart(dev).
		 */
		old_wlabel = sp->ds_wlabel;
		set_ds_wlabel(ssp, slice, TRUE);
		error = writedisklabel(dev, sp->ds_label);
		/* XXX should invalidate in-core label if write failed. */
		set_ds_wlabel(ssp, slice, old_wlabel);
		return (error);

	case DIOCWLABEL:
		if (slice == WHOLE_DISK_SLICE)
			return (ENODEV);
		if (!(flags & FWRITE))
			return (EBADF);
		set_ds_wlabel(ssp, slice, *(int *)data != 0);
		return (0);

	default:
		return (ENOIOCTL);
	}
}

/*
 * Chain the bio_done.  b_cmd remains valid through such chaining.
 */
static void
dsiodone(struct bio *bio)
{
	struct buf *bp = bio->bio_buf;
	char *msg;

	if (bp->b_cmd != BUF_CMD_READ
	    || (!(bp->b_flags & B_ERROR) && bp->b_error == 0)) {
		msg = fixlabel(NULL, bio->bio_caller_info1.ptr,
			       (struct disklabel *)
			       (bp->b_data + (int)bio->bio_caller_info2.offset),
			       FALSE);
		if (msg != NULL)
			kprintf("%s\n", msg);
	}
	biodone(bio->bio_prev);
}

int
dsisopen(struct diskslices *ssp)
{
	int slice;

	if (ssp == NULL)
		return (0);
	for (slice = 0; slice < ssp->dss_nslices; slice++) {
		if (ssp->dss_slices[slice].ds_openmask)
			return (1);
	}
	return (0);
}

/*
 * Allocate a slices "struct" and initialize it to contain only an empty
 * compatibility slice (pointing to itself), a whole disk slice (covering
 * the disk as described by the label), and (nslices - BASE_SLICES) empty
 * slices beginning at BASE_SLICE.
 */
struct diskslices *
dsmakeslicestruct(int nslices, struct disk_info *info)
{
	struct diskslice *sp;
	struct diskslices *ssp;

	ssp = kmalloc(offsetof(struct diskslices, dss_slices) +
		     nslices * sizeof *sp, M_DEVBUF, M_WAITOK);
	ssp->dss_first_bsd_slice = COMPATIBILITY_SLICE;
	ssp->dss_nslices = nslices;
	ssp->dss_oflags = 0;
	ssp->dss_secmult = info->d_media_blksize / DEV_BSIZE;
	if (ssp->dss_secmult & (ssp->dss_secmult - 1))
		ssp->dss_secshift = -1;
	else
		ssp->dss_secshift = ffs(ssp->dss_secmult) - 1;
	ssp->dss_secsize = info->d_media_blksize;
	sp = &ssp->dss_slices[0];
	bzero(sp, nslices * sizeof *sp);
	sp[WHOLE_DISK_SLICE].ds_size = info->d_media_blocks;
	return (ssp);
}

char *
dsname(cdev_t dev, int unit, int slice, int part, char *partname)
{
	static char name[32];
	const char *dname;

	dname = dev_dname(dev);
	if (strlen(dname) > 16)
		dname = "nametoolong";
	ksnprintf(name, sizeof(name), "%s%d", dname, unit);
	partname[0] = '\0';
	if (slice != WHOLE_DISK_SLICE || part != RAW_PART) {
		partname[0] = 'a' + part;
		partname[1] = '\0';
		if (slice != COMPATIBILITY_SLICE) {
			ksnprintf(name + strlen(name),
			    sizeof(name) - strlen(name), "s%d", slice - 1);
		}
	}
	return (name);
}

/*
 * This should only be called when the unit is inactive and the strategy
 * routine should not allow it to become active unless we call it.  Our
 * strategy routine must be special to allow activity.
 */
int
dsopen(cdev_t dev, int mode, u_int flags, 
	struct diskslices **sspp, struct disk_info *info)
{
	cdev_t dev1;
	int error;
	struct disklabel *lp1;
	char *msg;
	u_char mask;
	bool_t need_init;
	int part;
	char partname[2];
	int slice;
	char *sname;
	struct diskslice *sp;
	struct diskslices *ssp;
	int unit;

	dev->si_bsize_phys = info->d_media_blksize;

	unit = dkunit(dev);
	if (info->d_media_blksize % DEV_BSIZE) {
		kprintf("%s: invalid sector size %lu\n", devtoname(dev),
		    (u_long)info->d_media_blksize);
		return (EINVAL);
	}

	/*
	 * Do not attempt to read the slice table or disk label when
	 * accessing the raw disk.
	 */
	if (dkslice(dev) == WHOLE_DISK_SLICE && dkpart(dev) == RAW_PART) {
		flags |= DSO_ONESLICE | DSO_NOLABELS;
	}

	/*
	 * XXX reinitialize the slice table unless there is an open device
	 * on the unit.  This should only be done if the media has changed.
	 */
	ssp = *sspp;
	need_init = !dsisopen(ssp);
	if (ssp != NULL && need_init)
		dsgone(sspp);
	if (need_init) {
		/*
		 * Allocate a minimal slices "struct".  This will become
		 * the final slices "struct" if we don't want real slices
		 * or if we can't find any real slices.
		 *
		 * Then scan the disk
		 */
		*sspp = dsmakeslicestruct(BASE_SLICE, info);

		if (!(flags & DSO_ONESLICE)) {
			TRACE(("mbrinit\n"));
			error = mbrinit(dev, info, sspp);
			if (error != 0) {
				dsgone(sspp);
				return (error);
			}
		}
		ssp = *sspp;
		ssp->dss_oflags = flags;

		/*
		 * If there are no real slices, then make the compatiblity
		 * slice cover the whole disk.
		 *
		 * no sectors are reserved for the platform (ds_skip_platform
		 * will be 0) in this case.  This means that if a disklabel
		 * is installed it will be directly installed in sector 0.
		 */
		if (ssp->dss_nslices == BASE_SLICE) {
			ssp->dss_slices[COMPATIBILITY_SLICE].ds_size
				= info->d_media_blocks;
		}

		/*
		 * Point the compatibility slice at the BSD slice, if any. 
		 */
		for (slice = BASE_SLICE; slice < ssp->dss_nslices; slice++) {
			sp = &ssp->dss_slices[slice];
			if (sp->ds_type == DOSPTYP_386BSD /* XXX */) {
				struct diskslice *csp;

				csp = &ssp->dss_slices[COMPATIBILITY_SLICE];
				ssp->dss_first_bsd_slice = slice;
				csp->ds_offset = sp->ds_offset;
				csp->ds_size = sp->ds_size;
				csp->ds_type = sp->ds_type;
				csp->ds_skip_platform = sp->ds_skip_platform;
				csp->ds_skip_bsdlabel = sp->ds_skip_bsdlabel;
				break;
			}
		}

		/*
		 * By definition accesses via the whole-disk device do not
		 * specify any reserved areas.  The whole disk may be read
		 * or written by the whole-disk device.
		 *
		 * XXX do not set a label for the whole disk slice, the
		 * code should be able to operate without one once we
		 * fix the virgin label code.
		 */
		sp = &ssp->dss_slices[WHOLE_DISK_SLICE];
		sp->ds_label = clone_label(info, NULL);
		sp->ds_wlabel = TRUE;
		sp->ds_skip_platform = 0;
		sp->ds_skip_bsdlabel = 0;
	}

	/*
	 * Initialize secondary info for all slices.  It is needed for more
	 * than the current slice in the DEVFS case.  XXX DEVFS is no more.
	 *
	 * Attempt to read the disklabel for each slice, creating a virgin
	 * label if a slice does not have one.
	 */
	for (slice = 0; slice < ssp->dss_nslices; slice++) {
		sp = &ssp->dss_slices[slice];
		if (sp->ds_label != NULL)
			continue;
		dev1 = dkmodslice(dkmodpart(dev, RAW_PART), slice);
		sname = dsname(dev, unit, slice, RAW_PART, partname);
		/*
		 * XXX this should probably only be done for the need_init
		 * case, but there may be a problem with DIOCSYNCSLICEINFO.
		 */
		set_ds_wlabel(ssp, slice, TRUE);	/* XXX invert */
		lp1 = clone_label(info, sp);
		TRACE(("readdisklabel\n"));
		if (flags & DSO_NOLABELS) {
			msg = NULL;
		} else {
			msg = readdisklabel(dev1, lp1);

			/*
			 * readdisklabel() returns NULL for success, and an
			 * error string for failure.
			 *
			 * If there isn't a label on the disk, and if the
			 * DSO_COMPATLABEL is set, we want to use the
			 * faked-up label provided by the caller.
			 *
			 * So we set msg to NULL to indicate that there is
			 * no failure (since we have a faked-up label),
			 * free lp1, and then clone it again from lp.
			 * (In case readdisklabel() modified lp1.)
			 */
			if (msg != NULL && (flags & DSO_COMPATLABEL)) {
				msg = NULL;
				kfree(lp1, M_DEVBUF);
				lp1 = clone_label(info, sp);
			}
		}
		if (msg == NULL)
			msg = fixlabel(sname, sp, lp1, FALSE);
		if (msg == NULL && lp1->d_secsize != ssp->dss_secsize)
			msg = "inconsistent sector size";
		if (msg != NULL) {
			if (sp->ds_type == DOSPTYP_386BSD /* XXX */)
				log(LOG_WARNING, "%s: cannot find label (%s)\n",
				    sname, msg);
			kfree(lp1, M_DEVBUF);
			continue;
		}
		set_ds_label(ssp, slice, lp1);
		set_ds_wlabel(ssp, slice, FALSE);
	}

	slice = dkslice(dev);
	if (slice >= ssp->dss_nslices)
		return (ENXIO);
	sp = &ssp->dss_slices[slice];
	part = dkpart(dev);
	if (part != RAW_PART
	    && (sp->ds_label == NULL || part >= sp->ds_label->d_npartitions))
		return (EINVAL);	/* XXX needs translation */
	mask = 1 << part;
	sp->ds_openmask |= mask;
	return (0);
}

int64_t
dssize(cdev_t dev, struct diskslices **sspp)
{
	struct disklabel *lp;
	int part;
	int slice;
	struct diskslices *ssp;

	slice = dkslice(dev);
	part = dkpart(dev);
	ssp = *sspp;
	if (ssp == NULL || slice >= ssp->dss_nslices
	    || !(ssp->dss_slices[slice].ds_openmask & (1 << part))) {
		if (dev_dopen(dev, FREAD, S_IFCHR, proc0.p_ucred) != 0)
			return (-1);
		dev_dclose(dev, FREAD, S_IFCHR);
		ssp = *sspp;
	}
	lp = ssp->dss_slices[slice].ds_label;
	if (lp == NULL)
		return (-1);
	return ((int64_t)lp->d_partitions[part].p_size);
}

static void
free_ds_label(struct diskslices *ssp, int slice)
{
	struct disklabel *lp;
	struct diskslice *sp;

	sp = &ssp->dss_slices[slice];
	lp = sp->ds_label;
	if (lp == NULL)
		return;
	kfree(lp, M_DEVBUF);
	set_ds_label(ssp, slice, (struct disklabel *)NULL);
}

static char *
fixlabel(char *sname, struct diskslice *sp, struct disklabel *lp, int writeflag)
{
	u_int64_t start;
	u_int64_t end;
	u_int64_t offset;
	int part;
	struct partition *pp;
	bool_t warned;

	/* These errors "can't happen" so don't bother reporting details. */
	if (lp->d_magic != DISKMAGIC || lp->d_magic2 != DISKMAGIC)
		return ("fixlabel: invalid magic");
	if (dkcksum(lp) != 0)
		return ("fixlabel: invalid checksum");

	pp = &lp->d_partitions[RAW_PART];
	if (writeflag) {
		start = 0;
		offset = sp->ds_offset;
	} else {
		start = sp->ds_offset;
		offset = -sp->ds_offset;
	}
	if (pp->p_offset != start) {
		if (sname != NULL) {
			kprintf(
"%s: rejecting BSD label: raw partition offset != slice offset\n",
			       sname);
			slice_info(sname, sp);
			partition_info(sname, RAW_PART, pp);
		}
		return ("fixlabel: raw partition offset != slice offset");
	}
	if (pp->p_size != sp->ds_size) {
		if (sname != NULL) {
			kprintf("%s: raw partition size != slice size\n", sname);
			slice_info(sname, sp);
			partition_info(sname, RAW_PART, pp);
		}
		if (pp->p_size > sp->ds_size) {
			if (sname == NULL)
				return ("fixlabel: raw partition size > slice size");
			kprintf("%s: truncating raw partition\n", sname);
			pp->p_size = sp->ds_size;
		}
	}
	end = start + sp->ds_size;
	if (start > end)
		return ("fixlabel: slice wraps");
	if (lp->d_secpercyl <= 0)
		return ("fixlabel: d_secpercyl <= 0");
	pp -= RAW_PART;
	warned = FALSE;
	for (part = 0; part < lp->d_npartitions; part++, pp++) {
		if (pp->p_offset != 0 || pp->p_size != 0) {
			if (pp->p_offset < start
			    || pp->p_offset + pp->p_size > end
			    || pp->p_offset + pp->p_size < pp->p_offset) {
				if (sname != NULL) {
					kprintf(
"%s: rejecting partition in BSD label: it isn't entirely within the slice\n",
					       sname);
					if (!warned) {
						slice_info(sname, sp);
						warned = TRUE;
					}
					partition_info(sname, part, pp);
				}
				/* XXX else silently discard junk. */
				bzero(pp, sizeof *pp);
			} else
				pp->p_offset += offset;
		}
	}
	lp->d_ncylinders = sp->ds_size / lp->d_secpercyl;
	lp->d_secperunit = sp->ds_size;
 	lp->d_checksum = 0;
 	lp->d_checksum = dkcksum(lp);
	return (NULL);
}

static void
partition_info(char *sname, int part, struct partition *pp)
{
	kprintf("%s%c: start %lu, end %lu, size %lu\n", sname, 'a' + part,
	       (u_long)pp->p_offset, (u_long)(pp->p_offset + pp->p_size - 1),
	       (u_long)pp->p_size);
}

static void
slice_info(char *sname, struct diskslice *sp)
{
	kprintf("%s: start %llu, end %llu, size %llu\n", sname,
	       sp->ds_offset, sp->ds_offset + sp->ds_size - 1, sp->ds_size);
}

static void
set_ds_label(struct diskslices *ssp, int slice, struct disklabel *lp)
{
	struct diskslice *sp1 = &ssp->dss_slices[slice];
	struct diskslice *sp2;

	if (slice == COMPATIBILITY_SLICE)
		sp2 = &ssp->dss_slices[ssp->dss_first_bsd_slice];
	else if (slice == ssp->dss_first_bsd_slice)
		sp2 = &ssp->dss_slices[COMPATIBILITY_SLICE];
	else
		sp2 = NULL;
	sp1->ds_label = lp;
	if (sp2)
		sp2->ds_label = lp;

	/*
	 * If the slice is not the whole-disk slice, setup the reserved
	 * area(s).
	 *
	 * The reserved area for the original bsd disklabel, inclusive of
	 * the label and space for boot2, is 15 sectors.  If you've
	 * noticed people traditionally skipping 16 sectors its because
	 * the sector numbers start at the beginning of the slice rather
	 * then the beginning of the disklabel and traditional dos slices
	 * reserve a sector at the beginning for the boot code.
	 *
	 * NOTE! With the traditional bsdlabel, the first N bytes of boot2
	 * overlap with the disklabel.  The disklabel program checks that
	 * they are 0.
	 */
	if (slice != WHOLE_DISK_SLICE) {
		sp1->ds_skip_bsdlabel = sp1->ds_skip_platform + 15;
		if (sp2)
			sp2->ds_skip_bsdlabel = sp1->ds_skip_bsdlabel;
	}
}

static void
set_ds_wlabel(struct diskslices *ssp, int slice, int wlabel)
{
	ssp->dss_slices[slice].ds_wlabel = wlabel;
	if (slice == COMPATIBILITY_SLICE)
		ssp->dss_slices[ssp->dss_first_bsd_slice].ds_wlabel = wlabel;
	else if (slice == ssp->dss_first_bsd_slice)
		ssp->dss_slices[COMPATIBILITY_SLICE].ds_wlabel = wlabel;
}
