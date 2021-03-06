/*
 * Copyright (c) 1995, Mike Mitchell
 * Copyright (c) 1984, 1985, 1986, 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)spx_usrreq.h
 *
 * $FreeBSD: src/sys/netipx/spx_usrreq.c,v 1.27.2.1 2001/02/22 09:44:18 bp Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketvar2.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/route.h>
#include <netinet/tcp_fsm.h>

#include "ipx.h"
#include "ipx_pcb.h"
#include "ipx_var.h"
#include "spx.h"
#include "spx_timer.h"
#include "spx_var.h"
#include "spx_debug.h"

/*
 * SPX protocol implementation.
 */
static u_short 	spx_iss;
static u_short	spx_newchecks[50];
static int	spx_hardnosed;
static int	spx_use_delack = 0;
static int	traceallspxs = 0;
static struct	spx 	spx_savesi;
static struct	spx_istat spx_istat;

/* Following was struct spxstat spxstat; */
#ifndef spxstat 
#define spxstat spx_istat.newstats
#endif  

static int spx_backoff[SPX_MAXRXTSHIFT+1] =
    { 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

static	struct spxpcb *spx_close(struct spxpcb *cb);
static	struct spxpcb *spx_disconnect(struct spxpcb *cb);
static	struct spxpcb *spx_drop(struct spxpcb *cb, int errno);
static	int spx_output(struct spxpcb *cb, struct mbuf *m0);
static	int spx_reass(struct spxpcb *cb, struct spx *si, struct mbuf *si_m);
static	void spx_setpersist(struct spxpcb *cb);
static	void spx_template(struct spxpcb *cb);
static	struct spxpcb *spx_timers(struct spxpcb *cb, int timer);
static	struct spxpcb *spx_usrclosed(struct spxpcb *cb);

static	void spx_usr_abort(netmsg_t);
static	void spx_accept(netmsg_t);
static	void spx_attach(netmsg_t);
static	void spx_bind(netmsg_t);
static	void spx_connect(netmsg_t);
static	void spx_detach(netmsg_t);
static	void spx_usr_disconnect(netmsg_t);
static	void spx_listen(netmsg_t);
static	void spx_rcvd(netmsg_t);
static	void spx_rcvoob(netmsg_t);
static	void spx_send(netmsg_t);
static	void spx_shutdown(netmsg_t);
static	void spx_sp_attach(netmsg_t);

struct	pr_usrreqs spx_usrreqs = {
	.pru_abort = spx_usr_abort,
	.pru_accept = spx_accept,
	.pru_attach = spx_attach,
	.pru_bind = spx_bind,
	.pru_connect = spx_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = ipx_control,
	.pru_detach = spx_detach,
	.pru_disconnect = spx_usr_disconnect,
	.pru_listen = spx_listen,
	.pru_peeraddr = ipx_peeraddr,
	.pru_rcvd = spx_rcvd,
	.pru_rcvoob = spx_rcvoob,
	.pru_send = spx_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = spx_shutdown,
	.pru_sockaddr = ipx_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

struct	pr_usrreqs spx_usrreq_sps = {
	.pru_abort = spx_usr_abort,
	.pru_accept = spx_accept,
	.pru_attach = spx_sp_attach,
	.pru_bind = spx_bind,
	.pru_connect = spx_connect,
	.pru_connect2 = pr_generic_notsupp,
	.pru_control = ipx_control,
	.pru_detach = spx_detach,
	.pru_disconnect = spx_usr_disconnect,
	.pru_listen = spx_listen,
	.pru_peeraddr = ipx_peeraddr,
	.pru_rcvd = spx_rcvd,
	.pru_rcvoob = spx_rcvoob,
	.pru_send = spx_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = spx_shutdown,
	.pru_sockaddr = ipx_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive
};

static MALLOC_DEFINE(M_SPX_Q, "ipx_spx_q", "IPX Packet Management");

void
spx_init(void)
{

	spx_iss = 1; /* WRONG !! should fish it out of TODR */
}

void
spx_input(struct mbuf *m, struct ipxpcb *ipxp)
{
	struct spxpcb *cb;
	struct spx *si;
	struct socket *so;
	int dropsocket = 0;
	short ostate = 0;

	spxstat.spxs_rcvtotal++;
	if (ipxp == NULL) {
		panic("No ipxpcb in spx_input\n");
		return;
	}

	cb = ipxtospxpcb(ipxp);
	if (cb == NULL)
		goto bad;

	if (m->m_len < sizeof(struct spx)) {
		if ((m = m_pullup(m, sizeof(*si))) == NULL) {
			spxstat.spxs_rcvshort++;
			return;
		}
	}
	si = mtod(m, struct spx *);
	si->si_seq = ntohs(si->si_seq);
	si->si_ack = ntohs(si->si_ack);
	si->si_alo = ntohs(si->si_alo);

	so = ipxp->ipxp_socket;

	if (so->so_options & SO_DEBUG || traceallspxs) {
		ostate = cb->s_state;
		spx_savesi = *si;
	}
	if (so->so_options & SO_ACCEPTCONN) {
		struct spxpcb *ocb = cb;

		so = sonewconn(so, 0);
		if (so == NULL) {
			goto drop;
		}
		/*
		 * This is ugly, but ....
		 *
		 * Mark socket as temporary until we're
		 * committed to keeping it.  The code at
		 * ``drop'' and ``dropwithreset'' check the
		 * flag dropsocket to see if the temporary
		 * socket created here should be discarded.
		 * We mark the socket as discardable until
		 * we're committed to it below in TCPS_LISTEN.
		 */
		dropsocket++;
		ipxp = (struct ipxpcb *)so->so_pcb;
		ipxp->ipxp_laddr = si->si_dna;
		cb = ipxtospxpcb(ipxp);
		cb->s_mtu = ocb->s_mtu;		/* preserve sockopts */
		cb->s_flags = ocb->s_flags;	/* preserve sockopts */
		cb->s_flags2 = ocb->s_flags2;	/* preserve sockopts */
		cb->s_state = TCPS_LISTEN;
	}

	/*
	 * Packet received on connection.
	 * reset idle time and keep-alive timer;
	 */
	cb->s_idle = 0;
	cb->s_timer[SPXT_KEEP] = SPXTV_KEEP;

	switch (cb->s_state) {

	case TCPS_LISTEN:{
		struct sockaddr_ipx *sipx, ssipx;
		struct ipx_addr laddr;

		/*
		 * If somebody here was carying on a conversation
		 * and went away, and his pen pal thinks he can
		 * still talk, we get the misdirected packet.
		 */
		if (spx_hardnosed && (si->si_did != 0 || si->si_seq != 0)) {
			spx_istat.gonawy++;
			goto dropwithreset;
		}
		sipx = &ssipx;
		bzero(sipx, sizeof *sipx);
		sipx->sipx_len = sizeof(*sipx);
		sipx->sipx_family = AF_IPX;
		sipx->sipx_addr = si->si_sna;
		laddr = ipxp->ipxp_laddr;
		if (ipx_nullhost(laddr))
			ipxp->ipxp_laddr = si->si_dna;
		if (ipx_pcbconnect(ipxp, (struct sockaddr *)sipx, &thread0)) {
			ipxp->ipxp_laddr = laddr;
			spx_istat.noconn++;
			goto drop;
		}
		spx_template(cb);
		dropsocket = 0;		/* committed to socket */
		cb->s_did = si->si_sid;
		cb->s_rack = si->si_ack;
		cb->s_ralo = si->si_alo;
#define THREEWAYSHAKE
#ifdef THREEWAYSHAKE
		cb->s_state = TCPS_SYN_RECEIVED;
		cb->s_force = 1 + SPXT_KEEP;
		spxstat.spxs_accepts++;
		cb->s_timer[SPXT_KEEP] = SPXTV_KEEP;
		}
		break;
	/*
	 * This state means that we have heard a response
	 * to our acceptance of their connection
	 * It is probably logically unnecessary in this
	 * implementation.
	 */
	 case TCPS_SYN_RECEIVED: {
		if (si->si_did != cb->s_sid) {
			spx_istat.wrncon++;
			goto drop;
		}
#endif
		ipxp->ipxp_fport =  si->si_sport;
		cb->s_timer[SPXT_REXMT] = 0;
		cb->s_timer[SPXT_KEEP] = SPXTV_KEEP;
		soisconnected(so);
		cb->s_state = TCPS_ESTABLISHED;
		spxstat.spxs_accepts++;
		}
		break;

	/*
	 * This state means that we have gotten a response
	 * to our attempt to establish a connection.
	 * We fill in the data from the other side,
	 * telling us which port to respond to, instead of the well-
	 * known one we might have sent to in the first place.
	 * We also require that this is a response to our
	 * connection id.
	 */
	case TCPS_SYN_SENT:
		if (si->si_did != cb->s_sid) {
			spx_istat.notme++;
			goto drop;
		}
		spxstat.spxs_connects++;
		cb->s_did = si->si_sid;
		cb->s_rack = si->si_ack;
		cb->s_ralo = si->si_alo;
		cb->s_dport = ipxp->ipxp_fport =  si->si_sport;
		cb->s_timer[SPXT_REXMT] = 0;
		cb->s_flags |= SF_ACKNOW;
		soisconnected(so);
		cb->s_state = TCPS_ESTABLISHED;
		/* Use roundtrip time of connection request for initial rtt */
		if (cb->s_rtt) {
			cb->s_srtt = cb->s_rtt << 3;
			cb->s_rttvar = cb->s_rtt << 1;
			SPXT_RANGESET(cb->s_rxtcur,
			    ((cb->s_srtt >> 2) + cb->s_rttvar) >> 1,
			    SPXTV_MIN, SPXTV_REXMTMAX);
			    cb->s_rtt = 0;
		}
	}
	if (so->so_options & SO_DEBUG || traceallspxs)
		spx_trace(SA_INPUT, (u_char)ostate, cb, &spx_savesi, 0);

	m->m_len -= sizeof(struct ipx);
	m->m_pkthdr.len -= sizeof(struct ipx);
	m->m_data += sizeof(struct ipx);

	if (spx_reass(cb, si, m)) {
		m_freem(m);
	}
	if (cb->s_force || (cb->s_flags & (SF_ACKNOW|SF_WIN|SF_RXT)))
		spx_output(cb, NULL);
	cb->s_flags &= ~(SF_WIN|SF_RXT);
	return;

dropwithreset:
	if (dropsocket)
		soabort(so);
	si->si_seq = ntohs(si->si_seq);
	si->si_ack = ntohs(si->si_ack);
	si->si_alo = ntohs(si->si_alo);
	m_freem(m);
	if (cb->s_ipxpcb->ipxp_socket->so_options & SO_DEBUG || traceallspxs)
		spx_trace(SA_DROP, (u_char)ostate, cb, &spx_savesi, 0);
	return;

drop:
bad:
	if (cb == 0 || cb->s_ipxpcb->ipxp_socket->so_options & SO_DEBUG ||
            traceallspxs)
		spx_trace(SA_DROP, (u_char)ostate, cb, &spx_savesi, 0);
	m_freem(m);
}

static int spxrexmtthresh = 3;

/*
 * This is structurally similar to the tcp reassembly routine
 * but its function is somewhat different:  It merely queues
 * packets up, and suppresses duplicates.
 */
static int
spx_reass(struct spxpcb *cb, struct spx *si, struct mbuf *si_m)
{
	struct spx_q *q, *nq, *q_temp;
	struct mbuf *m;
	struct socket *so = cb->s_ipxpcb->ipxp_socket;
	char packetp = cb->s_flags & SF_HI;
	int incr;
	char wakeup = 0;

	if (si == NULL)
		goto present;
	/*
	 * Update our news from them.
	 */
	if (si->si_cc & SPX_SA)
		cb->s_flags |= (spx_use_delack ? SF_DELACK : SF_ACKNOW);
	if (SSEQ_GT(si->si_alo, cb->s_ralo))
		cb->s_flags |= SF_WIN;
	if (SSEQ_LEQ(si->si_ack, cb->s_rack)) {
		if ((si->si_cc & SPX_SP) && cb->s_rack != (cb->s_smax + 1)) {
			spxstat.spxs_rcvdupack++;
			/*
			 * If this is a completely duplicate ack
			 * and other conditions hold, we assume
			 * a packet has been dropped and retransmit
			 * it exactly as in tcp_input().
			 */
			if (si->si_ack != cb->s_rack ||
			    si->si_alo != cb->s_ralo)
				cb->s_dupacks = 0;
			else if (++cb->s_dupacks == spxrexmtthresh) {
				u_short onxt = cb->s_snxt;
				int cwnd = cb->s_cwnd;

				cb->s_snxt = si->si_ack;
				cb->s_cwnd = CUNIT;
				cb->s_force = 1 + SPXT_REXMT;
				spx_output(cb, NULL);
				cb->s_timer[SPXT_REXMT] = cb->s_rxtcur;
				cb->s_rtt = 0;
				if (cwnd >= 4 * CUNIT)
					cb->s_cwnd = cwnd / 2;
				if (SSEQ_GT(onxt, cb->s_snxt))
					cb->s_snxt = onxt;
				return (1);
			}
		} else
			cb->s_dupacks = 0;
		goto update_window;
	}
	cb->s_dupacks = 0;
	/*
	 * If our correspondent acknowledges data we haven't sent
	 * TCP would drop the packet after acking.  We'll be a little
	 * more permissive
	 */
	if (SSEQ_GT(si->si_ack, (cb->s_smax + 1))) {
		spxstat.spxs_rcvacktoomuch++;
		si->si_ack = cb->s_smax + 1;
	}
	spxstat.spxs_rcvackpack++;
	/*
	 * If transmit timer is running and timed sequence
	 * number was acked, update smoothed round trip time.
	 * See discussion of algorithm in tcp_input.c
	 */
	if (cb->s_rtt && SSEQ_GT(si->si_ack, cb->s_rtseq)) {
		spxstat.spxs_rttupdated++;
		if (cb->s_srtt != 0) {
			short delta;
			delta = cb->s_rtt - (cb->s_srtt >> 3);
			if ((cb->s_srtt += delta) <= 0)
				cb->s_srtt = 1;
			if (delta < 0)
				delta = -delta;
			delta -= (cb->s_rttvar >> 2);
			if ((cb->s_rttvar += delta) <= 0)
				cb->s_rttvar = 1;
		} else {
			/*
			 * No rtt measurement yet
			 */
			cb->s_srtt = cb->s_rtt << 3;
			cb->s_rttvar = cb->s_rtt << 1;
		}
		cb->s_rtt = 0;
		cb->s_rxtshift = 0;
		SPXT_RANGESET(cb->s_rxtcur,
			((cb->s_srtt >> 2) + cb->s_rttvar) >> 1,
			SPXTV_MIN, SPXTV_REXMTMAX);
	}
	/*
	 * If all outstanding data is acked, stop retransmit
	 * timer and remember to restart (more output or persist).
	 * If there is more data to be acked, restart retransmit
	 * timer, using current (possibly backed-off) value;
	 */
	if (si->si_ack == cb->s_smax + 1) {
		cb->s_timer[SPXT_REXMT] = 0;
		cb->s_flags |= SF_RXT;
	} else if (cb->s_timer[SPXT_PERSIST] == 0)
		cb->s_timer[SPXT_REXMT] = cb->s_rxtcur;
	/*
	 * When new data is acked, open the congestion window.
	 * If the window gives us less than ssthresh packets
	 * in flight, open exponentially (maxseg at a time).
	 * Otherwise open linearly (maxseg^2 / cwnd at a time).
	 */
	incr = CUNIT;
	if (cb->s_cwnd > cb->s_ssthresh)
		incr = max(incr * incr / cb->s_cwnd, 1);
	cb->s_cwnd = min(cb->s_cwnd + incr, cb->s_cwmx);
	/*
	 * Trim Acked data from output queue.
	 */
	while ((m = so->so_snd.ssb_mb) != NULL) {
		if (SSEQ_LT((mtod(m, struct spx *))->si_seq, si->si_ack))
			sbdroprecord(&so->so_snd.sb);
		else
			break;
	}
	sowwakeup(so);
	cb->s_rack = si->si_ack;
update_window:
	if (SSEQ_LT(cb->s_snxt, cb->s_rack))
		cb->s_snxt = cb->s_rack;
	if (SSEQ_LT(cb->s_swl1, si->si_seq) || ((cb->s_swl1 == si->si_seq &&
	    (SSEQ_LT(cb->s_swl2, si->si_ack))) ||
	     (cb->s_swl2 == si->si_ack && SSEQ_LT(cb->s_ralo, si->si_alo)))) {
		/* keep track of pure window updates */
		if ((si->si_cc & SPX_SP) && cb->s_swl2 == si->si_ack
		    && SSEQ_LT(cb->s_ralo, si->si_alo)) {
			spxstat.spxs_rcvwinupd++;
			spxstat.spxs_rcvdupack--;
		}
		cb->s_ralo = si->si_alo;
		cb->s_swl1 = si->si_seq;
		cb->s_swl2 = si->si_ack;
		cb->s_swnd = (1 + si->si_alo - si->si_ack);
		if (cb->s_swnd > cb->s_smxw)
			cb->s_smxw = cb->s_swnd;
		cb->s_flags |= SF_WIN;
	}
	/*
	 * If this packet number is higher than that which
	 * we have allocated refuse it, unless urgent
	 */
	if (SSEQ_GT(si->si_seq, cb->s_alo)) {
		if (si->si_cc & SPX_SP) {
			spxstat.spxs_rcvwinprobe++;
			return (1);
		} else
			spxstat.spxs_rcvpackafterwin++;
		if (si->si_cc & SPX_OB) {
			if (SSEQ_GT(si->si_seq, cb->s_alo + 60)) {
				m_freem(si_m);
				return (0);
			} /* else queue this packet; */
		} else {
			/*register struct socket *so = cb->s_ipxpcb->ipxp_socket;
			if (so->so_state && SS_NOFDREF) {
				spx_close(cb);
			} else
				       would crash system*/
			spx_istat.notyet++;
			m_freem(si_m);
			return (0);
		}
	}
	/*
	 * If this is a system packet, we don't need to
	 * queue it up, and won't update acknowledge #
	 */
	if (si->si_cc & SPX_SP) {
		return (1);
	}
	/*
	 * We have already seen this packet, so drop.
	 */
	if (SSEQ_LT(si->si_seq, cb->s_ack)) {
		spx_istat.bdreas++;
		spxstat.spxs_rcvduppack++;
		if (si->si_seq == cb->s_ack - 1)
			spx_istat.lstdup++;
		return (1);
	}
	/*
	 * Loop through all packets queued up to insert in
	 * appropriate sequence.
	 */
	LIST_FOREACH(q, &cb->s_q, sq_entry) {
		if (si->si_seq == SI(q)->si_seq) {
			spxstat.spxs_rcvduppack++;
			return (1);
		}
		if (SSEQ_LT(si->si_seq, SI(q)->si_seq)) {
			spxstat.spxs_rcvoopack++;
			break;
		}
	}
	nq = kmalloc(sizeof(struct spx_q), M_SPX_Q, M_INTNOWAIT);
	if (nq == NULL) {
		m_freem(si_m);
		return (0);
	}
	if (q == NULL)
		LIST_INSERT_HEAD(&cb->s_q, nq, sq_entry);
	else
		LIST_INSERT_BEFORE(q, nq, sq_entry);
	nq->si_mbuf = si_m;
	/*
	 * If this packet is urgent, inform process
	 */
	if (si->si_cc & SPX_OB) {
		cb->s_iobc = ((char *)si)[1 + sizeof(*si)];
		sohasoutofband(so);
		cb->s_oobflags |= SF_IOOB;
	}
present:
#define SPINC sizeof(struct spxhdr)
	/*
	 * Loop through all packets queued up to update acknowledge
	 * number, and present all acknowledged data to user;
	 * If in packet interface mode, show packet headers.
	 */
	LIST_FOREACH_MUTABLE(q, &cb->s_q, sq_entry, q_temp) {
		  if (SI(q)->si_seq == cb->s_ack) {
			cb->s_ack++;
			m = q->si_mbuf;
			if (SI(q)->si_cc & SPX_OB) {
				cb->s_oobflags &= ~SF_IOOB;
				if (so->so_rcv.ssb_cc)
					so->so_oobmark = so->so_rcv.ssb_cc;
				else
					sosetstate(so, SS_RCVATMARK);
			}
			LIST_REMOVE(q, sq_entry);
			kfree(q, M_SPX_Q);
			wakeup = 1;
			spxstat.spxs_rcvpack++;
#ifdef SF_NEWCALL
			if (cb->s_flags2 & SF_NEWCALL) {
				struct spxhdr *sp = mtod(m, struct spxhdr *);
				u_char dt = sp->spx_dt;
				spx_newchecks[4]++;
				if (dt != cb->s_rhdr.spx_dt) {
					struct mbuf *mm =
					   m_getclr(MB_DONTWAIT, MT_CONTROL);
					spx_newchecks[0]++;
					if (mm != NULL) {
						u_short *s =
							mtod(mm, u_short *);
						cb->s_rhdr.spx_dt = dt;
						mm->m_len = 5; /*XXX*/
						s[0] = 5;
						s[1] = 1;
						*(u_char *)(&s[2]) = dt;
						sbappend(&so->so_rcv.sb, mm);
					}
				}
				if (sp->spx_cc & SPX_OB) {
					m_chtype(m, MT_OOBDATA);
					spx_newchecks[1]++;
					so->so_oobmark = 0;
					soclrstate(so, SS_RCVATMARK);
				}
				if (packetp == 0) {
					m->m_data += SPINC;
					m->m_len -= SPINC;
					m->m_pkthdr.len -= SPINC;
				}
				if ((sp->spx_cc & SPX_EM) || packetp) {
					sbappendrecord(&so->so_rcv.sb, m);
					spx_newchecks[9]++;
				} else
					sbappend(&so->so_rcv.sb, m);
			} else
#endif
			if (packetp) {
				sbappendrecord(&so->so_rcv.sb, m);
			} else {
				cb->s_rhdr = *mtod(m, struct spxhdr *);
				m->m_data += SPINC;
				m->m_len -= SPINC;
				m->m_pkthdr.len -= SPINC;
				sbappend(&so->so_rcv.sb, m);
			}
		  } else
			break;
	}
	if (wakeup)
		sorwakeup(so);
	return (0);
}

void
spx_ctlinput(netmsg_t msg)
{
	/*struct socket *so = msg->base.nm_so;*/
	int cmd = msg->ctlinput.nm_cmd;
	struct sockaddr *arg_as_sa = msg->ctlinput.nm_arg;
	caddr_t arg = (/* XXX */ caddr_t)arg_as_sa;
	struct ipx_addr *na;
	struct sockaddr_ipx *sipx;

	if (cmd < 0 || cmd > PRC_NCMDS)
		goto out;

	switch (cmd) {
	case PRC_ROUTEDEAD:
		break;
	case PRC_IFDOWN:
	case PRC_HOSTDEAD:
	case PRC_HOSTUNREACH:
		sipx = (struct sockaddr_ipx *)arg;
		if (sipx->sipx_family != AF_IPX)
			break;
		na = &sipx->sipx_addr;
		break;
	default:
		break;
	}
out:
	lwkt_replymsg(&msg->lmsg, 0);
}

static int
spx_output(struct spxpcb *cb, struct mbuf *m0)
{
	struct socket *so = cb->s_ipxpcb->ipxp_socket;
	struct mbuf *m = NULL;
	struct spx *si = NULL;
	struct signalsockbuf *ssb = &so->so_snd;
	int len = 0, win, rcv_win;
	short span, off, recordp = 0;
	u_short alo;
	int error = 0, sendalot;
#ifdef notdef
	int idle;
#endif
	struct mbuf *mprev;

	if (m0 != NULL) {
		int mtu = cb->s_mtu;
		int datalen;
		/*
		 * Make sure that packet isn't too big.
		 */
		for (m = m0; m != NULL; m = m->m_next) {
			mprev = m;
			len += m->m_len;
			if (m->m_flags & M_EOR)
				recordp = 1;
		}
		datalen = (cb->s_flags & SF_HO) ?
				len - sizeof(struct spxhdr) : len;
		if (datalen > mtu) {
			if (cb->s_flags & SF_PI) {
				m_freem(m0);
				return (EMSGSIZE);
			} else {
				int oldEM = cb->s_cc & SPX_EM;

				cb->s_cc &= ~SPX_EM;
				while (len > mtu) {
					/*
					 * Here we are only being called
					 * from usrreq(), so it is OK to
					 * block.
					 */
					m = m_copym(m0, 0, mtu, MB_WAIT);
					if (cb->s_flags & SF_NEWCALL) {
					    struct mbuf *mm = m;
					    spx_newchecks[7]++;
					    while (mm != NULL) {
						mm->m_flags &= ~M_EOR;
						mm = mm->m_next;
					    }
					}
					error = spx_output(cb, m);
					if (error) {
						cb->s_cc |= oldEM;
						m_freem(m0);
						return (error);
					}
					m_adj(m0, mtu);
					len -= mtu;
				}
				cb->s_cc |= oldEM;
			}
		}
		/*
		 * Force length even, by adding a "garbage byte" if
		 * necessary.
		 */
		if (len & 1) {
			m = mprev;
			if (M_TRAILINGSPACE(m) >= 1)
				m->m_len++;
			else {
				struct mbuf *m1 = m_get(MB_DONTWAIT, MT_DATA);

				if (m1 == NULL) {
					m_freem(m0);
					return (ENOBUFS);
				}
				m1->m_len = 1;
				*(mtod(m1, u_char *)) = 0;
				m->m_next = m1;
			}
		}
		m = m_gethdr(MB_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			m_freem(m0);
			return (ENOBUFS);
		}
		/*
		 * Fill in mbuf with extended SP header
		 * and addresses and length put into network format.
		 */
		MH_ALIGN(m, sizeof(struct spx));
		m->m_len = sizeof(struct spx);
		m->m_next = m0;
		si = mtod(m, struct spx *);
		si->si_i = *cb->s_ipx;
		si->si_s = cb->s_shdr;
		if ((cb->s_flags & SF_PI) && (cb->s_flags & SF_HO)) {
			struct spxhdr *sh;
			if (m0->m_len < sizeof(*sh)) {
				if((m0 = m_pullup(m0, sizeof(*sh))) == NULL) {
					m_free(m);
					m_freem(m0);
					return (EINVAL);
				}
				m->m_next = m0;
			}
			sh = mtod(m0, struct spxhdr *);
			si->si_dt = sh->spx_dt;
			si->si_cc |= sh->spx_cc & SPX_EM;
			m0->m_len -= sizeof(*sh);
			m0->m_data += sizeof(*sh);
			len -= sizeof(*sh);
		}
		len += sizeof(*si);
		if ((cb->s_flags2 & SF_NEWCALL) && recordp) {
			si->si_cc |= SPX_EM;
			spx_newchecks[8]++;
		}
		if (cb->s_oobflags & SF_SOOB) {
			/*
			 * Per jqj@cornell:
			 * make sure OB packets convey exactly 1 byte.
			 * If the packet is 1 byte or larger, we
			 * have already guaranted there to be at least
			 * one garbage byte for the checksum, and
			 * extra bytes shouldn't hurt!
			 */
			if (len > sizeof(*si)) {
				si->si_cc |= SPX_OB;
				len = (1 + sizeof(*si));
			}
		}
		si->si_len = htons((u_short)len);
		m->m_pkthdr.len = ((len - 1) | 1) + 1;
		/*
		 * queue stuff up for output
		 */
		sbappendrecord(&ssb->sb, m);
		cb->s_seq++;
	}
#ifdef notdef
	idle = (cb->s_smax == (cb->s_rack - 1));
#endif
again:
	sendalot = 0;
	off = cb->s_snxt - cb->s_rack;
	win = min(cb->s_swnd, (cb->s_cwnd / CUNIT));

	/*
	 * If in persist timeout with window of 0, send a probe.
	 * Otherwise, if window is small but nonzero
	 * and timer expired, send what we can and go into
	 * transmit state.
	 */
	if (cb->s_force == 1 + SPXT_PERSIST) {
		if (win != 0) {
			cb->s_timer[SPXT_PERSIST] = 0;
			cb->s_rxtshift = 0;
		}
	}
	span = cb->s_seq - cb->s_rack;
	len = min(span, win) - off;

	if (len < 0) {
		/*
		 * Window shrank after we went into it.
		 * If window shrank to 0, cancel pending
		 * restransmission and pull s_snxt back
		 * to (closed) window.  We will enter persist
		 * state below.  If the widndow didn't close completely,
		 * just wait for an ACK.
		 */
		len = 0;
		if (win == 0) {
			cb->s_timer[SPXT_REXMT] = 0;
			cb->s_snxt = cb->s_rack;
		}
	}
	if (len > 1)
		sendalot = 1;
	rcv_win = ssb_space(&so->so_rcv);

	/*
	 * Send if we owe peer an ACK.
	 */
	if (cb->s_oobflags & SF_SOOB) {
		/*
		 * must transmit this out of band packet
		 */
		cb->s_oobflags &= ~ SF_SOOB;
		sendalot = 1;
		spxstat.spxs_sndurg++;
		goto found;
	}
	if (cb->s_flags & SF_ACKNOW)
		goto send;
	if (cb->s_state < TCPS_ESTABLISHED)
		goto send;
	/*
	 * Silly window can't happen in spx.
	 * Code from tcp deleted.
	 */
	if (len)
		goto send;
	/*
	 * Compare available window to amount of window
	 * known to peer (as advertised window less
	 * next expected input.)  If the difference is at least two
	 * packets or at least 35% of the mximum possible window,
	 * then want to send a window update to peer.
	 */
	if (rcv_win > 0) {
		u_short delta =  1 + cb->s_alo - cb->s_ack;
		int adv = rcv_win - (delta * cb->s_mtu);
		
		if ((so->so_rcv.ssb_cc == 0 && adv >= (2 * cb->s_mtu)) ||
		    (100 * adv / so->so_rcv.ssb_hiwat >= 35)) {
			spxstat.spxs_sndwinup++;
			cb->s_flags |= SF_ACKNOW;
			goto send;
		}

	}
	/*
	 * Many comments from tcp_output.c are appropriate here
	 * including . . .
	 * If send window is too small, there is data to transmit, and no
	 * retransmit or persist is pending, then go to persist state.
	 * If nothing happens soon, send when timer expires:
	 * if window is nonzero, transmit what we can,
	 * otherwise send a probe.
	 */
	if (so->so_snd.ssb_cc && cb->s_timer[SPXT_REXMT] == 0 &&
		cb->s_timer[SPXT_PERSIST] == 0) {
			cb->s_rxtshift = 0;
			spx_setpersist(cb);
	}
	/*
	 * No reason to send a packet, just return.
	 */
	cb->s_outx = 1;
	return (0);

send:
	/*
	 * Find requested packet.
	 */
	si = NULL;
	if (len > 0) {
		cb->s_want = cb->s_snxt;
		for (m = ssb->ssb_mb; m != NULL; m = m->m_nextpkt) {
			si = mtod(m, struct spx *);
			if (SSEQ_LEQ(cb->s_snxt, si->si_seq))
				break;
		}
	found:
		if (si != NULL) {
			if (si->si_seq == cb->s_snxt)
					cb->s_snxt++;
				else
					spxstat.spxs_sndvoid++, si = 0;
		}
	}
	/*
	 * update window
	 */
	if (rcv_win < 0)
		rcv_win = 0;
	alo = cb->s_ack - 1 + (rcv_win / ((short)cb->s_mtu));
	if (SSEQ_LT(alo, cb->s_alo)) 
		alo = cb->s_alo;

	if (si != NULL) {
		/*
		 * must make a copy of this packet for
		 * ipx_output to monkey with
		 */
		m = m_copy(m, 0, (int)M_COPYALL);
		if (m == NULL) {
			return (ENOBUFS);
		}
		si = mtod(m, struct spx *);
		if (SSEQ_LT(si->si_seq, cb->s_smax))
			spxstat.spxs_sndrexmitpack++;
		else
			spxstat.spxs_sndpack++;
	} else if (cb->s_force || cb->s_flags & SF_ACKNOW) {
		/*
		 * Must send an acknowledgement or a probe
		 */
		if (cb->s_force)
			spxstat.spxs_sndprobe++;
		if (cb->s_flags & SF_ACKNOW)
			spxstat.spxs_sndacks++;
		m = m_gethdr(MB_DONTWAIT, MT_HEADER);
		if (m == NULL)
			return (ENOBUFS);
		/*
		 * Fill in mbuf with extended SP header
		 * and addresses and length put into network format.
		 */
		MH_ALIGN(m, sizeof(struct spx));
		m->m_len = sizeof(*si);
		m->m_pkthdr.len = sizeof(*si);
		si = mtod(m, struct spx *);
		si->si_i = *cb->s_ipx;
		si->si_s = cb->s_shdr;
		si->si_seq = cb->s_smax + 1;
		si->si_len = htons(sizeof(*si));
		si->si_cc |= SPX_SP;
	} else {
		cb->s_outx = 3;
		if (so->so_options & SO_DEBUG || traceallspxs)
			spx_trace(SA_OUTPUT, cb->s_state, cb, si, 0);
		return (0);
	}
	/*
	 * Stuff checksum and output datagram.
	 */
	if ((si->si_cc & SPX_SP) == 0) {
		if (cb->s_force != (1 + SPXT_PERSIST) ||
		    cb->s_timer[SPXT_PERSIST] == 0) {
			/*
			 * If this is a new packet and we are not currently 
			 * timing anything, time this one.
			 */
			if (SSEQ_LT(cb->s_smax, si->si_seq)) {
				cb->s_smax = si->si_seq;
				if (cb->s_rtt == 0) {
					spxstat.spxs_segstimed++;
					cb->s_rtseq = si->si_seq;
					cb->s_rtt = 1;
				}
			}
			/*
			 * Set rexmt timer if not currently set,
			 * Initial value for retransmit timer is smoothed
			 * round-trip time + 2 * round-trip time variance.
			 * Initialize shift counter which is used for backoff
			 * of retransmit time.
			 */
			if (cb->s_timer[SPXT_REXMT] == 0 &&
			    cb->s_snxt != cb->s_rack) {
				cb->s_timer[SPXT_REXMT] = cb->s_rxtcur;
				if (cb->s_timer[SPXT_PERSIST]) {
					cb->s_timer[SPXT_PERSIST] = 0;
					cb->s_rxtshift = 0;
				}
			}
		} else if (SSEQ_LT(cb->s_smax, si->si_seq)) {
			cb->s_smax = si->si_seq;
		}
	} else if (cb->s_state < TCPS_ESTABLISHED) {
		if (cb->s_rtt == 0)
			cb->s_rtt = 1; /* Time initial handshake */
		if (cb->s_timer[SPXT_REXMT] == 0)
			cb->s_timer[SPXT_REXMT] = cb->s_rxtcur;
	}
	{
		/*
		 * Do not request acks when we ack their data packets or
		 * when we do a gratuitous window update.
		 */
		if (((si->si_cc & SPX_SP) == 0) || cb->s_force)
				si->si_cc |= SPX_SA;
		si->si_seq = htons(si->si_seq);
		si->si_alo = htons(alo);
		si->si_ack = htons(cb->s_ack);

		if (ipxcksum) {
			si->si_sum = ipx_cksum(m, ntohs(si->si_len));
		} else
			si->si_sum = 0xffff;

		cb->s_outx = 4;
		if (so->so_options & SO_DEBUG || traceallspxs)
			spx_trace(SA_OUTPUT, cb->s_state, cb, si, 0);

		if (so->so_options & SO_DONTROUTE)
			error = ipx_outputfl(m, NULL, IPX_ROUTETOIF);
		else
			error = ipx_outputfl(m, &cb->s_ipxpcb->ipxp_route, 0);
	}
	if (error) {
		return (error);
	}
	spxstat.spxs_sndtotal++;
	/*
	 * Data sent (as far as we can tell).
	 * If this advertises a larger window than any other segment,
	 * then remember the size of the advertized window.
	 * Any pending ACK has now been sent.
	 */
	cb->s_force = 0;
	cb->s_flags &= ~(SF_ACKNOW|SF_DELACK);
	if (SSEQ_GT(alo, cb->s_alo))
		cb->s_alo = alo;
	if (sendalot)
		goto again;
	cb->s_outx = 5;
	return (0);
}

static int spx_do_persist_panics = 0;

static void
spx_setpersist(struct spxpcb *cb)
{
	int t = ((cb->s_srtt >> 2) + cb->s_rttvar) >> 1;

	if (cb->s_timer[SPXT_REXMT] && spx_do_persist_panics)
		panic("spx_output REXMT");
	/*
	 * Start/restart persistance timer.
	 */
	SPXT_RANGESET(cb->s_timer[SPXT_PERSIST],
	    t*spx_backoff[cb->s_rxtshift],
	    SPXTV_PERSMIN, SPXTV_PERSMAX);
	if (cb->s_rxtshift < SPX_MAXRXTSHIFT)
		cb->s_rxtshift++;
}

void
spx_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp = sotoipxpcb(so);
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct spxpcb *cb;
	int mask, error;
	short soptval;
	u_short usoptval;
	int optval;

	error = 0;

	if (sopt->sopt_level != IPXPROTO_SPX) {
		/* This will have to be changed when we do more general
		   stacking of protocols */
		ipx_ctloutput(msg);
		/* msg now invalid */
		return;
	}
	if (ipxp == NULL) {
		error = EINVAL;
		goto out;
	}
	cb = ipxtospxpcb(ipxp);

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case SO_HEADERS_ON_INPUT:
			mask = SF_HI;
			goto get_flags;

		case SO_HEADERS_ON_OUTPUT:
			mask = SF_HO;
		get_flags:
			soptval = cb->s_flags & mask;
			error = sooptcopyout(sopt, &soptval, sizeof soptval);
			break;

		case SO_MTU:
			usoptval = cb->s_mtu;
			error = sooptcopyout(sopt, &usoptval, sizeof usoptval);
			break;

		case SO_LAST_HEADER:
			error = sooptcopyout(sopt, &cb->s_rhdr, 
					     sizeof cb->s_rhdr);
			break;

		case SO_DEFAULT_HEADERS:
			error = sooptcopyout(sopt, &cb->s_shdr, 
					     sizeof cb->s_shdr);
			break;

		default:
			error = ENOPROTOOPT;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
			/* XXX why are these shorts on get and ints on set?
			   that doesn't make any sense... */
		case SO_HEADERS_ON_INPUT:
			mask = SF_HI;
			goto set_head;

		case SO_HEADERS_ON_OUTPUT:
			mask = SF_HO;
		set_head:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;

			if (cb->s_flags & SF_PI) {
				if (optval)
					cb->s_flags |= mask;
				else
					cb->s_flags &= ~mask;
			} else error = EINVAL;
			break;

		case SO_MTU:
			error = sooptcopyin(sopt, &usoptval, sizeof usoptval,
					    sizeof usoptval);
			if (error)
				break;
			cb->s_mtu = usoptval;
			break;

#ifdef SF_NEWCALL
		case SO_NEWCALL:
			error = sooptcopyin(sopt, &optval, sizeof optval,
					    sizeof optval);
			if (error)
				break;
			if (optval) {
				cb->s_flags2 |= SF_NEWCALL;
				spx_newchecks[5]++;
			} else {
				cb->s_flags2 &= ~SF_NEWCALL;
				spx_newchecks[6]++;
			}
			break;
#endif

		case SO_DEFAULT_HEADERS:
			{
				struct spxhdr sp;

				error = sooptcopyin(sopt, &sp, sizeof sp,
						    sizeof sp);
				if (error)
					break;
				cb->s_dt = sp.spx_dt;
				cb->s_cc = sp.spx_cc & SPX_EM;
			}
			break;

		default:
			error = ENOPROTOOPT;
		}
		break;
	}
