/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)vfs_syscalls.c	8.13 (Berkeley) 4/15/94
 * $FreeBSD: src/sys/kern/vfs_syscalls.c,v 1.151.2.18 2003/04/04 20:35:58 tegge Exp $
 * $DragonFly: src/sys/kern/vfs_syscalls.c,v 1.17 2003/09/23 05:03:51 dillon Exp $
 */

/* For 4.3 integer FS ID compatibility */
#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/sysent.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/sysproto.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/linker.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/extattr.h>

#include <machine/limits.h>
#include <vfs/union/union.h>
#include <sys/sysctl.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_zone.h>
#include <vm/vm_page.h>

#include <sys/file2.h>

static int change_dir (struct nameidata *ndp, struct thread *td);
static void checkdirs (struct vnode *olddp);
static int chroot_refuse_vdir_fds (struct filedesc *fdp);
static int getutimes (const struct timeval *, struct timespec *);
static int setfown (struct vnode *, uid_t, gid_t);
static int setfmode (struct vnode *, int);
static int setfflags (struct vnode *, int);
static int setutimes (struct vnode *, const struct timespec *, int);
static int	usermount = 0;	/* if 1, non-root can mount fs. */

int (*union_dircheckp) (struct thread *, struct vnode **, struct file *);

SYSCTL_INT(_vfs, OID_AUTO, usermount, CTLFLAG_RW, &usermount, 0, "");

/*
 * Virtual File System System Calls
 */

/*
 * Mount a file system.
 */
/*
 * mount_args(char *type, char *path, int flags, caddr_t data)
 */
/* ARGSUSED */
int
mount(struct mount_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct mount *mp;
	struct vfsconf *vfsp;
	int error, flag = 0, flag2 = 0;
	struct vattr va;
#ifdef COMPAT_43
	u_long fstypenum;
#endif
	struct nameidata nd;
	char fstypename[MFSNAMELEN];

	if (usermount == 0 && (error = suser(td)))
		return (error);
	/*
	 * Do not allow NFS export by non-root users.
	 */
	if (SCARG(uap, flags) & MNT_EXPORTED) {
		error = suser(td);
		if (error)
			return (error);
	}
	/*
	 * Silently enforce MNT_NOSUID and MNT_NODEV for non-root users
	 */
	if (suser(td)) 
		SCARG(uap, flags) |= MNT_NOSUID | MNT_NODEV;
	/*
	 * Get vnode to be covered
	 */
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;
	if (SCARG(uap, flags) & MNT_UPDATE) {
		if ((vp->v_flag & VROOT) == 0) {
			vput(vp);
			return (EINVAL);
		}
		mp = vp->v_mount;
		flag = mp->mnt_flag;
		flag2 = mp->mnt_kern_flag;
		/*
		 * We only allow the filesystem to be reloaded if it
		 * is currently mounted read-only.
		 */
		if ((SCARG(uap, flags) & MNT_RELOAD) &&
		    ((mp->mnt_flag & MNT_RDONLY) == 0)) {
			vput(vp);
			return (EOPNOTSUPP);	/* Needs translation */
		}
		/*
		 * Only root, or the user that did the original mount is
		 * permitted to update it.
		 */
		if (mp->mnt_stat.f_owner != p->p_ucred->cr_uid &&
		    (error = suser(td))) {
			vput(vp);
			return (error);
		}
		if (vfs_busy(mp, LK_NOWAIT, 0, td)) {
			vput(vp);
			return (EBUSY);
		}
		lwkt_gettoken(&vp->v_interlock);
		if ((vp->v_flag & VMOUNT) != 0 ||
		    vp->v_mountedhere != NULL) {
			lwkt_reltoken(&vp->v_interlock);
			vfs_unbusy(mp, td);
			vput(vp);
			return (EBUSY);
		}
		vp->v_flag |= VMOUNT;
		lwkt_reltoken(&vp->v_interlock);
		mp->mnt_flag |=
		    SCARG(uap, flags) & (MNT_RELOAD | MNT_FORCE | MNT_UPDATE);
		VOP_UNLOCK(vp, 0, td);
		goto update;
	}
	/*
	 * If the user is not root, ensure that they own the directory
	 * onto which we are attempting to mount.
	 */
	if ((error = VOP_GETATTR(vp, &va, td)) ||
	    (va.va_uid != p->p_ucred->cr_uid &&
	     (error = suser(td)))) {
		vput(vp);
		return (error);
	}
	if ((error = vinvalbuf(vp, V_SAVE, td, 0, 0)) != 0) {
		vput(vp);
		return (error);
	}
	if (vp->v_type != VDIR) {
		vput(vp);
		return (ENOTDIR);
	}
#ifdef COMPAT_43
	/*
	 * Historically filesystem types were identified by number. If we
	 * get an integer for the filesystem type instead of a string, we
	 * check to see if it matches one of the historic filesystem types.
	 */
	fstypenum = (uintptr_t)SCARG(uap, type);
	if (fstypenum < maxvfsconf) {
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == fstypenum)
				break;
		if (vfsp == NULL) {
			vput(vp);
			return (ENODEV);
		}
		strncpy(fstypename, vfsp->vfc_name, MFSNAMELEN);
	} else
#endif /* COMPAT_43 */
	if ((error = copyinstr(SCARG(uap, type), fstypename, MFSNAMELEN, NULL)) != 0) {
		vput(vp);
		return (error);
	}
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	if (vfsp == NULL) {
		linker_file_t lf;

		/* Only load modules for root (very important!) */
		if ((error = suser(td)) != 0) {
			vput(vp);
			return error;
		}
		error = linker_load_file(fstypename, &lf);
		if (error || lf == NULL) {
			vput(vp);
			if (lf == NULL)
				error = ENODEV;
			return error;
		}
		lf->userrefs++;
		/* lookup again, see if the VFS was loaded */
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (!strcmp(vfsp->vfc_name, fstypename))
				break;
		if (vfsp == NULL) {
			lf->userrefs--;
			linker_file_unload(lf);
			vput(vp);
			return (ENODEV);
		}
	}
	lwkt_gettoken(&vp->v_interlock);
	if ((vp->v_flag & VMOUNT) != 0 ||
	    vp->v_mountedhere != NULL) {
		lwkt_reltoken(&vp->v_interlock);
		vput(vp);
		return (EBUSY);
	}
	vp->v_flag |= VMOUNT;
	lwkt_reltoken(&vp->v_interlock);

	/*
	 * Allocate and initialize the filesystem.
	 */
	mp = malloc(sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	mp->mnt_nvnodelistsize = 0;
	lockinit(&mp->mnt_lock, 0, "vfslock", 0, LK_NOPAUSE);
	(void)vfs_busy(mp, LK_NOWAIT, 0, td);
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_vfc = vfsp;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_vnodecovered = vp;
	mp->mnt_stat.f_owner = p->p_ucred->cr_uid;
	mp->mnt_iosize_max = DFLTPHYS;
	VOP_UNLOCK(vp, 0, td);
update:
	/*
	 * Set the mount level flags.
	 */
	if (SCARG(uap, flags) & MNT_RDONLY)
		mp->mnt_flag |= MNT_RDONLY;
	else if (mp->mnt_flag & MNT_RDONLY)
		mp->mnt_kern_flag |= MNTK_WANTRDWR;
	mp->mnt_flag &=~ (MNT_NOSUID | MNT_NOEXEC | MNT_NODEV |
	    MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC | MNT_NOATIME |
	    MNT_NOSYMFOLLOW | MNT_IGNORE |
	    MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR);
	mp->mnt_flag |= SCARG(uap, flags) & (MNT_NOSUID | MNT_NOEXEC |
	    MNT_NODEV | MNT_SYNCHRONOUS | MNT_UNION | MNT_ASYNC | MNT_FORCE |
	    MNT_NOSYMFOLLOW | MNT_IGNORE |
	    MNT_NOATIME | MNT_NOCLUSTERR | MNT_NOCLUSTERW | MNT_SUIDDIR);
	/*
	 * Mount the filesystem.
	 * XXX The final recipients of VFS_MOUNT just overwrite the ndp they
	 * get.  No freeing of cn_pnbuf.
	 */
	error = VFS_MOUNT(mp, SCARG(uap, path), SCARG(uap, data), &nd, td);
	if (mp->mnt_flag & MNT_UPDATE) {
		if (mp->mnt_kern_flag & MNTK_WANTRDWR)
			mp->mnt_flag &= ~MNT_RDONLY;
		mp->mnt_flag &=~ (MNT_UPDATE | MNT_RELOAD | MNT_FORCE);
		mp->mnt_kern_flag &=~ MNTK_WANTRDWR;
		if (error) {
			mp->mnt_flag = flag;
			mp->mnt_kern_flag = flag2;
		}
		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			if (mp->mnt_syncer == NULL)
				error = vfs_allocate_syncvnode(mp);
		} else {
			if (mp->mnt_syncer != NULL)
				vrele(mp->mnt_syncer);
			mp->mnt_syncer = NULL;
		}
		vfs_unbusy(mp, td);
		lwkt_gettoken(&vp->v_interlock);
		vp->v_flag &= ~VMOUNT;
		lwkt_reltoken(&vp->v_interlock);
		vrele(vp);
		return (error);
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	/*
	 * Put the new filesystem on the mount list after root.
	 */
	cache_purge(vp);
	if (!error) {
		lwkt_gettoken(&vp->v_interlock);
		vp->v_flag &= ~VMOUNT;
		vp->v_mountedhere = mp;
		lwkt_reltoken(&vp->v_interlock);
		lwkt_gettoken(&mountlist_token);
		TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
		lwkt_reltoken(&mountlist_token);
		checkdirs(vp);
		VOP_UNLOCK(vp, 0, td);
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			error = vfs_allocate_syncvnode(mp);
		vfs_unbusy(mp, td);
		if ((error = VFS_START(mp, 0, td)) != 0)
			vrele(vp);
	} else {
		lwkt_gettoken(&vp->v_interlock);
		vp->v_flag &= ~VMOUNT;
		lwkt_reltoken(&vp->v_interlock);
		mp->mnt_vfc->vfc_refcount--;
		vfs_unbusy(mp, td);
		free((caddr_t)mp, M_MOUNT);
		vput(vp);
	}
	return (error);
}

