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
#include "net.h"

int netlink_open() {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        perror("netlink socket failed");
        return -1;
    }
    return fd;
}

int netlink_send(int fd, void *buf, size_t len) {
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
        if (nle->error == 0) {
            return 0;
        }
        fprintf(stderr, "kernel error: %s\n", strerror(-nle->error));
        return -1;
    }
    return 0;
}


void netlink_add_attr(struct nlmsghdr *nlh, int type, void *data, int len) {
    struct rtattr *rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = RTA_LENGTH(len);
    memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
}


void netlink_setup_veth(struct nlmsghdr *nlh, int max_len) {
    //Initial Interface Info (for veth0)
    struct ifinfomsg *ifm = (struct ifinfomsg *)NLMSG_DATA(nlh);
    ifm->ifi_family = AF_UNSPEC;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*ifm));

    //Set veth0 name
    netlink_add_attr(nlh, IFLA_IFNAME, "veth0", 6);

    //START NEST: IFLA_LINKINFO
    struct rtattr *linkinfo = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_LINKINFO, NULL, 0); 

    // Set Link Kind
    netlink_add_attr(nlh, IFLA_INFO_KIND, "veth", 5);

    //START NEST: IFLA_INFO_DATA
    struct rtattr *infodata = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_INFO_DATA, NULL, 0);

    //START NEST: VETH_INFO_PEER
    struct rtattr *peer = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, VETH_INFO_PEER, NULL, 0);

    struct ifinfomsg peer_ifm = { .ifi_family = AF_UNSPEC };
    // Manually append the header into the buffer
    void *payload = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len);
    memcpy(payload, &peer_ifm, sizeof(peer_ifm));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + sizeof(peer_ifm);

    // Set veth1 name (inside the peer nest)
    netlink_add_attr(nlh, IFLA_IFNAME, "veth1", 6);

    peer->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)peer;
    infodata->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)infodata;
    linkinfo->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)linkinfo;

    char ns_path[64];
    snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/net", pid);
    int ns_fd = open(ns_path, O_RDONLY);
    
    if (ns_fd >= 0) {
        //Create this peer directly in this namespace
        netlink_add_attr(nlh, max_len, IFLA_NET_NS_FD, &ns_fd, sizeof(int));
        close(ns_fd);
    } else {
        perror("Failed to open namespace");
    }

    send(sock, nlh, nlh->nlmsg_len, 0);

    int if_idx = if_nametoindex("veth0");
    if (if_idx == 0) return; 

    memset(buf, 0, sizeof(buf));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;

    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = 24; // /24 subnet
    ifa->ifa_index = if_idx;

    struct in_addr addr;
    inet_pton(AF_INET, host_ip, &addr);
    netlink_add_attr(nlh, sizeof(buf), IFA_LOCAL, &addr, sizeof(addr));
    netlink_add_attr(nlh, sizeof(buf), IFA_ADDRESS, &addr, sizeof(addr));

    send(sock, nlh, nlh->nlmsg_len, 0);

    int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
    write(fd, "1", 1);
    close(fd);

    // NAT via iptables - shelling out is fine here
    system("iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -j MASQUERADE");
}