out:
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
spx_usr_abort(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	spx_drop(cb, ECONNABORTED);

	lwkt_replymsg(&msg->lmsg, 0);
}

/*
 * Accept a connection.  Essentially all the work is
 * done at higher levels; just return the address
 * of the peer, storing through addr.
 */
static void
spx_accept(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct sockaddr **nam = msg->accept.nm_nam;
	struct ipxpcb *ipxp;
	struct sockaddr_ipx *sipx, ssipx;

	ipxp = sotoipxpcb(so);
	sipx = &ssipx;
	bzero(sipx, sizeof *sipx);
	sipx->sipx_len = sizeof *sipx;
	sipx->sipx_family = AF_IPX;
	sipx->sipx_addr = ipxp->ipxp_faddr;
	*nam = dup_sockaddr((struct sockaddr *)sipx);

	lwkt_replymsg(&msg->lmsg, 0);
}

static int
spx_attach_oncpu(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	struct mbuf *mm;
	struct signalsockbuf *ssb;
	int error;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	if (ipxp != NULL) {
		error = EISCONN;
		goto spx_attach_end;
	}
	error = ipx_pcballoc(so, &ipxpcb_list);
	if (error)
		goto spx_attach_end;
	if (so->so_snd.ssb_hiwat == 0 || so->so_rcv.ssb_hiwat == 0) {
		error = soreserve(so, (u_long) 3072, (u_long) 3072,
				  ai->sb_rlimit);
		if (error)
			goto spx_attach_end;
	}
	ipxp = sotoipxpcb(so);

	cb = kmalloc(sizeof *cb, M_PCB, M_INTWAIT | M_ZERO);
	ssb = &so->so_snd;

	mm = m_getclr(MB_DONTWAIT, MT_HEADER);
	if (mm == NULL) {
		kfree(cb, M_PCB);
		error = ENOBUFS;
		goto spx_attach_end;
	}
	cb->s_ipx_m = mm;
	cb->s_ipx = mtod(mm, struct ipx *);
	cb->s_state = TCPS_LISTEN;
	cb->s_smax = -1;
	cb->s_swl1 = -1;
	LIST_INIT(&cb->s_q);
	cb->s_ipxpcb = ipxp;
	cb->s_mtu = 576 - sizeof(struct spx);
	cb->s_cwnd = ssb_space(ssb) * CUNIT / cb->s_mtu;
	cb->s_ssthresh = cb->s_cwnd;
	cb->s_cwmx = ssb_space(ssb) * CUNIT / (2 * sizeof(struct spx));
	/* Above is recomputed when connecting to account
	   for changed buffering or mtu's */
	cb->s_rtt = SPXTV_SRTTBASE;
	cb->s_rttvar = SPXTV_SRTTDFLT << 2;
	SPXT_RANGESET(cb->s_rxtcur,
	    ((SPXTV_SRTTBASE >> 2) + (SPXTV_SRTTDFLT << 2)) >> 1,
	    SPXTV_MIN, SPXTV_REXMTMAX);
	ipxp->ipxp_pcb = (caddr_t)cb; 
spx_attach_end:
	crit_exit();
	return error;
}

