#	From: @(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD: src/sbin/ifconfig/Makefile,v 1.29 2005/06/05 03:32:51 thompsa Exp $

PROG=	ifconfig

SRCS=	ifconfig.c		# base support

#
# NB: The order here defines the order in which the constructors
#     are called.  This in turn defines the default order in which
#     status is displayed.  Probably should add a priority mechanism
#     to the registration process so we don't depend on this aspect
#     of the toolchain.
#
SRCS+=	af_link.c		# LLC support
SRCS+=	af_inet.c		# IPv4 support
SRCS+=	af_inet6.c		# IPv6 support

SRCS+=	ifclone.c		# clone device support
#SRCS+=	ifmac.c			# MAC support
SRCS+=	ifmedia.c		# SIOC[GS]IFMEDIA support
SRCS+=	ifvlan.c		# SIOC[GS]ETVLAN support
SRCS+=	ifieee80211.c 		# SIOC[GS]IEEE80211 support
SRCS+=	regdomain.c

SRCS+=	ifcarp.c		# SIOC[GS]VH support
#SRCS+=	ifpfsync.c		# pfsync(4) support

SRCS+=	ifbridge.c		# bridge support

SRCS+=	af_ipx.c		# IPX support

DPADD=	${LIBIPX} ${LIBBSDXML} ${LIBSBUF}
LDADD=	-lipx -lbsdxml -lsbuf

MAN=	ifconfig.8

WARNS=	2

.include <bsd.prog.mk>
