/*
 * Copyright (c) 2002 Michael Shalayeff. All rights reserved.
 * Copyright (c) 2003 Ryan McBride. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * $FreeBSD: src/sbin/ifconfig/ifcarp.c,v 1.2 2005/02/22 14:07:47 glebius Exp $
 * $OpenBSD: ifconfig.c,v 1.82 2003/10/19 05:43:35 mcbride Exp $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <stdlib.h>
#include <unistd.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_carp.h>
#include <net/route.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include "ifconfig.h"

static const char *carp_states[] = { CARP_STATES };

void carp_status(int s);
void setcarp_advbase(const char *,int, int, const struct afswtch *rafp);
void setcarp_advskew(const char *, int, int, const struct afswtch *rafp);
void setcarp_passwd(const char *, int, int, const struct afswtch *rafp);
void setcarp_vhid(const char *, int, int, const struct afswtch *rafp);

void
carp_status(int s)
{
	const char *state;
	struct carpreq carpr;
	struct ifdrv ifd;
	char devname[IFNAMSIZ];

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		return;

	if (carpr.carpr_vhid > 0) {
		if (carpr.carpr_state > CARP_MAXSTATE)
			state = "<UNKNOWN>";
		else
			state = carp_states[carpr.carpr_state];

		printf("\tcarp: %s vhid %d advbase %d advskew %d\n",
		    state, carpr.carpr_vhid, carpr.carpr_advbase,
		    carpr.carpr_advskew);
	}

	memset(&ifd, 0, sizeof(ifd));
	strncpy(ifd.ifd_name, ifr.ifr_name, sizeof(ifd.ifd_name));
	ifd.ifd_cmd = CARPGDEVNAME;
	ifd.ifd_len = sizeof(devname);
	ifd.ifd_data = devname;
	if (ioctl(s, SIOCGDRVSPEC, &ifd) < 0)
		strlcpy(devname, "none", sizeof(devname));
	if (devname[0] != '\0')
		printf("\tcarpdev: %s\n", devname);
}

void
setcarp_passwd(const char *val, int d, int s, const struct afswtch *afp)
{
	struct carpreq carpr;

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCGVH");

	/* XXX Should hash the password into the key here, perhaps? */
	strlcpy(carpr.carpr_key, val, CARP_KEY_LEN);

	if (ioctl(s, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCSVH");
}

void
setcarp_vhid(const char *val, int d, int s, const struct afswtch *afp)
{
	int vhid;
	struct carpreq carpr;

	vhid = atoi(val);

	if (vhid <= 0)
		errx(1, "vhid must be greater than 0");

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCGVH");

	carpr.carpr_vhid = vhid;

	if (ioctl(s, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCSVH");
}

void
setcarp_advskew(const char *val, int d, int s, const struct afswtch *afp)
{
	int advskew;
	struct carpreq carpr;

	advskew = atoi(val);

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCGVH");

	carpr.carpr_advskew = advskew;

	if (ioctl(s, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCSVH");
}

void
setcarp_advbase(const char *val, int d, int s, const struct afswtch *afp)
{
	int advbase;
	struct carpreq carpr;

	advbase = atoi(val);

	memset((char *)&carpr, 0, sizeof(struct carpreq));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCGVH");

	carpr.carpr_advbase = advbase;

	if (ioctl(s, SIOCSVH, (caddr_t)&ifr) == -1)
		err(1, "SIOCSVH");
}

static void
getcarp_vhaddr(const char *val, int d, int s, const struct afswtch *afp)
{
#define VHADDR_PFMT	"%-15s %-15s %s\n"

	struct ifdrv ifd;
	struct ifcarpvhaddr *carpa;
	int count, i;

	memset(&ifd, 0, sizeof(ifd));
	strncpy(ifd.ifd_name, ifr.ifr_name, sizeof(ifd.ifd_name));
	ifd.ifd_cmd = CARPGVHADDR;
	if (ioctl(s, SIOCGDRVSPEC, &ifd) < 0)
		return;
	if (ifd.ifd_len != 0) {
		carpa = malloc(ifd.ifd_len);
		if (carpa == NULL)
			return;

		ifd.ifd_cmd = CARPGVHADDR;
		ifd.ifd_data = carpa;
		if (ioctl(s, SIOCGDRVSPEC, &ifd) < 0) {
			free(carpa);
			return;
		}
	} else {
		carpa = NULL;
	}
	count = ifd.ifd_len / sizeof(*carpa);
	if (count != 0)
		printf(VHADDR_PFMT, "virtual addr", "backing addr", "flags");
	for (i = 0; i < count; ++i) {
		char flags[16];
		char baddr[INET_ADDRSTRLEN];
		int a = 0;

		memset(flags, 0, sizeof(flags));
		flags[a] = '*';
		if (carpa[i].carpa_flags & CARP_VHAF_OWNER)
			flags[a++] = 'O';

		memset(baddr, 0, sizeof(baddr));
		baddr[0] = '*';
		if (carpa[i].carpa_baddr.sin_addr.s_addr != INADDR_ANY) {
			inet_ntop(AF_INET, &carpa[i].carpa_baddr.sin_addr,
				  baddr, sizeof(baddr));
		}

		printf(VHADDR_PFMT, inet_ntoa(carpa[i].carpa_addr.sin_addr),
		       baddr, flags);
	}
	if (carpa != NULL)
		free(carpa);

#undef VHADDR_PFMT
}

static struct cmd carp_cmds[] = {
	DEF_CMD_ARG("advbase",	setcarp_advbase),
	DEF_CMD_ARG("advskew",	setcarp_advskew),
	DEF_CMD_ARG("pass",	setcarp_passwd),
	DEF_CMD_ARG("vhid",	setcarp_vhid),
	DEF_CMD("vhaddr", 1,	getcarp_vhaddr)
};
static struct afswtch af_carp = {
	.af_name	= "af_carp",
	.af_af		= AF_UNSPEC,
	.af_other_status = carp_status,
};

static __constructor(101) void
carp_ctor(void)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	int i;

	for (i = 0; i < N(carp_cmds);  i++)
		cmd_register(&carp_cmds[i]);
	af_register(&af_carp);
#undef N
}