static void
spx_attach(netmsg_t msg)
{
	int error;

	error = spx_attach_oncpu(msg->base.nm_so,
				 msg->attach.nm_proto,
				 msg->attach.nm_ai);
	lwkt_replymsg(&msg->lmsg, error);
}


static void
spx_bind(netmsg_t msg)
{  
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	int error;

	ipxp = sotoipxpcb(so);

	error = ipx_pcbbind(ipxp, msg->bind.nm_nam, msg->bind.nm_td);
	lwkt_replymsg(&msg->lmsg, error);
}  
   
/*
 * Initiate connection to peer.
 * Enter SYN_SENT state, and mark socket as connecting.
 * Start keep-alive timer, setup prototype header,
 * Send initial system packet requesting connection.
 */
static void
spx_connect(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct thread *td = msg->connect.nm_td;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	if (ipxp->ipxp_lport == 0) {
		error = ipx_pcbbind(ipxp, NULL, td);
		if (error)
			goto spx_connect_end;
	}
	error = ipx_pcbconnect(ipxp, nam, td);
	if (error)
		goto spx_connect_end;
	soisconnecting(so);
	spxstat.spxs_connattempt++;
	cb->s_state = TCPS_SYN_SENT;
	cb->s_did = 0;
	spx_template(cb);
	cb->s_timer[SPXT_KEEP] = SPXTV_KEEP;
	cb->s_force = 1 + SPXTV_KEEP;
	/*
	 * Other party is required to respond to
	 * the port I send from, but he is not
	 * required to answer from where I am sending to,
	 * so allow wildcarding.
	 * original port I am sending to is still saved in
	 * cb->s_dport.
	 */
	ipxp->ipxp_fport = 0;
	error = spx_output(cb, NULL);
spx_connect_end:
	crit_exit();
	lwkt_replymsg(&msg->lmsg, error);
}