/*
 * Scan all active processes to see if any of them have a current
 * or root directory onto which the new filesystem has just been
 * mounted. If so, replace them with the new mount point.
 */
static void
checkdirs(struct vnode *olddp)
{
	struct filedesc *fdp;
	struct vnode *newdp;
	struct proc *p;

	if (olddp->v_usecount == 1)
		return;
	if (VFS_ROOT(olddp->v_mountedhere, &newdp))
		panic("mount: lost mount");
	FOREACH_PROC_IN_SYSTEM(p) {
		fdp = p->p_fd;
		if (fdp->fd_cdir == olddp) {
			vrele(fdp->fd_cdir);
			VREF(newdp);
			fdp->fd_cdir = newdp;
		}
		if (fdp->fd_rdir == olddp) {
			vrele(fdp->fd_rdir);
			VREF(newdp);
			fdp->fd_rdir = newdp;
		}
	}
	if (rootvnode == olddp) {
		vrele(rootvnode);
		VREF(newdp);
		rootvnode = newdp;
	}
	vput(newdp);
}

/*
 * Unmount a file system.
 *
 * Note: unmount takes a path to the vnode mounted on as argument,
 * not special file (as before).
 */
/*
 * umount_args(char *path, int flags)
 */
/* ARGSUSED */
int
unmount(struct unmount_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct mount *mp;
	int error;
	struct nameidata nd;

	KKASSERT(p);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	mp = vp->v_mount;

	/*
	 * Only root, or the user that did the original mount is
	 * permitted to unmount this filesystem.
	 */
	if ((mp->mnt_stat.f_owner != p->p_ucred->cr_uid) &&
	    (error = suser(td))) {
		vput(vp);
		return (error);
	}

	/*
	 * Don't allow unmounting the root file system.
	 */
	if (mp->mnt_flag & MNT_ROOTFS) {
		vput(vp);
		return (EINVAL);
	}

	/*
	 * Must be the root of the filesystem
	 */
	if ((vp->v_flag & VROOT) == 0) {
		vput(vp);
		return (EINVAL);
	}
	vput(vp);
	return (dounmount(mp, SCARG(uap, flags), td));
}

/*
 * Do the actual file system unmount.
 */
int
dounmount(struct mount *mp, int flags, struct thread *td)
{
	struct vnode *coveredvp;
	int error;
	int async_flag;

	lwkt_gettoken(&mountlist_token);
	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		lwkt_reltoken(&mountlist_token);
		return (EBUSY);
	}
	mp->mnt_kern_flag |= MNTK_UNMOUNT;
	/* Allow filesystems to detect that a forced unmount is in progress. */
	if (flags & MNT_FORCE)
		mp->mnt_kern_flag |= MNTK_UNMOUNTF;
	error = lockmgr(&mp->mnt_lock, LK_DRAIN | LK_INTERLOCK |
	    ((flags & MNT_FORCE) ? 0 : LK_NOWAIT), &mountlist_token, td);
	if (error) {
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		if (mp->mnt_kern_flag & MNTK_MWAIT)
			wakeup((caddr_t)mp);
		return (error);
	}

	if (mp->mnt_flag & MNT_EXPUBLIC)
		vfs_setpublicfs(NULL, NULL, NULL);

	vfs_msync(mp, MNT_WAIT);
	async_flag = mp->mnt_flag & MNT_ASYNC;
	mp->mnt_flag &=~ MNT_ASYNC;
	cache_purgevfs(mp);	/* remove cache entries for this file sys */
	if (mp->mnt_syncer != NULL)
		vrele(mp->mnt_syncer);
	if (((mp->mnt_flag & MNT_RDONLY) ||
	     (error = VFS_SYNC(mp, MNT_WAIT, td)) == 0) ||
	    (flags & MNT_FORCE))
		error = VFS_UNMOUNT(mp, flags, td);
	lwkt_gettoken(&mountlist_token);
	if (error) {
		if ((mp->mnt_flag & MNT_RDONLY) == 0 && mp->mnt_syncer == NULL)
			(void) vfs_allocate_syncvnode(mp);
		mp->mnt_kern_flag &= ~(MNTK_UNMOUNT | MNTK_UNMOUNTF);
		mp->mnt_flag |= async_flag;
		lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK | LK_REENABLE,
		    &mountlist_token, td);
		if (mp->mnt_kern_flag & MNTK_MWAIT)
			wakeup((caddr_t)mp);
		return (error);
	}
	TAILQ_REMOVE(&mountlist, mp, mnt_list);
	if ((coveredvp = mp->mnt_vnodecovered) != NULLVP) {
		coveredvp->v_mountedhere = (struct mount *)0;
		vrele(coveredvp);
	}
	mp->mnt_vfc->vfc_refcount--;
	if (!TAILQ_EMPTY(&mp->mnt_nvnodelist))
		panic("unmount: dangling vnode");
	lockmgr(&mp->mnt_lock, LK_RELEASE | LK_INTERLOCK, &mountlist_token, td);
	if (mp->mnt_kern_flag & MNTK_MWAIT)
		wakeup((caddr_t)mp);
	free((caddr_t)mp, M_MOUNT);
	return (0);
}

/*
 * Sync each mounted filesystem.
 */

#ifdef DEBUG
static int syncprt = 0;
SYSCTL_INT(_debug, OID_AUTO, syncprt, CTLFLAG_RW, &syncprt, 0, "");
#endif

/* ARGSUSED */
int
sync(struct sync_args *uap)
{
	struct thread *td = curthread;
	struct mount *mp, *nmp;
	int asyncflag;

	lwkt_gettoken(&mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_token, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		if ((mp->mnt_flag & MNT_RDONLY) == 0) {
			asyncflag = mp->mnt_flag & MNT_ASYNC;
			mp->mnt_flag &= ~MNT_ASYNC;
			vfs_msync(mp, MNT_NOWAIT);
			VFS_SYNC(mp, MNT_NOWAIT, td);
			mp->mnt_flag |= asyncflag;
		}
		lwkt_gettoken(&mountlist_token);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&mountlist_token);
#if 0
/*
 * XXX don't call vfs_bufstats() yet because that routine
 * was not imported in the Lite2 merge.
 */
#ifdef DIAGNOSTIC
	if (syncprt)
		vfs_bufstats();
#endif /* DIAGNOSTIC */
#endif
	return (0);
}

/* XXX PRISON: could be per prison flag */
static int prison_quotas;
#if 0
SYSCTL_INT(_kern_prison, OID_AUTO, quotas, CTLFLAG_RW, &prison_quotas, 0, "");
#endif

/*
 *  quotactl_args(char *path, int fcmd, int uid, caddr_t arg)
 *
 * Change filesystem quotas.
 */
/* ARGSUSED */
int
quotactl(struct quotactl_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct mount *mp;
	int error;
	struct nameidata nd;

	KKASSERT(p);
	if (p->p_ucred->cr_prison && !prison_quotas)
		return (EPERM);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	mp = nd.ni_vp->v_mount;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vrele(nd.ni_vp);
	return (VFS_QUOTACTL(mp, SCARG(uap, cmd), SCARG(uap, uid),
	    SCARG(uap, arg), td));
}

/*
 * statfs_args(char *path, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
/* ARGSUSED */
int
statfs(struct statfs_args *uap)
{
	struct thread *td = curthread;
	struct mount *mp;
	struct statfs *sp;
	int error;
	struct nameidata nd;
	struct statfs sb;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	mp = nd.ni_vp->v_mount;
	sp = &mp->mnt_stat;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vrele(nd.ni_vp);
	error = VFS_STATFS(mp, sp, td);
	if (error)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	if (suser(td)) {
		bcopy((caddr_t)sp, (caddr_t)&sb, sizeof(sb));
		sb.f_fsid.val[0] = sb.f_fsid.val[1] = 0;
		sp = &sb;
	}
	return (copyout((caddr_t)sp, (caddr_t)SCARG(uap, buf), sizeof(*sp)));
}

/*
 * fstatfs_args(int fd, struct statfs *buf)
 *
 * Get filesystem statistics.
 */
/* ARGSUSED */
int
fstatfs(struct fstatfs_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	struct mount *mp;
	struct statfs *sp;
	int error;
	struct statfs sb;

	KKASSERT(p);
	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	mp = ((struct vnode *)fp->f_data)->v_mount;
	if (mp == NULL)
		return (EBADF);
	sp = &mp->mnt_stat;
	error = VFS_STATFS(mp, sp, td);
	if (error)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	if (suser(td)) {
		bcopy((caddr_t)sp, (caddr_t)&sb, sizeof(sb));
		sb.f_fsid.val[0] = sb.f_fsid.val[1] = 0;
		sp = &sb;
	}
	return (copyout((caddr_t)sp, (caddr_t)SCARG(uap, buf), sizeof(*sp)));
}

/*
 * getfsstat_args(struct statfs *buf, long bufsize, int flags)
 *
 * Get statistics on all filesystems.
 */
