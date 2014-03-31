/***
  This file is part of systemd.

  Copyright (C) 2013 Intel Corporation. All rights reserved.

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/filter.h>

#include "socket-util.h"

#include "dhcp-internal.h"

int dhcp_network_bind_raw_socket(int index, union sockaddr_union *link)
{
        struct sock_filter filter[] = {
            BPF_STMT(BPF_LD + BPF_B + BPF_ABS, offsetof(DHCPPacket, ip.protocol)), /* A <- IP protocol */
            BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 1, 0),                /* IP protocol = UDP? */
            BPF_STMT(BPF_RET + BPF_K, 0),                                          /* ignore */
            BPF_STMT(BPF_LD + BPF_H + BPF_ABS, offsetof(DHCPPacket, udp.dest)),    /* A <- UDP destination port */
            BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, DHCP_PORT_CLIENT, 1, 0),           /* UDP destination port = DHCP client? */
            BPF_STMT(BPF_RET + BPF_K, 0),                                          /* ignore */
            BPF_STMT(BPF_RET + BPF_K, 65535),                                      /* return all */
        };
        struct sock_fprog fprog = {
            .len = ELEMENTSOF(filter),
            .filter = filter
        };
        int s, one = 1;

        assert(index > 0);
        assert(link);

        s = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (s < 0)
                return -errno;

        link->ll.sll_family = AF_PACKET;
        link->ll.sll_protocol = htons(ETH_P_IP);
        link->ll.sll_ifindex =  index;
        link->ll.sll_halen = ETH_ALEN;
        memset(link->ll.sll_addr, 0xff, ETH_ALEN);

        if (setsockopt (s, SOL_PACKET, PACKET_AUXDATA, &one, sizeof(one)) < 0)
                return -errno;

        if (setsockopt(s, SOL_SOCKET, SO_ATTACH_FILTER, &fprog, sizeof(fprog)) < 0) {
                return -errno;
        }

        if (bind(s, &link->sa, sizeof(link->ll)) < 0) {
                safe_close(s);
                return -errno;
        }

        return s;
}

int dhcp_network_bind_udp_socket(int index, be32_t address, uint16_t port)
{
        int s;
        union sockaddr_union src = {
                .in.sin_family = AF_INET,
                .in.sin_port = htobe16(port),
                .in.sin_addr.s_addr = address,
        };

        s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (s < 0)
                return -errno;

        if (bind(s, &src.sa, sizeof(src.in)) < 0) {
                safe_close(s);
                return -errno;
        }

        return s;
}

int dhcp_network_send_raw_socket(int s, const union sockaddr_union *link,
                                 const void *packet, size_t len)
{
        assert(link);
        assert(packet);
        assert(len);

        if (sendto(s, packet, len, 0, &link->sa, sizeof(link->ll)) < 0)
                return -errno;

        return 0;
}

int dhcp_network_send_udp_socket(int s, be32_t address, uint16_t port,
                                 const void *packet, size_t len)
{
        union sockaddr_union dest = {
                .in.sin_family = AF_INET,
                .in.sin_port = htobe16(port),
                .in.sin_addr.s_addr = address,
        };

        if (sendto(s, packet, len, 0, &dest.sa, sizeof(dest.in)) < 0)
                return -errno;

        return 0;
}
