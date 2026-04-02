#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <linux/if_link.h>
#include <linux/if_addr.h>
#include "net.h"

static int netlink_open(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        perror("netlink socket failed");
        return -1;
    }
    return fd;
}

static int netlink_send(int fd, void *buf, size_t len) {
    struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_pid    = 0,
        .nl_groups = 0
    };

    if (sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("netlink sendto failed");
        return -1;
    }

    char recv_buf[4096];
    int received = recv(fd, recv_buf, sizeof(recv_buf), 0);
    if (received < 0) {
        perror("netlink recv failed");
        return -1;
    }

    struct nlmsghdr *nh = (struct nlmsghdr *)recv_buf;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *nle = (struct nlmsgerr *)NLMSG_DATA(nh);
        if (nle->error == 0)
            return 0;
        fprintf(stderr, "kernel error: %s\n", strerror(-nle->error));
        return -1;
    }
    return 0;
}

static void netlink_add_attr(struct nlmsghdr *nlh, int type, void *data, int len) {
    struct rtattr *rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = RTA_LENGTH(len);
    if (data && len > 0)
        memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
}

int net_setup_host(pid_t pid) {
    int sock = netlink_open();
    if (sock < 0) return -1;

    char buf[4096];
    struct nlmsghdr *nlh;

    /*Create veth pair */ 
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlh->nlmsg_seq   = 1;
    nlh->nlmsg_pid   = 0;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    struct ifinfomsg *ifm = NLMSG_DATA(nlh);
    ifm->ifi_family = AF_UNSPEC;

    // veth0 name
    netlink_add_attr(nlh, IFLA_IFNAME, "veth0", 6);

    // open IFLA_LINKINFO
    struct rtattr *linkinfo = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_LINKINFO, NULL, 0);

    netlink_add_attr(nlh, IFLA_INFO_KIND, "veth", 5);

    // open IFLA_INFO_DATA
    struct rtattr *infodata = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_INFO_DATA, NULL, 0);

    // open VETH_INFO_PEER
    struct rtattr *peer = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, VETH_INFO_PEER, NULL, 0);

    // peer needs its own ifinfomsg header
    struct ifinfomsg peer_ifm = { .ifi_family = AF_UNSPEC };
    void *payload = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len);
    memcpy(payload, &peer_ifm, sizeof(peer_ifm));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + sizeof(peer_ifm);

    // veth1 name inside peer
    netlink_add_attr(nlh, IFLA_IFNAME, "veth1", 6);

    // move veth1 into container namespace
    char ns_path[64];
    snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/net", pid);
    int ns_fd = open(ns_path, O_RDONLY);
    if (ns_fd < 0) {
        perror("failed to open container netns");
        close(sock);
        return -1;
    }
    netlink_add_attr(nlh, IFLA_NET_NS_FD, &ns_fd, sizeof(int));
    close(ns_fd);

    // close VETH_INFO_PEER
    peer->rta_len = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len) - (char *)peer;
    // close IFLA_INFO_DATA
    infodata->rta_len = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len) - (char *)infodata;
    // close IFLA_LINKINFO
    linkinfo->rta_len = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len) - (char *)linkinfo;

    if (netlink_send(sock, buf, nlh->nlmsg_len) < 0) {
        fprintf(stderr, "veth creation failed\n");
        close(sock);
        return -1;
    }

    /*Assign IP to veth0*/
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_seq   = 2;
    nlh->nlmsg_pid   = 0;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));

    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    ifa->ifa_family    = AF_INET;
    ifa->ifa_prefixlen = 24;
    ifa->ifa_index     = if_nametoindex("veth0");

    struct in_addr host_addr;
    inet_pton(AF_INET, "10.0.0.1", &host_addr);
    netlink_add_attr(nlh, IFA_LOCAL,   &host_addr, sizeof(host_addr));
    netlink_add_attr(nlh, IFA_ADDRESS, &host_addr, sizeof(host_addr));

    if (netlink_send(sock, buf, nlh->nlmsg_len) < 0) {
        fprintf(stderr, "IP assignment failed\n");
        close(sock);
        return -1;
    }

    /*Bring veth0 up*/
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = 3;
    nlh->nlmsg_pid   = 0;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    struct ifinfomsg *ifi_up = NLMSG_DATA(nlh);
    ifi_up->ifi_family = AF_UNSPEC;
    ifi_up->ifi_index  = if_nametoindex("veth0");
    ifi_up->ifi_flags  = IFF_UP;
    ifi_up->ifi_change = IFF_UP;

    if (netlink_send(sock, buf, nlh->nlmsg_len) < 0) {
        fprintf(stderr, "bringing veth0 up failed\n");
        close(sock);
        return -1;
    }

    /*IP forwarding + NAT*/
    int fwd_fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
    if (fwd_fd >= 0) {
        write(fwd_fd, "1", 1);
        close(fwd_fd);
    }
    system("iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -j MASQUERADE");

    close(sock);
    return 0;
}

void net_cleanup(void) {
    system("iptables -t nat -D POSTROUTING -s 10.0.0.0/24 -j MASQUERADE");
}

int net_setup_container(void) {
    return 0;
}

void netlink_setup_container(int nl_sock, const char *container_ip, const char *gateway_ip) {
    char buf[4096];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;

    //loopback and veth1 up 
    const char *ifs[] = {"lo", "veth1"};
    for (int i = 0; i < 2; i++) {
        memset(buf, 0, sizeof(buf));
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        nlh->nlmsg_type = RTM_NEWLINK;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        struct ifinfomsg *ifm = (struct ifinfomsg *)NLMSG_DATA(nlh);
        ifm->ifi_index = if_nametoindex(ifs[i]);
        ifm->ifi_flags = IFF_UP;
        ifm->ifi_change = IFF_UP;
        send(nl_sock, nlh, nlh->nlmsg_len, 0);
    }

    //assign ip to veth1
    int veth1_idx = if_nametoindex("veth1");
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = 24;
    ifa->ifa_index = veth1_idx;
    struct in_addr c_addr;
    inet_pton(AF_INET, container_ip, &c_addr);
    netlink_add_attr(nlh, sizeof(buf), IFA_LOCAL, &c_addr, sizeof(c_addr));
    send(nl_sock, nlh, nlh->nlmsg_len, 0);

    //default route
    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlh->nlmsg_type = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    struct rtmsg *rtm = (struct rtmsg *)NLMSG_DATA(nlh);
    rtm->rtm_family = AF_INET;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_STATIC;
    rtm->rtm_scope = RT_SCOPE_UNIVERSE;
    rtm->rtm_type = RTN_UNICAST;
    rtm->rtm_dst_len = 0; // Default route mask /0

    struct in_addr gw;
    inet_pton(AF_INET, gateway_ip, &gw);
    netlink_add_attr(nlh, sizeof(buf), RTA_GATEWAY, &gw, sizeof(gw));
    netlink_add_attr(nlh, sizeof(buf), RTA_OIF, &veth1_idx, sizeof(int));

    send(nl_sock, nlh, nlh->nlmsg_len, 0);
}