/* ARGSUSED */
int
getfsstat(struct getfsstat_args *uap)
{
	struct thread *td = curthread;
	struct mount *mp, *nmp;
	struct statfs *sp;
	caddr_t sfsp;
	long count, maxcount, error;

	maxcount = SCARG(uap, bufsize) / sizeof(struct statfs);
	sfsp = (caddr_t)SCARG(uap, buf);
	count = 0;
	lwkt_gettoken(&mountlist_token);
	for (mp = TAILQ_FIRST(&mountlist); mp != NULL; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_token, td)) {
			nmp = TAILQ_NEXT(mp, mnt_list);
			continue;
		}
		if (sfsp && count < maxcount) {
			sp = &mp->mnt_stat;
			/*
			 * If MNT_NOWAIT or MNT_LAZY is specified, do not
			 * refresh the fsstat cache. MNT_NOWAIT or MNT_LAZY
			 * overrides MNT_WAIT.
			 */
			if (((SCARG(uap, flags) & (MNT_LAZY|MNT_NOWAIT)) == 0 ||
			    (SCARG(uap, flags) & MNT_WAIT)) &&
			    (error = VFS_STATFS(mp, sp, td))) {
				lwkt_gettoken(&mountlist_token);
				nmp = TAILQ_NEXT(mp, mnt_list);
				vfs_unbusy(mp, td);
				continue;
			}
			sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
			error = copyout((caddr_t)sp, sfsp, sizeof(*sp));
			if (error) {
				vfs_unbusy(mp, td);
				return (error);
			}
			sfsp += sizeof(*sp);
		}
		count++;
		lwkt_gettoken(&mountlist_token);
		nmp = TAILQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, td);
	}
	lwkt_reltoken(&mountlist_token);
	if (sfsp && count > maxcount)
		uap->sysmsg_result = maxcount;
	else
		uap->sysmsg_result = count;
	return (0);
}

/*
 * fchdir_args(int fd)
 *
 * Change current working directory to a given file descriptor.
 */
/* ARGSUSED */
int
fchdir(struct fchdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct vnode *vp, *tdp;
	struct mount *mp;
	struct file *fp;
	int error;

	if ((error = getvnode(fdp, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = (struct vnode *)fp->f_data;
	VREF(vp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, p->p_ucred, td);
	while (!error && (mp = vp->v_mountedhere) != NULL) {
		if (vfs_busy(mp, 0, 0, td))
			continue;
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp, td);
		if (error)
			break;
		vput(vp);
		vp = tdp;
	}
	if (error) {
		vput(vp);
		return (error);
	}
	VOP_UNLOCK(vp, 0, td);
	vrele(fdp->fd_cdir);
	fdp->fd_cdir = vp;
	return (0);
}

/*
 * chdir_args(char *path)
 *
 * Change current working directory (``.'').
 */
/* ARGSUSED */
int
chdir(struct chdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = change_dir(&nd, td)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vrele(fdp->fd_cdir);
	fdp->fd_cdir = nd.ni_vp;
	return (0);
}

/*
 * Helper function for raised chroot(2) security function:  Refuse if
 * any filedescriptors are open directories.
 */
static int
chroot_refuse_vdir_fds(fdp)
	struct filedesc *fdp;
{
	struct vnode *vp;
	struct file *fp;
	int error;
	int fd;

	for (fd = 0; fd < fdp->fd_nfiles ; fd++) {
		error = getvnode(fdp, fd, &fp);
		if (error)
			continue;
		vp = (struct vnode *)fp->f_data;
		if (vp->v_type != VDIR)
			continue;
		return(EPERM);
	}
	return (0);
}

/*
 * This sysctl determines if we will allow a process to chroot(2) if it
 * has a directory open:
 *	0: disallowed for all processes.
 *	1: allowed for processes that were not already chroot(2)'ed.
 *	2: allowed for all processes.
 */

static int chroot_allow_open_directories = 1;

SYSCTL_INT(_kern, OID_AUTO, chroot_allow_open_directories, CTLFLAG_RW,
     &chroot_allow_open_directories, 0, "");

/*
 * chroot_args(char *path)
 *
 * Change notion of root (``/'') directory.
 */
/* ARGSUSED */
int
chroot(struct chroot_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	int error;
	struct nameidata nd;

	KKASSERT(p);
	error = suser_cred(p->p_ucred, PRISON_ROOT);
	if (error)
		return (error);
	if (chroot_allow_open_directories == 0 ||
	    (chroot_allow_open_directories == 1 && fdp->fd_rdir != rootvnode))
		error = chroot_refuse_vdir_fds(fdp);
	if (error)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = change_dir(&nd, td)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vrele(fdp->fd_rdir);
	fdp->fd_rdir = nd.ni_vp;
	if (!fdp->fd_jdir) {
		fdp->fd_jdir = nd.ni_vp;
                VREF(fdp->fd_jdir);
	}
	return (0);
}

/*
 * Common routine for chroot and chdir.
 */
static int
change_dir(struct nameidata *ndp, struct thread *td)
{
	struct vnode *vp;
	int error;

	error = namei(ndp);
	if (error)
		return (error);
	vp = ndp->ni_vp;
	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, ndp->ni_cnd.cn_cred, td);
	if (error)
		vput(vp);
	else
		VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * open_args(char *path, int flags, int mode)
 *
 * Check permissions, allocate an open file structure,
 * and call the device open routine if any.
 */
int
open(struct open_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vnode *vp;
	int cmode, flags, oflags;
	struct file *nfp;
	int type, indx, error;
	struct flock lf;
	struct nameidata nd;

	oflags = SCARG(uap, flags);
	if ((oflags & O_ACCMODE) == O_ACCMODE)
		return (EINVAL);
	flags = FFLAGS(oflags);
	error = falloc(p, &nfp, &indx);
	if (error)
		return (error);
	fp = nfp;
	cmode = ((SCARG(uap, mode) &~ fdp->fd_cmask) & ALLPERMS) &~ S_ISTXT;
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	p->p_dupfd = -indx - 1;			/* XXX check for fdopen */
	/*
	 * Bump the ref count to prevent another process from closing
	 * the descriptor while we are blocked in vn_open()
	 */
	fhold(fp);
	error = vn_open(&nd, flags, cmode);
	if (error) {
		/*
		 * release our own reference
		 */
		fdrop(fp, td);

		/*
		 * handle special fdopen() case.  bleh.  dupfdopen() is
		 * responsible for dropping the old contents of ofiles[indx]
		 * if it succeeds.
		 */
		if ((error == ENODEV || error == ENXIO) &&
		    p->p_dupfd >= 0 &&			/* XXX from fdopen */
		    (error =
			dupfdopen(fdp, indx, p->p_dupfd, flags, error)) == 0) {
			uap->sysmsg_result = indx;
			return (0);
		}
		/*
		 * Clean up the descriptor, but only if another thread hadn't
		 * replaced or closed it.
		 */
		if (fdp->fd_ofiles[indx] == fp) {
			fdp->fd_ofiles[indx] = NULL;
			fdrop(fp, td);
		}

		if (error == ERESTART)
			error = EINTR;
		return (error);
	}
	p->p_dupfd = 0;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;

	/*
	 * There should be 2 references on the file, one from the descriptor
	 * table, and one for us.
	 *
	 * Handle the case where someone closed the file (via its file
	 * descriptor) while we were blocked.  The end result should look
	 * like opening the file succeeded but it was immediately closed.
	 */
	if (fp->f_count == 1) {
		KASSERT(fdp->fd_ofiles[indx] != fp,
		    ("Open file descriptor lost all refs"));
		VOP_UNLOCK(vp, 0, td);
		vn_close(vp, flags & FMASK, td);
		fdrop(fp, td);
		uap->sysmsg_result = indx;
		return 0;
	}

	fp->f_data = (caddr_t)vp;
	fp->f_flag = flags & FMASK;
	fp->f_ops = &vnops;
	fp->f_type = (vp->v_type == VFIFO ? DTYPE_FIFO : DTYPE_VNODE);
	if (flags & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (flags & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((flags & FNONBLOCK) == 0)
			type |= F_WAIT;
		VOP_UNLOCK(vp, 0, td);
		if ((error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type)) != 0) {
			/*
			 * lock request failed.  Normally close the descriptor
			 * but handle the case where someone might have dup()d
			 * it when we weren't looking.  One reference is
			 * owned by the descriptor array, the other by us.
			 */
			if (fdp->fd_ofiles[indx] == fp) {
				fdp->fd_ofiles[indx] = NULL;
				fdrop(fp, td);
			}
			fdrop(fp, td);
			return (error);
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
		fp->f_flag |= FHASLOCK;
	}
	/* assert that vn_open created a backing object if one is needed */
	KASSERT(!vn_canvmio(vp) || VOP_GETVOBJECT(vp, NULL) == 0,
		("open: vmio vnode has no backing object after vn_open"));
	VOP_UNLOCK(vp, 0, td);

	/*
	 * release our private reference, leaving the one associated with the
	 * descriptor table intact.
	 */
	fdrop(fp, td);
	uap->sysmsg_result = indx;
	return (0);
}

#ifdef COMPAT_43
/*
 * ocreat(char *path, int mode)
 *
 * Create a file.
 */
int
ocreat(struct ocreat_args *uap)
{
	struct open_args /* {
		syscallarg(char *) path;
		syscallarg(int) flags;
		syscallarg(int) mode;
	} */ nuap;

	SCARG(&nuap, path) = SCARG(uap, path);
	SCARG(&nuap, mode) = SCARG(uap, mode);
	SCARG(&nuap, flags) = O_WRONLY | O_CREAT | O_TRUNC;
	return (open(&nuap));
}
#endif /* COMPAT_43 */

/*
 * mknod_args(char *path, int mode, int dev)
 *
 * Create a special file.
 */
/* ARGSUSED */
int
mknod(struct mknod_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	int whiteout = 0;
	struct nameidata nd;

	KKASSERT(p);

	switch (SCARG(uap, mode) & S_IFMT) {
	case S_IFCHR:
	case S_IFBLK:
		error = suser(td);
		break;
	default:
		error = suser_cred(p->p_ucred, PRISON_ROOT);
		break;
	}
	if (error)
		return (error);
	bwillwrite();
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT, UIO_USERSPACE,
		SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL)
		error = EEXIST;
	else {
		VATTR_NULL(&vattr);
		vattr.va_mode = (SCARG(uap, mode) & ALLPERMS) &~ p->p_fd->fd_cmask;
		vattr.va_rdev = SCARG(uap, dev);
		whiteout = 0;

		switch (SCARG(uap, mode) & S_IFMT) {
		case S_IFMT:	/* used by badsect to flag bad sectors */
			vattr.va_type = VBAD;
			break;
		case S_IFCHR:
			vattr.va_type = VCHR;
			break;
		case S_IFBLK:
			vattr.va_type = VBLK;
			break;
		case S_IFWHT:
			whiteout = 1;
			break;
		default:
			error = EINVAL;
			break;
		}
	}
	if (!error) {
		VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
		if (whiteout)
			error = VOP_WHITEOUT(nd.ni_dvp, &nd.ni_cnd, NAMEI_CREATE);
		else {
			error = VOP_MKNOD(nd.ni_dvp, &nd.ni_vp,
						&nd.ni_cnd, &vattr);
			if (error == 0)
				vput(nd.ni_vp);
		}
		NDFREE(&nd, NDF_ONLY_PNBUF);
		vput(nd.ni_dvp);
	} else {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (vp)
			vrele(vp);
	}
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "mknod");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "mknod");
	return (error);
}