static void
spx_detach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	if (ipxp) {
		crit_enter();
		if (cb->s_state > TCPS_LISTEN)
			spx_disconnect(cb);
		else
			spx_close(cb);
		crit_exit();
		error = 0;
	} else {
		error = ENOTCONN;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * We may decide later to implement connection closing
 * handshaking at the spx level optionally.
 * here is the hook to do it:
 */
static void
spx_usr_disconnect(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	spx_disconnect(cb);
	crit_exit();

	lwkt_replymsg(&msg->lmsg, 0);
}

static void
spx_listen(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	error = 0;
	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	if (ipxp->ipxp_lport == 0)
		error = ipx_pcbbind(ipxp, NULL, msg->listen.nm_td);
	if (error == 0)
		cb->s_state = TCPS_LISTEN;
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * After a receive, possibly send acknowledgment
 * updating allocation.
 */
static void
spx_rcvd(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	cb->s_flags |= SF_RVD;
	spx_output(cb, NULL);
	cb->s_flags &= ~SF_RVD;
	crit_exit();

	lwkt_replymsg(&msg->lmsg, 0);
}

static void
spx_rcvoob(netmsg_t msg)
{
	struct mbuf *m = msg->rcvoob.nm_m;
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	if ((cb->s_oobflags & SF_IOOB) || so->so_oobmark ||
	    (so->so_state & SS_RCVATMARK)) {
		m->m_len = 1;
		*mtod(m, caddr_t) = cb->s_iobc;
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->lmsg, error);
}

static void
spx_send(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct mbuf *controlp = msg->send.nm_control;
	int flags = msg->send.nm_flags;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	error = 0;
	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	if (flags & PRUS_OOB) {
		if (ssb_space(&so->so_snd) < -512) {
			error = ENOBUFS;
			goto spx_send_end;
		}
		cb->s_oobflags |= SF_SOOB;
	}
	if (controlp != NULL) {
		u_short *p = mtod(controlp, u_short *);
		spx_newchecks[2]++;
		if ((p[0] == 5) && (p[1] == 1)) { /* XXXX, for testing */
			cb->s_shdr.spx_dt = *(u_char *)(&p[2]);
			spx_newchecks[3]++;
		}
		m_freem(controlp);
	}
	controlp = NULL;
	error = spx_output(cb, m);
	m = NULL;
spx_send_end:
	if (controlp != NULL)
		m_freem(controlp);
	if (m != NULL)
		m_freem(m);
	crit_exit();
	lwkt_replymsg(&msg->lmsg, error);
}

static void
spx_shutdown(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	struct spxpcb *cb;
	int error;

	error = 0;
	ipxp = sotoipxpcb(so);
	cb = ipxtospxpcb(ipxp);

	crit_enter();
	socantsendmore(so);
	cb = spx_usrclosed(cb);
	if (cb != NULL)
		error = spx_output(cb, NULL);
	crit_exit();
	lwkt_replymsg(&msg->lmsg, error);
}

static void
spx_sp_attach(netmsg_t msg)
{
	struct socket *so = msg->base.nm_so;
	struct ipxpcb *ipxp;
	int error;

	error = spx_attach_oncpu(so, msg->attach.nm_proto, msg->attach.nm_ai);
	if (error == 0) {
		ipxp = sotoipxpcb(so);
		((struct spxpcb *)ipxp->ipxp_pcb)->s_flags |=
					(SF_HI | SF_HO | SF_PI);
	}
	lwkt_replymsg(&msg->lmsg, error);
}

/*
 * Create template to be used to send spx packets on a connection.
 * Called after host entry created, fills
 * in a skeletal spx header (choosing connection id),
 * minimizing the amount of work necessary when the connection is used.
 */
static void
spx_template(struct spxpcb *cb)
{
	struct ipxpcb *ipxp = cb->s_ipxpcb;
	struct ipx *ipx = cb->s_ipx;
	struct signalsockbuf *ssb = &(ipxp->ipxp_socket->so_snd);

	ipx->ipx_pt = IPXPROTO_SPX;
	ipx->ipx_sna = ipxp->ipxp_laddr;
	ipx->ipx_dna = ipxp->ipxp_faddr;
	cb->s_sid = htons(spx_iss);
	spx_iss += SPX_ISSINCR/2;
	cb->s_alo = 1;
	cb->s_cwnd = (ssb_space(ssb) * CUNIT) / cb->s_mtu;
	cb->s_ssthresh = cb->s_cwnd; /* Try to expand fast to full complement
					of large packets */
	cb->s_cwmx = (ssb_space(ssb) * CUNIT) / (2 * sizeof(struct spx));
	cb->s_cwmx = max(cb->s_cwmx, cb->s_cwnd);
		/* But allow for lots of little packets as well */
}

/*
 * Close a SPIP control block:
 *	discard spx control block itself
 *	discard ipx protocol control block
 *	wake up any sleepers
 */
static struct spxpcb *
spx_close(struct spxpcb *cb)
{
	struct spx_q *q;
	struct ipxpcb *ipxp = cb->s_ipxpcb;
	struct socket *so = ipxp->ipxp_socket;

	while (!LIST_EMPTY(&cb->s_q)) {
		q = LIST_FIRST(&cb->s_q);
		LIST_REMOVE(q, sq_entry);
		m_freem(q->si_mbuf);
		kfree(q, M_SPX_Q);
	}
	m_free(cb->s_ipx_m);
	kfree(cb, M_PCB);
	ipxp->ipxp_pcb = 0;
	soisdisconnected(so);
	ipx_pcbdetach(ipxp);
	spxstat.spxs_closed++;
	return (NULL);
}

/*
 *	Someday we may do level 3 handshaking
 *	to close a connection or send a xerox style error.
 *	For now, just close.
 */
static struct spxpcb *
spx_usrclosed(struct spxpcb *cb)
{
	return (spx_close(cb));
}

static struct spxpcb *
spx_disconnect(struct spxpcb *cb)
{
	return (spx_close(cb));
}

/*
 * Drop connection, reporting
 * the specified error.
 */
static struct spxpcb *
spx_drop(struct spxpcb *cb, int errno)
{
	struct socket *so = cb->s_ipxpcb->ipxp_socket;

	/*
	 * someday, in the xerox world
	 * we will generate error protocol packets
	 * announcing that the socket has gone away.
	 */
	if (TCPS_HAVERCVDSYN(cb->s_state)) {
		spxstat.spxs_drops++;
		cb->s_state = TCPS_CLOSED;
		/*tcp_output(cb);*/
	} else
		spxstat.spxs_conndrops++;
	so->so_error = errno;
	return (spx_close(cb));
}

/*
 * Fast timeout routine for processing delayed acks
 */
void
spx_fasttimo(void)
{
	struct ipxpcb *ipxp;
	struct spxpcb *cb;

	crit_enter();
	LIST_FOREACH(ipxp, &ipxpcb_list, ipxp_list) {
		if ((cb = (struct spxpcb *)ipxp->ipxp_pcb) != NULL &&
		    (cb->s_flags & SF_DELACK)) {
			cb->s_flags &= ~SF_DELACK;
			cb->s_flags |= SF_ACKNOW;
			spxstat.spxs_delack++;
			spx_output(cb, NULL);
		}
	}
	crit_exit();
}

/*
 * spx protocol timeout routine called every 500 ms.
 * Updates the timers in all active pcb's and
 * causes finite state machine actions if timers expire.
 */
void
spx_slowtimo(void)
{
	struct ipxpcb *ip, *ip_temp;
	struct spxpcb *cb;
	int i;

	/*
	 * Search through tcb's and update active timers.
	 */
	crit_enter();
	LIST_FOREACH_MUTABLE(ip, &ipxpcb_list, ipxp_list, ip_temp) {
		cb = ipxtospxpcb(ip);
		if (cb == NULL)
			continue;
		for (i = 0; i < SPXT_NTIMERS; i++) {
			if (cb->s_timer[i] && --cb->s_timer[i] == 0) {
				if (spx_timers(cb, i) == NULL)
					continue;
			}
		}
		cb->s_idle++;
		if (cb->s_rtt)
			cb->s_rtt++;
	}
	spx_iss += SPX_ISSINCR/PR_SLOWHZ;		/* increment iss */
	crit_exit();
}

/*
 * SPX timer processing.
 */
static struct spxpcb *
spx_timers(struct spxpcb *cb, int timer)
{
	long rexmt;
	int win;

	cb->s_force = 1 + timer;
	switch (timer) {

	/*
	 * 2 MSL timeout in shutdown went off.  TCP deletes connection
	 * control block.
	 */
	case SPXT_2MSL:
		kprintf("spx: SPXT_2MSL went off for no reason\n");
		cb->s_timer[timer] = 0;
		break;

	/*
	 * Retransmission timer went off.  Message has not
	 * been acked within retransmit interval.  Back off
	 * to a longer retransmit interval and retransmit one packet.
	 */
	case SPXT_REXMT:
		if (++cb->s_rxtshift > SPX_MAXRXTSHIFT) {
			cb->s_rxtshift = SPX_MAXRXTSHIFT;
			spxstat.spxs_timeoutdrop++;
			cb = spx_drop(cb, ETIMEDOUT);
			break;
		}
		spxstat.spxs_rexmttimeo++;
		rexmt = ((cb->s_srtt >> 2) + cb->s_rttvar) >> 1;
		rexmt *= spx_backoff[cb->s_rxtshift];
		SPXT_RANGESET(cb->s_rxtcur, rexmt, SPXTV_MIN, SPXTV_REXMTMAX);
		cb->s_timer[SPXT_REXMT] = cb->s_rxtcur;
		/*
		 * If we have backed off fairly far, our srtt
		 * estimate is probably bogus.  Clobber it
		 * so we'll take the next rtt measurement as our srtt;
		 * move the current srtt into rttvar to keep the current
		 * retransmit times until then.
		 */
		if (cb->s_rxtshift > SPX_MAXRXTSHIFT / 4 ) {
			cb->s_rttvar += (cb->s_srtt >> 2);
			cb->s_srtt = 0;
		}
		cb->s_snxt = cb->s_rack;
		/*
		 * If timing a packet, stop the timer.
		 */
		cb->s_rtt = 0;
		/*
		 * See very long discussion in tcp_timer.c about congestion
		 * window and sstrhesh
		 */
		win = min(cb->s_swnd, (cb->s_cwnd/CUNIT)) / 2;
		if (win < 2)
			win = 2;
		cb->s_cwnd = CUNIT;
		cb->s_ssthresh = win * CUNIT;
		spx_output(cb, NULL);
		break;

	/*
	 * Persistance timer into zero window.
	 * Force a probe to be sent.
	 */
	case SPXT_PERSIST:
		spxstat.spxs_persisttimeo++;
		spx_setpersist(cb);
		spx_output(cb, NULL);
		break;

	/*
	 * Keep-alive timer went off; send something
	 * or drop connection if idle for too long.
	 */
	case SPXT_KEEP:
		spxstat.spxs_keeptimeo++;
		if (cb->s_state < TCPS_ESTABLISHED)
			goto dropit;
		if (cb->s_ipxpcb->ipxp_socket->so_options & SO_KEEPALIVE) {
		    	if (cb->s_idle >= SPXTV_MAXIDLE)
				goto dropit;
			spxstat.spxs_keepprobe++;
			spx_output(cb, NULL);
		} else
			cb->s_idle = 0;
		cb->s_timer[SPXT_KEEP] = SPXTV_KEEP;
		break;
	dropit:
		spxstat.spxs_keepdrops++;
		cb = spx_drop(cb, ETIMEDOUT);
		break;
	}
	return (cb);
}
