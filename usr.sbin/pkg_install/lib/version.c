/*
 * FreeBSD install - a package for the installation and maintenance
 * of non-core utilities.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Maxim Sobolev
 * 31 July 2001
 *
 * $FreeBSD: src/usr.sbin/pkg_install/lib/version.c,v 1.1.2.3 2002/08/20 06:35:08 obrien Exp $
 * $DragonFly: src/usr.sbin/pkg_install/lib/Attic/version.c,v 1.2 2003/06/17 04:29:59 dillon Exp $
 */

#include "lib.h"
#include <err.h>

/*
 * Routines to assist with PLIST_FMT_VER numbers in the packing
 * lists.
 *
 * Following is the PLIST_FMT_VER history:
 * 1.0 - Initial revision;
 * 1.1 - When recording/checking checksum of symlink use hash of readlink()
 *	 value instead of the hash of an object this links points to.
 *
 */
int
verscmp(Package *pkg, int major, int minor)
{
    int rval = 0;

    if ((pkg->fmtver_maj < major) || (pkg->fmtver_maj == major &&
	pkg->fmtver_mnr < minor))
	rval = -1;
    else if ((pkg->fmtver_maj > major) || (pkg->fmtver_maj == major &&
	     pkg->fmtver_mnr > minor))
	rval = 1;

    return rval;
}