/*
 * mkfifo_args(char *path, int mode)
 *
 * Create a named pipe.
 */
/* ARGSUSED */
int
mkfifo(struct mkfifo_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT, UIO_USERSPACE,
		SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	if (nd.ni_vp != NULL) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		return (EEXIST);
	}
	VATTR_NULL(&vattr);
	vattr.va_type = VFIFO;
	vattr.va_mode = (SCARG(uap, mode) & ALLPERMS) &~ p->p_fd->fd_cmask;
	VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
	error = VOP_MKNOD(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	if (error == 0)
		vput(nd.ni_vp);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(nd.ni_dvp);
	return (error);
}

/*
 * link_args(char *path, char *link)
 *
 * Make a hard file link.
 */
/* ARGSUSED */
int
link(struct link_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct nameidata nd;
	int error;

	bwillwrite();
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_NOOBJ, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;
	if (vp->v_type == VDIR)
		error = EPERM;		/* POSIX */
	else {
		NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT | CNP_NOOBJ,
			UIO_USERSPACE, SCARG(uap, link), td);
		error = namei(&nd);
		if (!error) {
			if (nd.ni_vp != NULL) {
				if (nd.ni_vp)
					vrele(nd.ni_vp);
				error = EEXIST;
			} else {
				VOP_LEASE(nd.ni_dvp, td, p->p_ucred,
				    LEASE_WRITE);
				VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
				error = VOP_LINK(nd.ni_dvp, vp, &nd.ni_cnd);
			}
			NDFREE(&nd, NDF_ONLY_PNBUF);
			if (nd.ni_dvp == nd.ni_vp)
				vrele(nd.ni_dvp);
			else
				vput(nd.ni_dvp);
		}
	}
	vrele(vp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "link");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "link");
	return (error);
}

/*
 * symlink(char *path, char *link)
 *
 * Make a symbolic link.
 */
/* ARGSUSED */
int
symlink(struct symlink_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	char *path;
	int error;
	struct nameidata nd;

	path = zalloc(namei_zone);
	if ((error = copyinstr(SCARG(uap, path), path, MAXPATHLEN, NULL)) != 0)
		goto out;
	bwillwrite();
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT | CNP_NOOBJ, UIO_USERSPACE,
		SCARG(uap, link), td);
	if ((error = namei(&nd)) != 0)
		goto out;
	if (nd.ni_vp) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(nd.ni_vp);
		error = EEXIST;
		goto out;
	}
	VATTR_NULL(&vattr);
	vattr.va_mode = ACCESSPERMS &~ p->p_fd->fd_cmask;
	VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
	error = VOP_SYMLINK(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr, path);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error == 0)
		vput(nd.ni_vp);
	vput(nd.ni_dvp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "symlink");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "symlink");
out:
	zfree(namei_zone, path);
	return (error);
}

/*
 * undelete_args(char *path)
 *
 * Delete a whiteout from the filesystem.
 */
/* ARGSUSED */
int
undelete(struct undelete_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, NAMEI_DELETE, CNP_LOCKPARENT | CNP_DOWHITEOUT, UIO_USERSPACE,
	    SCARG(uap, path), td);
	error = namei(&nd);
	if (error)
		return (error);

	if (nd.ni_vp != NULLVP || !(nd.ni_cnd.cn_flags & CNP_ISWHITEOUT)) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		if (nd.ni_vp)
			vrele(nd.ni_vp);
		return (EEXIST);
	}

	VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
	error = VOP_WHITEOUT(nd.ni_dvp, &nd.ni_cnd, NAMEI_DELETE);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(nd.ni_dvp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "undelete");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "undelete");
	return (error);
}

/*
 * unlink_args(char *path)
 *
 * Delete a name from the filesystem.
 */
int
unlink(struct unlink_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, NAMEI_DELETE, CNP_LOCKPARENT, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);

	if (vp->v_type == VDIR)
		error = EPERM;		/* POSIX */
	else {
		/*
		 * The root of a mounted filesystem cannot be deleted.
		 *
		 * XXX: can this only be a VDIR case?
		 */
		if (vp->v_flag & VROOT)
			error = EBUSY;
	}

	if (!error) {
		VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
		error = VOP_REMOVE(nd.ni_dvp, vp, &nd.ni_cnd);
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_dvp == vp)
		vrele(nd.ni_dvp);
	else
		vput(nd.ni_dvp);
	if (vp != NULLVP)
		vput(vp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "unlink");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "unlink");
	return (error);
}

/*
 * lseek_args(int fd, int pad, off_t offset, int whence)
 *
 * Reposition read/write file offset.
 */
int
lseek(struct lseek_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct vattr vattr;
	int error;

	if ((u_int)SCARG(uap, fd) >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[SCARG(uap, fd)]) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE)
		return (ESPIPE);
	switch (SCARG(uap, whence)) {
	case L_INCR:
		fp->f_offset += SCARG(uap, offset);
		break;
	case L_XTND:
		error=VOP_GETATTR((struct vnode *)fp->f_data, &vattr, td);
		if (error)
			return (error);
		fp->f_offset = SCARG(uap, offset) + vattr.va_size;
		break;
	case L_SET:
		fp->f_offset = SCARG(uap, offset);
		break;
	default:
		return (EINVAL);
	}
	uap->sysmsg_offset = fp->f_offset;
	return (0);
}

#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
/*
 * Reposition read/write file offset.
 *
 * olseek_args(int fd, long offset, int whence)
 */
int
olseek(struct olseek_args *uap)
{
	struct lseek_args /* {
		syscallarg(int) fd;
		syscallarg(int) pad;
		syscallarg(off_t) offset;
		syscallarg(int) whence;
	} */ nuap;
	int error;

	SCARG(&nuap, fd) = SCARG(uap, fd);
	SCARG(&nuap, offset) = SCARG(uap, offset);
	SCARG(&nuap, whence) = SCARG(uap, whence);
	error = lseek(&nuap);
	return (error);
}
#endif /* COMPAT_43 */

/*
 * access_args(char *path, int flags)
 *
 * Check access permissions.
 */
