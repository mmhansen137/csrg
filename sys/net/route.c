/*	route.c	6.2	83/10/20	*/

#include "../h/param.h"
#include "../h/systm.h"
#include "../h/mbuf.h"
#include "../h/protosw.h"
#include "../h/socket.h"
#include "../h/dir.h"
#include "../h/user.h"
#include "../h/ioctl.h"
#include "../h/errno.h"

#include "../net/if.h"
#include "../net/af.h"
#include "../net/route.h"

int	rttrash;		/* routes not in table but not freed */
struct	sockaddr wildcard;	/* zero valued cookie for wildcard searches */

/*
 * Packet routing routines.
 */
rtalloc(ro)
	register struct route *ro;
{
	register struct rtentry *rt;
	register struct mbuf *m;
	register u_long hash;
	struct sockaddr *dst = &ro->ro_dst;
	int (*match)(), doinghost;
	struct afhash h;
	u_int af = dst->sa_family;
	struct rtentry *rtmin;
	struct mbuf **table;

	if (ro->ro_rt && ro->ro_rt->rt_ifp)			/* XXX */
		return;
	if (af >= AF_MAX)
		return;
	(*afswitch[af].af_hash)(dst, &h);
	rtmin = 0;
	match = afswitch[af].af_netmatch;
	hash = h.afh_hosthash, table = rthost, doinghost = 1;
again:
	for (m = table[hash % RTHASHSIZ]; m; m = m->m_next) {
		rt = mtod(m, struct rtentry *);
		if (rt->rt_hash != hash)
			continue;
		if ((rt->rt_flags & RTF_UP) == 0 ||
		    (rt->rt_ifp->if_flags & IFF_UP) == 0)
			continue;
		if (doinghost) {
			if (bcmp((caddr_t)&rt->rt_dst, (caddr_t)dst,
			    sizeof (*dst)))
				continue;
		} else {
			if (rt->rt_dst.sa_family != af ||
			    !(*match)(&rt->rt_dst, dst))
				continue;
		}
		if (rtmin == 0 || rt->rt_use < rtmin->rt_use)
			rtmin = rt;
	}
	if (rtmin == 0 && doinghost) {
		doinghost = 0;
		hash = h.afh_nethash, table = rtnet;
		goto again;
	}
	/*
	 * Check for wildcard gateway, by convention network 0.
	 */
	if (rtmin == 0 && dst != &wildcard) {
		dst = &wildcard, hash = 0;
		goto again;
	}
	ro->ro_rt = rtmin;
	if (rtmin == 0) {
		rtstat.rts_unreach++;
		return;
	}
	rtmin->rt_refcnt++;
	if (dst == &wildcard)
		rtstat.rts_wildcard++;
}

rtfree(rt)
	register struct rtentry *rt;
{

	if (rt == 0)
		panic("rtfree");
	rt->rt_refcnt--;
	if (rt->rt_refcnt == 0 && (rt->rt_flags&RTF_UP) == 0) {
		rttrash--;
		(void) m_free(dtom(rt));
	}
}

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 *
 * N.B.: must be called at splnet or higher
 *
 * Should notify all parties with a reference to
 * the route that it's changed (so, for instance,
 * current round trip time estimates could be flushed),
 * but we have no back pointers at the moment.
 */
rtredirect(dst, gateway)
	struct sockaddr *dst, *gateway;
{
	struct route ro;
	register struct rtentry *rt;

	/* verify the gateway is directly reachable */
	if (if_ifwithnet(gateway) == 0) {
		rtstat.rts_badredirect++;
		return;
	}
	ro.ro_dst = *dst;
	ro.ro_rt = 0;
	rtalloc(&ro);
	rt = ro.ro_rt;
	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if (rt &&
	    (*afswitch[dst->sa_family].af_netmatch)(&wildcard, &rt->rt_dst)) {
		rtfree(rt);
		rt = 0;
	}
	if (rt == 0) {
		rtinit(dst, gateway, RTF_GATEWAY);
		rtstat.rts_dynamic++;
		return;
	}
	/*
	 * Don't listen to the redirect if it's
	 * for a route to an interface. 
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		/*
		 * Smash the current notion of the gateway to
		 * this destination.  This is probably not right,
		 * as it's conceivable a flurry of redirects could
		 * cause the gateway value to fluctuate wildly during
		 * dynamic routing reconfiguration.
		 */
		rt->rt_gateway = *gateway;
		rtfree(rt);
		rtstat.rts_newgateway++;
		return;
	}
}

