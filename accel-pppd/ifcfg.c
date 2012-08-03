#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/route.h>
#include "linux_ppp.h"

#include "triton.h"
#include "events.h"
#include "ppp.h"
#include "ipdb.h"
#include "log.h"
#include "backup.h"

// from /usr/include/linux/ipv6.h
struct in6_ifreq {
        struct in6_addr ifr6_addr;
        __u32           ifr6_prefixlen;
        int             ifr6_ifindex; 
};

static void devconf(struct ap_session *ses, const char *attr, const char *val)
{
	int fd;
	char fname[PATH_MAX];

	sprintf(fname, "/proc/sys/net/ipv6/conf/%s/%s", ses->ifname, attr);
	fd = open(fname, O_WRONLY);
	if (!fd) {
		log_ppp_error("failed to open '%s': %s\n", fname, strerror(errno));
		return;
	}

	write(fd, val, strlen(val));

	close(fd);
}

static void build_addr(struct ipv6db_addr_t *a, uint64_t intf_id, struct in6_addr *addr)
{
	memcpy(addr, &a->addr, sizeof(*addr));

	if (a->prefix_len <= 64)
		*(uint64_t *)(addr->s6_addr + 8) = intf_id;
	else
		*(uint64_t *)(addr->s6_addr + 8) |= intf_id & ((1 << (128 - a->prefix_len)) - 1);
}

void ap_session_ifup(struct ap_session *ses)
{
	struct ipv6db_addr_t *a;
	struct ifreq ifr;
	//struct rtentry rt;
	struct in6_ifreq ifr6;
	struct npioctl np;
	struct sockaddr_in addr;
	struct ppp_t *ppp;
	
	triton_event_fire(EV_SES_ACCT_START, ses);
	if (ses->stop_time)
		return;

	triton_event_fire(EV_SES_PRE_UP, ses);
	if (ses->stop_time)
		return;

	if (!ses->ctrl->dont_ifcfg) {
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, ses->ifname);

#ifdef USE_BACKUP
		if (!ses->backup || !ses->backup->internal) {
#endif
			if (ses->ipv4) {
				memset(&addr, 0, sizeof(addr));
				addr.sin_family = AF_INET;
				addr.sin_addr.s_addr = ses->ipv4->addr;
				memcpy(&ifr.ifr_addr, &addr, sizeof(addr));
				
				if (ioctl(sock_fd, SIOCSIFADDR, &ifr))
					log_ppp_error("failed to set IPv4 address: %s\n", strerror(errno));
			
				/*if (ses->ctrl->type == CTRL_TYPE_IPOE) {
					addr.sin_addr.s_addr = 0xffffffff;
					memcpy(&ifr.ifr_netmask, &addr, sizeof(addr));
					if (ioctl(sock_fd, SIOCSIFNETMASK, &ifr))
						log_ppp_error("failed to set IPv4 nask: %s\n", strerror(errno));
				}*/
				
				addr.sin_addr.s_addr = ses->ipv4->peer_addr;

				/*if (ses->ctrl->type == CTRL_TYPE_IPOE) {
					memset(&rt, 0, sizeof(rt));
					memcpy(&rt.rt_dst, &addr, sizeof(addr));
					rt.rt_flags = RTF_HOST | RTF_UP;
					rt.rt_metric = 1;
					rt.rt_dev = ifr.ifr_name;
					if (ioctl(sock_fd, SIOCADDRT, &rt, sizeof(rt)))
						log_ppp_error("failed to add route: %s\n", strerror(errno));
				} else*/ {
					memcpy(&ifr.ifr_dstaddr, &addr, sizeof(addr));
					
					if (ioctl(sock_fd, SIOCSIFDSTADDR, &ifr))
						log_ppp_error("failed to set peer IPv4 address: %s\n", strerror(errno));
				}
			}

			if (ses->ipv6) {
				devconf(ses, "accept_ra", "0");
				devconf(ses, "autoconf", "0");
				devconf(ses, "forwarding", "1");

				memset(&ifr6, 0, sizeof(ifr6));
				
				if (ses->ctrl->type != CTRL_TYPE_IPOE) {
					ifr6.ifr6_addr.s6_addr32[0] = htons(0xfe80);
					*(uint64_t *)(ifr6.ifr6_addr.s6_addr + 8) = ses->ipv6->intf_id;
					ifr6.ifr6_prefixlen = 64;
					ifr6.ifr6_ifindex = ses->ifindex;

					if (ioctl(sock6_fd, SIOCSIFADDR, &ifr6))
						log_ppp_error("faild to set LL IPv6 address: %s\n", strerror(errno));
				}
				
				list_for_each_entry(a, &ses->ipv6->addr_list, entry) {
					if (a->prefix_len == 128)
						continue;

					build_addr(a, ses->ipv6->intf_id, &ifr6.ifr6_addr);
					ifr6.ifr6_prefixlen = a->prefix_len;

					if (ioctl(sock6_fd, SIOCSIFADDR, &ifr6))
						log_ppp_error("failed to add IPv6 address: %s\n", strerror(errno));
				}
			}

			if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr))
				log_ppp_error("failed to get interface flags: %s\n", strerror(errno));

			ifr.ifr_flags |= IFF_UP;

			if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr))
				log_ppp_error("failed to set interface flags: %s\n", strerror(errno));

			if (ses->ctrl->type != CTRL_TYPE_IPOE) {
				ppp = container_of(ses, typeof(*ppp), ses);
				if (ses->ipv4) {
					np.protocol = PPP_IP;
					np.mode = NPMODE_PASS;

					if (ioctl(ppp->unit_fd, PPPIOCSNPMODE, &np))
						log_ppp_error("failed to set NP (IPv4) mode: %s\n", strerror(errno));
				}
				
				if (ses->ipv6) {
					np.protocol = PPP_IPV6;
					np.mode = NPMODE_PASS;

					if (ioctl(ppp->unit_fd, PPPIOCSNPMODE, &np))
						log_ppp_error("failed to set NP (IPv6) mode: %s\n", strerror(errno));
				}
			}
#ifdef USE_BACKUP
		}
#endif
	}
	
	ses->ctrl->started(ses);

	triton_event_fire(EV_SES_STARTED, ses);
}

void __export ap_session_ifdown(struct ap_session *ses)
{
	struct ifreq ifr;
	struct sockaddr_in addr;
	struct in6_ifreq ifr6;
	struct ipv6db_addr_t *a;

	if (ses->ctrl->dont_ifcfg)
		return;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, ses->ifname);

	ioctl(sock_fd, SIOCSIFFLAGS, &ifr);

	if (ses->ipv4) {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		memcpy(&ifr.ifr_addr,&addr,sizeof(addr));
		ioctl(sock_fd, SIOCSIFADDR, &ifr);
	}

	if (ses->ipv6) {
		memset(&ifr6, 0, sizeof(ifr6));
		ifr6.ifr6_addr.s6_addr32[0] = htons(0xfe80);
		*(uint64_t *)(ifr6.ifr6_addr.s6_addr + 8) = ses->ipv6->intf_id;
		ifr6.ifr6_prefixlen = 64;
		ifr6.ifr6_ifindex = ses->ifindex;

		ioctl(sock6_fd, SIOCDIFADDR, &ifr6);
	
		list_for_each_entry(a, &ses->ipv6->addr_list, entry) {
			if (a->prefix_len == 128)
				continue;

			build_addr(a, ses->ipv6->intf_id, &ifr6.ifr6_addr);
			ifr6.ifr6_prefixlen = a->prefix_len;

			ioctl(sock6_fd, SIOCDIFADDR, &ifr6);
		}
	}
}
