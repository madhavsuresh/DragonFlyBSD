/* $FreeBSD: src/gnu/usr.bin/patch/INTERN.h,v 1.6 1999/09/05 17:31:54 peter Exp $
/* $DragonFly: src/gnu/usr.bin/patch/Attic/INTERN.h,v 1.2 2003/06/17 04:25:46 dillon Exp $
 *
 * $Log: INTERN.h,v $
 * Revision 2.0  86/09/17  15:35:58  lwall
 * Baseline for netwide release.
 *
 */

#ifdef EXT
#undef EXT
#endif
#define EXT

#ifdef INIT
#undef INIT
#endif
#define INIT(x) = x

#define DOINIT