int
access(struct access_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct ucred *cred, *tmpcred;
	struct vnode *vp;
	int error, flags;
	struct nameidata nd;

	cred = p->p_ucred;
	/*
	 * Create and modify a temporary credential instead of one that
	 * is potentially shared.  This could also mess up socket
	 * buffer accounting which can run in an interrupt context.
	 */
	tmpcred = crdup(cred);
	tmpcred->cr_uid = p->p_ucred->cr_ruid;
	tmpcred->cr_groups[0] = p->p_ucred->cr_rgid;
	p->p_ucred = tmpcred;
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		goto out1;
	vp = nd.ni_vp;

	/* Flags == 0 means only check for existence. */
	if (SCARG(uap, flags)) {
		flags = 0;
		if (SCARG(uap, flags) & R_OK)
			flags |= VREAD;
		if (SCARG(uap, flags) & W_OK)
			flags |= VWRITE;
		if (SCARG(uap, flags) & X_OK)
			flags |= VEXEC;
		if ((flags & VWRITE) == 0 || (error = vn_writechk(vp)) == 0)
			error = VOP_ACCESS(vp, flags, tmpcred, td);
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(vp);
out1:
	p->p_ucred = cred;
	crfree(tmpcred);
	return (error);
}

#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
/*
 * ostat_args(char *path, struct ostat *ub)
 *
 * Get file status; this version follows links.
 */
/* ARGSUSED */
int
ostat(struct ostat_args *uap)
{
	struct thread *td = curthread;
	struct stat sb;
	struct ostat osb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = vn_stat(nd.ni_vp, &sb, td);
	vput(nd.ni_vp);
	if (error)
		return (error);
	cvtstat(&sb, &osb);
	error = copyout((caddr_t)&osb, (caddr_t)SCARG(uap, ub), sizeof (osb));
	return (error);
}

/*
 * olstat_args(char *path, struct ostat *ub)
 *
 * Get file status; this version does not follow links.
 */
/* ARGSUSED */
int
olstat(struct olstat_args *uap)
{
	struct thread *td = curthread;
	struct vnode *vp;
	struct stat sb;
	struct ostat osb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	error = vn_stat(vp, &sb, td);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(vp);
	if (error)
		return (error);
	cvtstat(&sb, &osb);
	error = copyout((caddr_t)&osb, (caddr_t)SCARG(uap, ub), sizeof (osb));
	return (error);
}

/*
 * Convert from an old to a new stat structure.
 */
void
cvtstat(st, ost)
	struct stat *st;
	struct ostat *ost;
{
	ost->st_dev = st->st_dev;
	ost->st_ino = st->st_ino;
	ost->st_mode = st->st_mode;
	ost->st_nlink = st->st_nlink;
	ost->st_uid = st->st_uid;
	ost->st_gid = st->st_gid;
	ost->st_rdev = st->st_rdev;
	if (st->st_size < (quad_t)1 << 32)
		ost->st_size = st->st_size;
	else
		ost->st_size = -2;
	ost->st_atime = st->st_atime;
	ost->st_mtime = st->st_mtime;
	ost->st_ctime = st->st_ctime;
	ost->st_blksize = st->st_blksize;
	ost->st_blocks = st->st_blocks;
	ost->st_flags = st->st_flags;
	ost->st_gen = st->st_gen;
}
#endif /* COMPAT_43 || COMPAT_SUNOS */

/*
 * stat_args(char *path, struct stat *ub)
 *
 * Get file status; this version follows links.
 */
/* ARGSUSED */
int
stat(struct stat_args *uap)
{
	struct thread *td = curthread;
	struct stat sb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	error = vn_stat(nd.ni_vp, &sb, td);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(nd.ni_vp);
	if (error)
		return (error);
	error = copyout((caddr_t)&sb, (caddr_t)SCARG(uap, ub), sizeof (sb));
	return (error);
}

/*
 * lstat_args(char *path, struct stat *ub)
 *
 * Get file status; this version does not follow links.
 */
/* ARGSUSED */
int
lstat(struct lstat_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct vnode *vp;
	struct stat sb;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	error = vn_stat(vp, &sb, td);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(vp);
	if (error)
		return (error);
	error = copyout((caddr_t)&sb, (caddr_t)SCARG(uap, ub), sizeof (sb));
	return (error);
}

void
cvtnstat(sb, nsb)
	struct stat *sb;
	struct nstat *nsb;
{
	nsb->st_dev = sb->st_dev;
	nsb->st_ino = sb->st_ino;
	nsb->st_mode = sb->st_mode;
	nsb->st_nlink = sb->st_nlink;
	nsb->st_uid = sb->st_uid;
	nsb->st_gid = sb->st_gid;
	nsb->st_rdev = sb->st_rdev;
	nsb->st_atimespec = sb->st_atimespec;
	nsb->st_mtimespec = sb->st_mtimespec;
	nsb->st_ctimespec = sb->st_ctimespec;
	nsb->st_size = sb->st_size;
	nsb->st_blocks = sb->st_blocks;
	nsb->st_blksize = sb->st_blksize;
	nsb->st_flags = sb->st_flags;
	nsb->st_gen = sb->st_gen;
	nsb->st_qspare[0] = sb->st_qspare[0];
	nsb->st_qspare[1] = sb->st_qspare[1];
}

/*
 * nstat_args(char *path, struct nstat *ub)
 */
/* ARGSUSED */
int
nstat(struct nstat_args *uap)
{
	struct thread *td = curthread;
	struct stat sb;
	struct nstat nsb;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = vn_stat(nd.ni_vp, &sb, td);
	vput(nd.ni_vp);
	if (error)
		return (error);
	cvtnstat(&sb, &nsb);
	error = copyout((caddr_t)&nsb, (caddr_t)SCARG(uap, ub), sizeof (nsb));
	return (error);
}

/*
 * lstat_args(char *path, struct stat *ub)
 *
 * Get file status; this version does not follow links.
 */
/* ARGSUSED */
int
nlstat(struct nlstat_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct vnode *vp;
	struct stat sb;
	struct nstat nsb;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = vn_stat(vp, &sb, td);
	vput(vp);
	if (error)
		return (error);
	cvtnstat(&sb, &nsb);
	error = copyout((caddr_t)&nsb, (caddr_t)SCARG(uap, ub), sizeof (nsb));
	return (error);
}

/*
 * pathconf_Args(char *path, int name)
 *
 * Get configurable pathname variables.
 */
/* ARGSUSED */
int
pathconf(struct pathconf_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = VOP_PATHCONF(nd.ni_vp, SCARG(uap, name), uap->sysmsg_fds);
	vput(nd.ni_vp);
	return (error);
}

/*
 * readlink_args(char *path, char *buf, int count)
 *
 * Return target name of a symbolic link.
 */
/* ARGSUSED */
int
readlink(struct readlink_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct iovec aiov;
	struct uio auio;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW | CNP_LOCKLEAF | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;
	if (vp->v_type != VLNK)
		error = EINVAL;
	else {
		aiov.iov_base = SCARG(uap, buf);
		aiov.iov_len = SCARG(uap, count);
		auio.uio_iov = &aiov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = 0;
		auio.uio_rw = UIO_READ;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_td = td;
		auio.uio_resid = SCARG(uap, count);
		error = VOP_READLINK(vp, &auio, p->p_ucred);
	}
	vput(vp);
	uap->sysmsg_result = SCARG(uap, count) - auio.uio_resid;
	return (error);
}

static int
setfflags(struct vnode *vp, int flags)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	/*
	 * Prevent non-root users from setting flags on devices.  When
	 * a device is reused, users can retain ownership of the device
	 * if they are allowed to set flags and programs assume that
	 * chown can't fail when done as root.
	 */
	if ((vp->v_type == VCHR || vp->v_type == VBLK) && 
	    ((error = suser_cred(p->p_ucred, PRISON_ROOT)) != 0))
		return (error);

	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	VATTR_NULL(&vattr);
	vattr.va_flags = flags;
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * chflags(char *path, int flags)
 *
 * Change flags of a file given a path name.
 */
/* ARGSUSED */
int
chflags(struct chflags_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setfflags(nd.ni_vp, SCARG(uap, flags));
	vrele(nd.ni_vp);
	return error;
}

/*
 * fchflags_args(int fd, int flags)
 *
 * Change flags of a file given a file descriptor.
 */
/* ARGSUSED */
int
fchflags(struct fchflags_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfflags((struct vnode *) fp->f_data, SCARG(uap, flags));
}

static int
setfmode(struct vnode *vp, int mode)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	VATTR_NULL(&vattr);
	vattr.va_mode = mode & ALLPERMS;
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
	VOP_UNLOCK(vp, 0, td);
	return error;
}

/*
 * chmod_args(char *path, int mode)
 *
 * Change mode of a file given path name.
 */
/* ARGSUSED */
int
chmod(struct chmod_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setfmode(nd.ni_vp, SCARG(uap, mode));
	vrele(nd.ni_vp);
	return error;
}

/*
 * lchmod_args(char *path, int mode)
 *
 * Change mode of a file given path name (don't follow links.)
 */
/* ARGSUSED */
int
lchmod(struct lchmod_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setfmode(nd.ni_vp, SCARG(uap, mode));
	vrele(nd.ni_vp);
	return error;
}

/*
 * fchmod_args(int fd, int mode)
 *
 * Change mode of a file given a file descriptor.
 */
/* ARGSUSED */
int
fchmod(struct fchmod_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfmode((struct vnode *)fp->f_data, SCARG(uap, mode));
}

static int
setfown(struct vnode *vp, uid_t uid, gid_t gid)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	VATTR_NULL(&vattr);
	vattr.va_uid = uid;
	vattr.va_gid = gid;
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
	VOP_UNLOCK(vp, 0, td);
	return error;
}

/*
 * chown(char *path, int uid, int gid)
 *
 * Set ownership given a path name.
 */
/* ARGSUSED */
int
chown(struct chown_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setfown(nd.ni_vp, SCARG(uap, uid), SCARG(uap, gid));
	vrele(nd.ni_vp);
	return (error);
}

/*
 * lchown_args(char *path, int uid, int gid)
 *
 * Set ownership given a path name, do not cross symlinks.
 */
/* ARGSUSED */
int
lchown(struct lchown_args *uap)
{
	struct thread *td = curthread;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setfown(nd.ni_vp, SCARG(uap, uid), SCARG(uap, gid));
	vrele(nd.ni_vp);
	return (error);
}

/*
 * fchown_args(int fd, int uid, int gid)
 *
 * Set ownership given a file descriptor.
 */
/* ARGSUSED */
int
fchown(struct fchown_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file *fp;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setfown((struct vnode *)fp->f_data,
		SCARG(uap, uid), SCARG(uap, gid));
}

static int
getutimes(const struct timeval *usrtvp, struct timespec *tsp)
{
	struct timeval tv[2];
	int error;

	if (usrtvp == NULL) {
		microtime(&tv[0]);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		tsp[1] = tsp[0];
	} else {
		if ((error = copyin(usrtvp, tv, sizeof (tv))) != 0)
			return (error);
		TIMEVAL_TO_TIMESPEC(&tv[0], &tsp[0]);
		TIMEVAL_TO_TIMESPEC(&tv[1], &tsp[1]);
	}
	return 0;
}

static int
setutimes(struct vnode *vp, const struct timespec *ts, int nullflag)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	int error;
	struct vattr vattr;

	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	VATTR_NULL(&vattr);
	vattr.va_atime = ts[0];
	vattr.va_mtime = ts[1];
	if (nullflag)
		vattr.va_vaflags |= VA_UTIMES_NULL;
	error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
	VOP_UNLOCK(vp, 0, td);
	return error;
}

/*
 * utimes_args(char *path, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
/* ARGSUSED */
int
utimes(struct utimes_args *uap)
{
	struct thread *td = curthread;
	struct timespec ts[2];
	struct timeval *usrtvp;
	int error;
	struct nameidata nd;

	usrtvp = SCARG(uap, tptr);
	if ((error = getutimes(usrtvp, ts)) != 0)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setutimes(nd.ni_vp, ts, usrtvp == NULL);
	vrele(nd.ni_vp);
	return (error);
}

