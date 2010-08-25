/* -*- mode: c++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: t -*-
 * vim: ts=4 sw=4 noet ai cindent syntax=cpp
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2004, Hannu Saransaari and Lauri Hakkarainen
 * Copyright (c) 2005-2010 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include "conky.h"
#include "logging.h"
#include "specials.h"
#include "net/if.h"
#include "text_object.h"
#include "net_stat.h"
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* network interface stuff */

enum if_up_strictness_ {
	IFUP_UP,
	IFUP_LINK,
	IFUP_ADDR
};

template<>
conky::lua_traits<if_up_strictness_>::Map conky::lua_traits<if_up_strictness_>::map = {
	{ "up",      IFUP_UP },
	{ "link",    IFUP_LINK },
	{ "address", IFUP_ADDR }
};

static conky::simple_config_setting<if_up_strictness_> if_up_strictness("if_up_strictness",
																		IFUP_UP, true);

struct net_stat netstats[MAX_NET_INTERFACES];

struct net_stat *get_net_stat(const char *dev, void *free_at_crash1, void *free_at_crash2)
{
	unsigned int i;

	if (!dev) {
		return 0;
	}

	/* find interface stat */
	for (i = 0; i < MAX_NET_INTERFACES; i++) {
		if (netstats[i].dev && strcmp(netstats[i].dev, dev) == 0) {
			return &netstats[i];
		}
	}

	/* wasn't found? add it */
	for (i = 0; i < MAX_NET_INTERFACES; i++) {
		if (netstats[i].dev == 0) {
			netstats[i].dev = strndup(dev, text_buffer_size);
			return &netstats[i];
		}
	}

	CRIT_ERR(free_at_crash1, free_at_crash2, "too many interfaces used (limit is %d)", MAX_NET_INTERFACES);
	return 0;
}

void parse_net_stat_arg(struct text_object *obj, const char *arg, void *free_at_crash)
{
	if (!arg)
		arg = DEFAULTNETDEV;

	obj->data.opaque = get_net_stat(arg, obj, free_at_crash);
}

void parse_net_stat_bar_arg(struct text_object *obj, const char *arg, void *free_at_crash)
{
	if (arg) {
		arg = scan_bar(obj, arg, 1);
		obj->data.opaque = get_net_stat(arg, obj, free_at_crash);
	} else {
		// default to DEFAULTNETDEV
		char *buf = strndup(DEFAULTNETDEV, text_buffer_size);
		obj->data.opaque = get_net_stat(buf, obj, free_at_crash);
		free(buf);
	}
}

void print_downspeed(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	human_readable(ns->recv_speed, p, p_max_size);
}

void print_downspeedf(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	spaced_print(p, p_max_size, "%.1f", 8, ns->recv_speed / 1024.0);
}

void print_upspeed(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	human_readable(ns->trans_speed, p, p_max_size);
}

void print_upspeedf(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	spaced_print(p, p_max_size, "%.1f", 8, ns->trans_speed / 1024.0);
}

void print_totaldown(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	human_readable(ns->recv, p, p_max_size);
}

void print_totalup(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	human_readable(ns->trans, p, p_max_size);
}

void print_addr(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	if ((ns->addr.sa_data[2] & 255) == 0 &&
	    (ns->addr.sa_data[3] & 255) == 0 &&
	    (ns->addr.sa_data[4] & 255) == 0 &&
	    (ns->addr.sa_data[5] & 255) == 0) {
		snprintf(p, p_max_size, "No Address");
	} else {
		snprintf(p, p_max_size, "%u.%u.%u.%u",
		         ns->addr.sa_data[2] & 255,
		         ns->addr.sa_data[3] & 255,
		         ns->addr.sa_data[4] & 255,
		         ns->addr.sa_data[5] & 255);
	}
}

#ifdef __linux__
void print_addrs(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	if (NULL != ns->addrs && strlen(ns->addrs) > 2) {
		ns->addrs[strlen(ns->addrs) - 2] = 0; /* remove ", " from end of string */
		strncpy(p, ns->addrs, p_max_size);
	} else {
		strncpy(p, "0.0.0.0", p_max_size);
	}
}
#endif /* __linux__ */

#ifdef BUILD_X11
void parse_net_stat_graph_arg(struct text_object *obj, const char *arg, void *free_at_crash)
{
	char *buf = 0;
	buf = scan_graph(obj, arg, 0);

	// default to DEFAULTNETDEV
	if (buf) {
		obj->data.opaque = get_net_stat(buf, obj, free_at_crash);
		free(buf);
		return;
	}
	obj->data.opaque = get_net_stat(DEFAULTNETDEV, obj, free_at_crash);
}

double downspeedgraphval(struct text_object *obj)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	return (ns ? (ns->recv_speed / 1024.0) : 0);
}

double upspeedgraphval(struct text_object *obj)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	return (ns ? (ns->trans_speed / 1024.0) : 0);
}
#endif /* BUILD_X11 */