/*
 * Routing table ioctl interface.
 */
rtioctl(cmd, data)
	int cmd;
	caddr_t data;
{

	if (cmd != SIOCADDRT && cmd != SIOCDELRT)
		return (EINVAL);
	if (!suser())
		return (u.u_error);
	return (rtrequest(cmd, (struct rtentry *)data));
}

/*
 * Carry out a request to change the routing table.  Called by
 * interfaces at boot time to make their ``local routes'' known,
 * for ioctl's, and as the result of routing redirects.
 */
rtrequest(req, entry)
	int req;
	register struct rtentry *entry;
{
	register struct mbuf *m, **mprev;
	register struct rtentry *rt;
	struct afhash h;
	int s, error = 0, (*match)();
	u_int af;
	u_long hash;
	struct ifnet *ifp;

	af = entry->rt_dst.sa_family;
	if (af >= AF_MAX)
		return (EAFNOSUPPORT);
	(*afswitch[af].af_hash)(&entry->rt_dst, &h);
	if (entry->rt_flags & RTF_HOST) {
		hash = h.afh_hosthash;
		mprev = &rthost[hash % RTHASHSIZ];
	} else {
		hash = h.afh_nethash;
		mprev = &rtnet[hash % RTHASHSIZ];
	}
	match = afswitch[af].af_netmatch;
	s = splimp();
	for (; m = *mprev; mprev = &m->m_next) {
		rt = mtod(m, struct rtentry *);
		if (rt->rt_hash != hash)
			continue;
		if (entry->rt_flags & RTF_HOST) {
#define	equal(a1, a2) \
	(bcmp((caddr_t)(a1), (caddr_t)(a2), sizeof (struct sockaddr)) == 0)
			if (!equal(&rt->rt_dst, &entry->rt_dst))
				continue;
		} else {
			if (rt->rt_dst.sa_family != entry->rt_dst.sa_family ||
			    (*match)(&rt->rt_dst, &entry->rt_dst) == 0)
				continue;
		}
		if (equal(&rt->rt_gateway, &entry->rt_gateway))
			break;
	}
	switch (req) {

	case SIOCDELRT:
		if (m == 0) {
			error = ESRCH;
			goto bad;
		}
		*mprev = m->m_next;
		if (rt->rt_refcnt > 0) {
			rt->rt_flags &= ~RTF_UP;
			rttrash++;
			m->m_next = 0;
		} else
			(void) m_free(m);
		break;

	case SIOCADDRT:
		if (m) {
			error = EEXIST;
			goto bad;
		}
		ifp = if_ifwithaddr(&entry->rt_gateway);
		if (ifp == 0) {
			ifp = if_ifwithnet(&entry->rt_gateway);
			if (ifp == 0) {
				error = ENETUNREACH;
				goto bad;
			}
		}
		m = m_get(M_DONTWAIT, MT_RTABLE);
		if (m == 0) {
			error = ENOBUFS;
			goto bad;
		}
		*mprev = m;
		m->m_off = MMINOFF;
		m->m_len = sizeof (struct rtentry);
		rt = mtod(m, struct rtentry *);
		rt->rt_hash = hash;
		rt->rt_dst = entry->rt_dst;
		rt->rt_gateway = entry->rt_gateway;
		rt->rt_flags =
		    RTF_UP | (entry->rt_flags & (RTF_HOST|RTF_GATEWAY));
		rt->rt_refcnt = 0;
		rt->rt_use = 0;
		rt->rt_ifp = ifp;
		break;
	}
bad:
	splx(s);
	return (error);
}

/*
 * Set up a routing table entry, normally
 * for an interface.
 */
rtinit(dst, gateway, flags)
	struct sockaddr *dst, *gateway;
	int flags;
{
	struct rtentry route;
	int cmd;

	if (flags == -1) {
		cmd = (int)SIOCDELRT;
		flags = 0;
	} else {
		cmd = (int)SIOCADDRT;
	}
	bzero((caddr_t)&route, sizeof (route));
	route.rt_dst = *dst;
	route.rt_gateway = *gateway;
	route.rt_flags = flags;
	(void) rtrequest(cmd, &route);
}