/*
 * lutimes_args(char *path, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
/* ARGSUSED */
int
lutimes(struct lutimes_args *uap)
{
	struct thread *td = curthread;
	struct timespec ts[2];
	struct timeval *usrtvp;
	int error;
	struct nameidata nd;

	usrtvp = SCARG(uap, tptr);
	if ((error = getutimes(usrtvp, ts)) != 0)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_NOFOLLOW, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	error = setutimes(nd.ni_vp, ts, usrtvp == NULL);
	vrele(nd.ni_vp);
	return (error);
}

/*
 * futimes_args(int fd, struct timeval *tptr)
 *
 * Set the access and modification times of a file.
 */
/* ARGSUSED */
int
futimes(struct futimes_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct timespec ts[2];
	struct file *fp;
	struct timeval *usrtvp;
	int error;

	usrtvp = SCARG(uap, tptr);
	if ((error = getutimes(usrtvp, ts)) != 0)
		return (error);
	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	return setutimes((struct vnode *)fp->f_data, ts, usrtvp == NULL);
}

/*
 * truncate(char *path, int pad, off_t length)
 *
 * Truncate a file given its path name.
 */
/* ARGSUSED */
int
truncate(struct truncate_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	if (uap->length < 0)
		return(EINVAL);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (vp->v_type == VDIR)
		error = EISDIR;
	else if ((error = vn_writechk(vp)) == 0 &&
	    (error = VOP_ACCESS(vp, VWRITE, p->p_ucred, td)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = SCARG(uap, length);
		error = VOP_SETATTR(vp, &vattr, p->p_ucred, td);
	}
	vput(vp);
	return (error);
}

/*
 * ftruncate_args(int fd, int pad, off_t length)
 *
 * Truncate a file given a file descriptor.
 */
/* ARGSUSED */
int
ftruncate(struct ftruncate_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vattr vattr;
	struct vnode *vp;
	struct file *fp;
	int error;

	if (uap->length < 0)
		return(EINVAL);
	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	if ((fp->f_flag & FWRITE) == 0)
		return (EINVAL);
	vp = (struct vnode *)fp->f_data;
	VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (vp->v_type == VDIR)
		error = EISDIR;
	else if ((error = vn_writechk(vp)) == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = SCARG(uap, length);
		error = VOP_SETATTR(vp, &vattr, fp->f_cred, td);
	}
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
/*
 * otruncate_args(char *path, long length)
 *
 * Truncate a file given its path name.
 */
/* ARGSUSED */
int
otruncate(struct otruncate_args *uap)
{
	struct truncate_args /* {
		syscallarg(char *) path;
		syscallarg(int) pad;
		syscallarg(off_t) length;
	} */ nuap;

	SCARG(&nuap, path) = SCARG(uap, path);
	SCARG(&nuap, length) = SCARG(uap, length);
	return (truncate(&nuap));
}

/*
 * oftruncate_args(int fd, long length)
 *
 * Truncate a file given a file descriptor.
 */
/* ARGSUSED */
int
oftruncate(struct oftruncate_args *uap)
{
	struct ftruncate_args /* {
		syscallarg(int) fd;
		syscallarg(int) pad;
		syscallarg(off_t) length;
	} */ nuap;

	SCARG(&nuap, fd) = SCARG(uap, fd);
	SCARG(&nuap, length) = SCARG(uap, length);
	return (ftruncate(&nuap));
}
#endif /* COMPAT_43 || COMPAT_SUNOS */

/*
 * fsync(int fd)
 *
 * Sync an open file.
 */
/* ARGSUSED */
int
fsync(struct fsync_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	vm_object_t obj;
	int error;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	vp = (struct vnode *)fp->f_data;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	if (VOP_GETVOBJECT(vp, &obj) == 0)
		vm_object_page_clean(obj, 0, 0, 0);
	if ((error = VOP_FSYNC(vp, MNT_WAIT, td)) == 0 &&
	    vp->v_mount && (vp->v_mount->mnt_flag & MNT_SOFTDEP) &&
	    bioops.io_fsync)
		error = (*bioops.io_fsync)(vp);
	VOP_UNLOCK(vp, 0, td);
	return (error);
}

/*
 * rename_args(char *from, char *to)
 *
 * Rename files.  Source and destination must either both be directories,
 * or both not be directories.  If target is a directory, it must be empty.
 */
/* ARGSUSED */
int
rename(struct rename_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *tvp, *fvp, *tdvp;
	struct nameidata fromnd, tond;
	int error;

	bwillwrite();
	NDINIT(&fromnd, NAMEI_DELETE, CNP_WANTPARENT | CNP_SAVESTART,
		UIO_USERSPACE, SCARG(uap, from), td);
	if ((error = namei(&fromnd)) != 0)
		return (error);
	fvp = fromnd.ni_vp;
	NDINIT(&tond, NAMEI_RENAME, 
	    CNP_LOCKPARENT | CNP_LOCKLEAF | CNP_NOCACHE |
	     CNP_SAVESTART | CNP_NOOBJ,
	    UIO_USERSPACE, SCARG(uap, to), td);
	if (fromnd.ni_vp->v_type == VDIR)
		tond.ni_cnd.cn_flags |= CNP_WILLBEDIR;
	if ((error = namei(&tond)) != 0) {
		/* Translate error code for rename("dir1", "dir2/."). */
		if (error == EISDIR && fvp->v_type == VDIR)
			error = EINVAL;
		NDFREE(&fromnd, NDF_ONLY_PNBUF);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
		goto out1;
	}
	tdvp = tond.ni_dvp;
	tvp = tond.ni_vp;
	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
	}
	if (fvp == tdvp)
		error = EINVAL;
	/*
	 * If the source is the same as the destination (that is, if they
	 * are links to the same vnode), then there is nothing to do.
	 */
	if (fvp == tvp)
		error = -1;
out:
	if (!error) {
		VOP_LEASE(tdvp, td, p->p_ucred, LEASE_WRITE);
		if (fromnd.ni_dvp != tdvp) {
			VOP_LEASE(fromnd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
		}
		if (tvp) {
			VOP_LEASE(tvp, td, p->p_ucred, LEASE_WRITE);
		}
		error = VOP_RENAME(fromnd.ni_dvp, fromnd.ni_vp, &fromnd.ni_cnd,
				   tond.ni_dvp, tond.ni_vp, &tond.ni_cnd);
		NDFREE(&fromnd, NDF_ONLY_PNBUF);
		NDFREE(&tond, NDF_ONLY_PNBUF);
	} else {
		NDFREE(&fromnd, NDF_ONLY_PNBUF);
		NDFREE(&tond, NDF_ONLY_PNBUF);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fromnd.ni_dvp);
		vrele(fvp);
	}
	vrele(tond.ni_startdir);
	ASSERT_VOP_UNLOCKED(fromnd.ni_dvp, "rename");
	ASSERT_VOP_UNLOCKED(fromnd.ni_vp, "rename");
	ASSERT_VOP_UNLOCKED(tond.ni_dvp, "rename");
	ASSERT_VOP_UNLOCKED(tond.ni_vp, "rename");
out1:
	if (fromnd.ni_startdir)
		vrele(fromnd.ni_startdir);
	if (error == -1)
		return (0);
	return (error);
}

/*
 * mkdir_args(char *path, int mode)
 *
 * Make a directory file.
 */
/* ARGSUSED */
int
mkdir(struct mkdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, NAMEI_CREATE, CNP_LOCKPARENT, UIO_USERSPACE,
		SCARG(uap, path), td);
	nd.ni_cnd.cn_flags |= CNP_WILLBEDIR;
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp != NULL) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		vrele(vp);
		return (EEXIST);
	}
	VATTR_NULL(&vattr);
	vattr.va_type = VDIR;
	vattr.va_mode = (SCARG(uap, mode) & ACCESSPERMS) &~ p->p_fd->fd_cmask;
	VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
	error = VOP_MKDIR(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vattr);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vput(nd.ni_dvp);
	if (!error)
		vput(nd.ni_vp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "mkdir");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "mkdir");
	return (error);
}

/*
 * rmdir_args(char *path)
 *
 * Remove a directory file.
 */
/* ARGSUSED */
int
rmdir(struct rmdir_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	int error;
	struct nameidata nd;

	bwillwrite();
	NDINIT(&nd, NAMEI_DELETE, CNP_LOCKPARENT | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}
	/*
	 * No rmdir "." please.
	 */
	if (nd.ni_dvp == vp) {
		error = EINVAL;
		goto out;
	}
	/*
	 * The root of a mounted filesystem cannot be deleted.
	 */
	if (vp->v_flag & VROOT)
		error = EBUSY;
	else {
		VOP_LEASE(nd.ni_dvp, td, p->p_ucred, LEASE_WRITE);
		VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
		error = VOP_RMDIR(nd.ni_dvp, nd.ni_vp, &nd.ni_cnd);
	}
out:
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_dvp == vp)
		vrele(nd.ni_dvp);
	else
		vput(nd.ni_dvp);
	if (vp != NULLVP)
		vput(vp);
	ASSERT_VOP_UNLOCKED(nd.ni_dvp, "rmdir");
	ASSERT_VOP_UNLOCKED(nd.ni_vp, "rmdir");
	return (error);
}

#ifdef COMPAT_43
/*
 * ogetdirentries_args(int fd, char *buf, u_int count, long *basep)
 *
 * Read a block of directory entries in a file system independent format.
 */