#ifdef BUILD_WLAN
void print_wireless_essid(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns) {
		for(unsigned int i = 0; *(netstats[i].dev) != 0; i++) {
			if(*(netstats[i].essid) != 0) {
				snprintf(p, p_max_size, "%s", netstats[i].essid);
				return;
			}
		}
		return;
	}

	snprintf(p, p_max_size, "%s", ns->essid);
}
void print_wireless_mode(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	snprintf(p, p_max_size, "%s", ns->mode);
}
void print_wireless_channel(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	if(ns->channel != 0) {
		snprintf(p, p_max_size, "%i", ns->channel);
	} else {
		snprintf(p, p_max_size, "/");
	}
}
void print_wireless_frequency(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	if(ns->freq[0] != 0) {
		snprintf(p, p_max_size, "%s", ns->freq);
	} else {
		snprintf(p, p_max_size, "/");
	}
}
void print_wireless_bitrate(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	snprintf(p, p_max_size, "%s", ns->bitrate);
}
void print_wireless_ap(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	snprintf(p, p_max_size, "%s", ns->ap);
}
void print_wireless_link_qual(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	spaced_print(p, p_max_size, "%d", 4, ns->link_qual);
}
void print_wireless_link_qual_max(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	spaced_print(p, p_max_size, "%d", 4, ns->link_qual_max);
}
void print_wireless_link_qual_perc(struct text_object *obj, char *p, int p_max_size)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return;

	if (ns->link_qual_max > 0) {
		spaced_print(p, p_max_size, "%.0f", 5,
				(double) ns->link_qual /
				ns->link_qual_max * 100);
	} else {
		spaced_print(p, p_max_size, "unk", 5);
	}
}
double wireless_link_barval(struct text_object *obj)
{
	struct net_stat *ns = (struct net_stat *)obj->data.opaque;

	if (!ns)
		return 0;

	return (double)ns->link_qual / ns->link_qual_max;
}
#endif /* BUILD_WLAN */

void clear_net_stats(void)
{
	int i;
	for (i = 0; i < MAX_NET_INTERFACES; i++) {
		free_and_zero(netstats[i].dev);
	}
	memset(netstats, 0, sizeof(netstats));
}

void parse_if_up_arg(struct text_object *obj, const char *arg)
{
	obj->data.opaque = strndup(arg, text_buffer_size);
}

void free_if_up(struct text_object *obj)
{
	free_and_zero(obj->data.opaque);
}

/* We should check if this is ok with OpenBSD and NetBSD as well. */
int interface_up(struct text_object *obj)
{
	int fd;
	struct ifreq ifr;
	char *dev = (char*)obj->data.opaque;

	if (!dev)
		return 0;

	if ((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		CRIT_ERR(NULL, NULL, "could not create sockfd");
		return 0;
	}
	strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr)) {
		/* if device does not exist, treat like not up */
		if (errno != ENODEV && errno != ENXIO)
			perror("SIOCGIFFLAGS");
		goto END_FALSE;
	}

	if (!(ifr.ifr_flags & IFF_UP)) /* iface is not up */
		goto END_FALSE;
	if (if_up_strictness.get(*state) == IFUP_UP)
		goto END_TRUE;

	if (!(ifr.ifr_flags & IFF_RUNNING))
		goto END_FALSE;
	if (if_up_strictness.get(*state) == IFUP_LINK)
		goto END_TRUE;

	if (ioctl(fd, SIOCGIFADDR, &ifr)) {
		perror("SIOCGIFADDR");
		goto END_FALSE;
	}
	if (((struct sockaddr_in *)&(ifr.ifr_ifru.ifru_addr))->sin_addr.s_addr)
		goto END_TRUE;

END_FALSE:
	close(fd);
	return 0;
END_TRUE:
	close(fd);
	return 1;
}

struct _dns_data {
	_dns_data() : nscount(0), ns_list(0) {}
	int nscount;
	char **ns_list;
};

static _dns_data dns_data;

void free_dns_data(struct text_object *obj)
{
	int i;

	(void)obj;

	for (i = 0; i < dns_data.nscount; i++)
		free(dns_data.ns_list[i]);
	if (dns_data.ns_list)
		free(dns_data.ns_list);
	memset(&dns_data, 0, sizeof(dns_data));
}

int update_dns_data(void)
{
	FILE *fp;
	char line[256];
	//static double last_dns_update = 0.0;

	/* maybe updating too often causes higher load because of /etc lying on a real FS
	if (current_update_time - last_dns_update < 10.0)
		return 0;

	last_dns_update = current_update_time;
	*/

	free_dns_data(NULL);

	if ((fp = fopen("/etc/resolv.conf", "r")) == NULL)
		return 0;
	while(!feof(fp)) {
		if (fgets(line, 255, fp) == NULL) {
			break;
		}
		if (!strncmp(line, "nameserver ", 11)) {
			line[strlen(line) - 1] = '\0';	// remove trailing newline
			dns_data.nscount++;
			dns_data.ns_list = (char**)realloc(dns_data.ns_list, dns_data.nscount * sizeof(char *));
			dns_data.ns_list[dns_data.nscount - 1] = strndup(line + 11, text_buffer_size);
		}
	}
	fclose(fp);
	return 0;
}

void parse_nameserver_arg(struct text_object *obj, const char *arg)
{
	obj->data.l = arg ? atoi(arg) : 0;
}

void print_nameserver(struct text_object *obj, char *p, int p_max_size)
{
	if (dns_data.nscount > obj->data.l)
		snprintf(p, p_max_size, "%s", dns_data.ns_list[obj->data.l]);
}