int
ogetdirentries(struct ogetdirentries_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	struct uio auio, kuio;
	struct iovec aiov, kiov;
	struct dirent *dp, *edp;
	caddr_t dirbuf;
	int error, eofflag, readcnt;
	long loff;

	/* XXX arbitrary sanity limit on `count'. */
	if (SCARG(uap, count) > 64 * 1024)
		return (EINVAL);
	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = SCARG(uap, buf);
	aiov.iov_len = SCARG(uap, count);
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_resid = SCARG(uap, count);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	loff = auio.uio_offset = fp->f_offset;
#	if (BYTE_ORDER != LITTLE_ENDIAN)
		if (vp->v_mount->mnt_maxsymlinklen <= 0) {
			error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag,
			    NULL, NULL);
			fp->f_offset = auio.uio_offset;
		} else
#	endif
	{
		kuio = auio;
		kuio.uio_iov = &kiov;
		kuio.uio_segflg = UIO_SYSSPACE;
		kiov.iov_len = SCARG(uap, count);
		MALLOC(dirbuf, caddr_t, SCARG(uap, count), M_TEMP, M_WAITOK);
		kiov.iov_base = dirbuf;
		error = VOP_READDIR(vp, &kuio, fp->f_cred, &eofflag,
			    NULL, NULL);
		fp->f_offset = kuio.uio_offset;
		if (error == 0) {
			readcnt = SCARG(uap, count) - kuio.uio_resid;
			edp = (struct dirent *)&dirbuf[readcnt];
			for (dp = (struct dirent *)dirbuf; dp < edp; ) {
#				if (BYTE_ORDER == LITTLE_ENDIAN)
					/*
					 * The expected low byte of
					 * dp->d_namlen is our dp->d_type.
					 * The high MBZ byte of dp->d_namlen
					 * is our dp->d_namlen.
					 */
					dp->d_type = dp->d_namlen;
					dp->d_namlen = 0;
#				else
					/*
					 * The dp->d_type is the high byte
					 * of the expected dp->d_namlen,
					 * so must be zero'ed.
					 */
					dp->d_type = 0;
#				endif
				if (dp->d_reclen > 0) {
					dp = (struct dirent *)
					    ((char *)dp + dp->d_reclen);
				} else {
					error = EIO;
					break;
				}
			}
			if (dp >= edp)
				error = uiomove(dirbuf, readcnt, &auio);
		}
		FREE(dirbuf, M_TEMP);
	}
	VOP_UNLOCK(vp, 0, td);
	if (error)
		return (error);
	if (SCARG(uap, count) == auio.uio_resid) {
		if (union_dircheckp) {
			error = union_dircheckp(td, &vp, fp);
			if (error == -1)
				goto unionread;
			if (error)
				return (error);
		}
		if ((vp->v_flag & VROOT) &&
		    (vp->v_mount->mnt_flag & MNT_UNION)) {
			struct vnode *tvp = vp;
			vp = vp->v_mount->mnt_vnodecovered;
			VREF(vp);
			fp->f_data = (caddr_t) vp;
			fp->f_offset = 0;
			vrele(tvp);
			goto unionread;
		}
	}
	error = copyout((caddr_t)&loff, (caddr_t)SCARG(uap, basep),
	    sizeof(long));
	uap->sysmsg_result = SCARG(uap, count) - auio.uio_resid;
	return (error);
}
#endif /* COMPAT_43 */

/*
 * getdirentries_args(int fd, char *buf, u_int conut, long *basep)
 *
 * Read a block of directory entries in a file system independent format.
 */
int
getdirentries(struct getdirentries_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct file *fp;
	struct uio auio;
	struct iovec aiov;
	long loff;
	int error, eofflag;

	if ((error = getvnode(p->p_fd, SCARG(uap, fd), &fp)) != 0)
		return (error);
	if ((fp->f_flag & FREAD) == 0)
		return (EBADF);
	vp = (struct vnode *)fp->f_data;
unionread:
	if (vp->v_type != VDIR)
		return (EINVAL);
	aiov.iov_base = SCARG(uap, buf);
	aiov.iov_len = SCARG(uap, count);
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_resid = SCARG(uap, count);
	/* vn_lock(vp, LK_SHARED | LK_RETRY, td); */
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
	loff = auio.uio_offset = fp->f_offset;
	error = VOP_READDIR(vp, &auio, fp->f_cred, &eofflag, NULL, NULL);
	fp->f_offset = auio.uio_offset;
	VOP_UNLOCK(vp, 0, td);
	if (error)
		return (error);
	if (SCARG(uap, count) == auio.uio_resid) {
		if (union_dircheckp) {
			error = union_dircheckp(td, &vp, fp);
			if (error == -1)
				goto unionread;
			if (error)
				return (error);
		}
		if ((vp->v_flag & VROOT) &&
		    (vp->v_mount->mnt_flag & MNT_UNION)) {
			struct vnode *tvp = vp;
			vp = vp->v_mount->mnt_vnodecovered;
			VREF(vp);
			fp->f_data = (caddr_t) vp;
			fp->f_offset = 0;
			vrele(tvp);
			goto unionread;
		}
	}
	if (SCARG(uap, basep) != NULL) {
		error = copyout((caddr_t)&loff, (caddr_t)SCARG(uap, basep),
		    sizeof(long));
	}
	uap->sysmsg_result = SCARG(uap, count) - auio.uio_resid;
	return (error);
}

/*
 * getdents_args(int fd, char *buf, size_t count)
 */
int
getdents(struct getdents_args *uap)
{
	struct getdirentries_args ap;

	ap.fd = uap->fd;
	ap.buf = uap->buf;
	ap.count = uap->count;
	ap.basep = NULL;
	return getdirentries(&ap);
}

/*
 * umask(int newmask)
 *
 * Set the mode mask for creation of filesystem nodes.
 *
 * MP SAFE
 */
int
umask(struct umask_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct filedesc *fdp;

	fdp = p->p_fd;
	uap->sysmsg_result = fdp->fd_cmask;
	fdp->fd_cmask = SCARG(uap, newmask) & ALLPERMS;
	return (0);
}

/*
 * revoke(char *path)
 *
 * Void all references to file by ripping underlying filesystem
 * away from vnode.
 */
/* ARGSUSED */
int
revoke(struct revoke_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct vnode *vp;
	struct vattr vattr;
	int error;
	struct nameidata nd;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (vp->v_type != VCHR && vp->v_type != VBLK) {
		error = EINVAL;
		goto out;
	}
	if ((error = VOP_GETATTR(vp, &vattr, td)) != 0)
		goto out;
	if (p->p_ucred->cr_uid != vattr.va_uid &&
	    (error = suser_cred(p->p_ucred, PRISON_ROOT)))
		goto out;
	if (vcount(vp) > 1)
		VOP_REVOKE(vp, REVOKEALL);
out:
	vrele(vp);
	return (error);
}

/*
 * Convert a user file descriptor to a kernel file entry.
 */
int
getvnode(struct filedesc *fdp, int fd, struct file **fpp)
{
	struct file *fp;

	if ((u_int)fd >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fd]) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_VNODE && fp->f_type != DTYPE_FIFO)
		return (EINVAL);
	*fpp = fp;
	return (0);
}
/*
 * getfh_args(char *fname, fhandle_t *fhp)
 *
 * Get (NFS) file handle
 */
int
getfh(struct getfh_args *uap)
{
	struct thread *td = curthread;
	struct nameidata nd;
	fhandle_t fh;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	error = suser(td);
	if (error)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE, uap->fname, td);
	error = namei(&nd);
	if (error)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;
	bzero(&fh, sizeof(fh));
	fh.fh_fsid = vp->v_mount->mnt_stat.f_fsid;
	error = VFS_VPTOFH(vp, &fh.fh_fid);
	vput(vp);
	if (error)
		return (error);
	error = copyout(&fh, uap->fhp, sizeof (fh));
	return (error);
}

/*
 * fhopen_args(const struct fhandle *u_fhp, int flags)
 *
 * syscall for the rpc.lockd to use to translate a NFS file handle into
 * an open descriptor.
 *
 * warning: do not remove the suser() call or this becomes one giant
 * security hole.
 */
int
fhopen(struct fhopen_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct mount *mp;
	struct vnode *vp;
	struct fhandle fhp;
	struct vattr vat;
	struct vattr *vap = &vat;
	struct flock lf;
	struct file *fp;
	struct filedesc *fdp = p->p_fd;
	int fmode, mode, error, type;
	struct file *nfp; 
	int indx;

	/*
	 * Must be super user
	 */
	error = suser(td);
	if (error)
		return (error);

	fmode = FFLAGS(SCARG(uap, flags));
	/* why not allow a non-read/write open for our lockd? */
	if (((fmode & (FREAD | FWRITE)) == 0) || (fmode & O_CREAT))
		return (EINVAL);
	error = copyin(SCARG(uap,u_fhp), &fhp, sizeof(fhp));
	if (error)
		return(error);
	/* find the mount point */
	mp = vfs_getvfs(&fhp.fh_fsid);
	if (mp == NULL)
		return (ESTALE);
	/* now give me my vnode, it gets returned to me locked */
	error = VFS_FHTOVP(mp, &fhp.fh_fid, &vp);
	if (error)
		return (error);
 	/*
	 * from now on we have to make sure not
	 * to forget about the vnode
	 * any error that causes an abort must vput(vp) 
	 * just set error = err and 'goto bad;'.
	 */

	/* 
	 * from vn_open 
	 */
	if (vp->v_type == VLNK) {
		error = EMLINK;
		goto bad;
	}
	if (vp->v_type == VSOCK) {
		error = EOPNOTSUPP;
		goto bad;
	}
	mode = 0;
	if (fmode & (FWRITE | O_TRUNC)) {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto bad;
		}
		error = vn_writechk(vp);
		if (error)
			goto bad;
		mode |= VWRITE;
	}
	if (fmode & FREAD)
		mode |= VREAD;
	if (mode) {
		error = VOP_ACCESS(vp, mode, p->p_ucred, td);
		if (error)
			goto bad;
	}
	if (fmode & O_TRUNC) {
		VOP_UNLOCK(vp, 0, td);				/* XXX */
		VOP_LEASE(vp, td, p->p_ucred, LEASE_WRITE);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);	/* XXX */
		VATTR_NULL(vap);
		vap->va_size = 0;
		error = VOP_SETATTR(vp, vap, p->p_ucred, td);
		if (error)
			goto bad;
	}
	error = VOP_OPEN(vp, fmode, p->p_ucred, td);
	if (error)
		goto bad;
	/*
	 * Make sure that a VM object is created for VMIO support.
	 */
	if (vn_canvmio(vp) == TRUE) {
		if ((error = vfs_object_create(vp, td)) != 0)
			goto bad;
	}
	if (fmode & FWRITE)
		vp->v_writecount++;

	/*
	 * end of vn_open code 
	 */

	if ((error = falloc(p, &nfp, &indx)) != 0) {
		if (fmode & FWRITE)
			vp->v_writecount--;
		goto bad;
	}
	fp = nfp;	

	/*
	 * hold an extra reference to avoid having fp ripped out
	 * from under us while we block in the lock op.
	 */
	fhold(fp);
	nfp->f_data = (caddr_t)vp;
	nfp->f_flag = fmode & FMASK;
	nfp->f_ops = &vnops;
	nfp->f_type = DTYPE_VNODE;
	if (fmode & (O_EXLOCK | O_SHLOCK)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (fmode & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((fmode & FNONBLOCK) == 0)
			type |= F_WAIT;
		VOP_UNLOCK(vp, 0, td);
		if ((error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type)) != 0) {
			/*
			 * lock request failed.  Normally close the descriptor
			 * but handle the case where someone might have dup()d
			 * or close()d it when we weren't looking.
			 */
			if (fdp->fd_ofiles[indx] == fp) {
				fdp->fd_ofiles[indx] = NULL;
				fdrop(fp, td);
			}

			/*
			 * release our private reference.
			 */
			fdrop(fp, td);
			return (error);
		}
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, td);
		fp->f_flag |= FHASLOCK;
	}
	if ((vp->v_type == VREG) && (VOP_GETVOBJECT(vp, NULL) != 0))
		vfs_object_create(vp, td);

	VOP_UNLOCK(vp, 0, td);
	fdrop(fp, td);
	uap->sysmsg_result = indx;
	return (0);

bad:
	vput(vp);
	return (error);
}

/*
 * fhstat_args(struct fhandle *u_fhp, struct stat *sb)
 */
int
fhstat(struct fhstat_args *uap)
{
	struct thread *td = curthread;
	struct stat sb;
	fhandle_t fh;
	struct mount *mp;
	struct vnode *vp;
	int error;

	/*
	 * Must be super user
	 */
	error = suser(td);
	if (error)
		return (error);
	
	error = copyin(SCARG(uap, u_fhp), &fh, sizeof(fhandle_t));
	if (error)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	error = vn_stat(vp, &sb, td);
	vput(vp);
	if (error)
		return (error);
	error = copyout(&sb, SCARG(uap, sb), sizeof(sb));
	return (error);
}

/*
 * fhstatfs_args(struct fhandle *u_fhp, struct statfs *buf)
 */
int
fhstatfs(struct fhstatfs_args *uap)
{
	struct thread *td = curthread;
	struct statfs *sp;
	struct mount *mp;
	struct vnode *vp;
	struct statfs sb;
	fhandle_t fh;
	int error;

	/*
	 * Must be super user
	 */
	if ((error = suser(td)))
		return (error);

	if ((error = copyin(SCARG(uap, u_fhp), &fh, sizeof(fhandle_t))) != 0)
		return (error);

	if ((mp = vfs_getvfs(&fh.fh_fsid)) == NULL)
		return (ESTALE);
	if ((error = VFS_FHTOVP(mp, &fh.fh_fid, &vp)))
		return (error);
	mp = vp->v_mount;
	sp = &mp->mnt_stat;
	vput(vp);
	if ((error = VFS_STATFS(mp, sp, td)) != 0)
		return (error);
	sp->f_flags = mp->mnt_flag & MNT_VISFLAGMASK;
	if (suser(td)) {
		bcopy((caddr_t)sp, (caddr_t)&sb, sizeof(sb));
		sb.f_fsid.val[0] = sb.f_fsid.val[1] = 0;
		sp = &sb;
	}
	return (copyout(sp, SCARG(uap, buf), sizeof(*sp)));
}

/*
 * Syscall to push extended attribute configuration information into the
 * VFS.  Accepts a path, which it converts to a mountpoint, as well as
 * a command (int cmd), and attribute name and misc data.  For now, the
 * attribute name is left in userspace for consumption by the VFS_op.
 * It will probably be changed to be copied into sysspace by the
 * syscall in the future, once issues with various consumers of the
 * attribute code have raised their hands.
 *
 * Currently this is used only by UFS Extended Attributes.
 */
int
extattrctl(struct extattrctl_args *uap)
{
	struct thread *td = curthread;
	struct nameidata nd;
	struct mount *mp;
	int error;

	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW, UIO_USERSPACE, SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	mp = nd.ni_vp->v_mount;
	NDFREE(&nd, 0);
	return (VFS_EXTATTRCTL(mp, SCARG(uap, cmd), SCARG(uap, attrname),
	    SCARG(uap, arg), td));
}

/*
 * Syscall to set a named extended attribute on a file or directory.
 * Accepts attribute name, and a uio structure pointing to the data to set.
 * The uio is consumed in the style of writev().  The real work happens
 * in VOP_SETEXTATTR().
 */
int
extattr_set_file(struct extattr_set_file_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct nameidata nd;
	struct uio auio;
	struct iovec *iov, *needfree = NULL, aiov[UIO_SMALLIOV];
	char attrname[EXTATTR_MAXNAMELEN];
	u_int iovlen, cnt;
	int error, i;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return(error);
	iovlen = uap->iovcnt * sizeof(struct iovec);
	if (uap->iovcnt > UIO_SMALLIOV) {
		if (uap->iovcnt > UIO_MAXIOV) {
			error = EINVAL;
			goto done;
		}
		MALLOC(iov, struct iovec *, iovlen, M_IOV, M_WAITOK);
		needfree = iov;
	} else
		iov = aiov;
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;
	if ((error = copyin((caddr_t)uap->iovp, (caddr_t)iov, iovlen)))
		goto done;
	auio.uio_resid = 0;
	for (i = 0; i < uap->iovcnt; i++) {
		if (iov->iov_len > INT_MAX - auio.uio_resid) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid += iov->iov_len;
		iov++;
	}
	cnt = auio.uio_resid;
	error = VOP_SETEXTATTR(nd.ni_vp, attrname, &auio, p->p_ucred, td);
	cnt -= auio.uio_resid;
	uap->sysmsg_result = cnt;
done:
	if (needfree)
		FREE(needfree, M_IOV);
	NDFREE(&nd, 0);
	return (error);
}

/*
 * Syscall to get a named extended attribute on a file or directory.
 * Accepts attribute name, and a uio structure pointing to a buffer for the
 * data.  The uio is consumed in the style of readv().  The real work
 * happens in VOP_GETEXTATTR();
 */
int
extattr_get_file(struct extattr_get_file_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct nameidata nd;
	struct uio auio;
	struct iovec *iov, *needfree, aiov[UIO_SMALLIOV];
	char attrname[EXTATTR_MAXNAMELEN];
	u_int iovlen, cnt;
	int error, i;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return (error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return (error);
	iovlen = uap->iovcnt * sizeof (struct iovec);
	if (uap->iovcnt > UIO_SMALLIOV) {
		if (uap->iovcnt > UIO_MAXIOV) {
			NDFREE(&nd, 0);
			return (EINVAL);
		}
		MALLOC(iov, struct iovec *, iovlen, M_IOV, M_WAITOK);
		needfree = iov;
	} else {
		iov = aiov;
		needfree = NULL;
	}
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovcnt;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;
	if ((error = copyin((caddr_t)uap->iovp, (caddr_t)iov, iovlen)))
		goto done;
	auio.uio_resid = 0;
	for (i = 0; i < uap->iovcnt; i++) {
		if (iov->iov_len > INT_MAX - auio.uio_resid) {
			error = EINVAL;
			goto done;
		}
		auio.uio_resid += iov->iov_len;
		iov++;
	}
	cnt = auio.uio_resid;
	error = VOP_GETEXTATTR(nd.ni_vp, attrname, &auio, p->p_ucred, td);
	cnt -= auio.uio_resid;
	uap->sysmsg_result = cnt;
done:
	if (needfree)
		FREE(needfree, M_IOV);
	NDFREE(&nd, 0);
	return(error);
}

/*
 * Syscall to delete a named extended attribute from a file or directory.
 * Accepts attribute name.  The real work happens in VOP_SETEXTATTR().
 */
int
extattr_delete_file(struct extattr_delete_file_args *uap)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct nameidata nd;
	char attrname[EXTATTR_MAXNAMELEN];
	int	error;

	error = copyin(SCARG(uap, attrname), attrname, EXTATTR_MAXNAMELEN);
	if (error)
		return(error);
	NDINIT(&nd, NAMEI_LOOKUP, CNP_FOLLOW | CNP_LOCKLEAF, UIO_USERSPACE,
	    SCARG(uap, path), td);
	if ((error = namei(&nd)) != 0)
		return(error);
	error = VOP_SETEXTATTR(nd.ni_vp, attrname, NULL, p->p_ucred, td);
	NDFREE(&nd, 0);
	return(error);
}
