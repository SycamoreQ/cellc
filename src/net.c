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
    if (fd < 0) { perror("netlink socket failed"); return -1; }
    return fd;
}

static int netlink_send(int fd, void *buf, size_t len) {
    struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
    if (sendto(fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;

    char recv_buf[4096];
    int received = recv(fd, recv_buf, sizeof(recv_buf), 0);
    if (received < 0) return -1;

    struct nlmsghdr *nh = (struct nlmsghdr *)recv_buf;
    if (nh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *nle = (struct nlmsgerr *)NLMSG_DATA(nh);
        if (nle->error == 0) return 0;
        fprintf(stderr, "kernel error: %s\n", strerror(-nle->error));
        return -1;
    }
    return 0;
}

static void netlink_add_attr(struct nlmsghdr *nlh, int type, void *data, int len) {
    struct rtattr *rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = RTA_LENGTH(len);
    if (data && len > 0) memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
}

int net_setup_host(pid_t pid , const char *container_id) {
    char veth_host[IFNAMSIZ], veth_child[IFNAMSIZ];
    snprintf(veth_host, IFNAMSIZ, "vH_%s", container_id);
    snprintf(veth_child, IFNAMSIZ, "vC_%s", container_id);

    int sock = netlink_open();
    if (sock < 0) return -1;

    char buf[4096];
    struct nlmsghdr *nlh;

    /* Message 1: Create veth pair */
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    ((struct ifinfomsg *)NLMSG_DATA(nlh))->ifi_family = AF_UNSPEC;

    netlink_add_attr(nlh, IFLA_IFNAME, veth_host, strlen(veth_host) + 1);
    struct rtattr *linkinfo = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_LINKINFO, NULL, 0);
    netlink_add_attr(nlh, IFLA_INFO_KIND, "veth", 5);
    struct rtattr *infodata = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, IFLA_INFO_DATA, NULL, 0);
    struct rtattr *peer = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    netlink_add_attr(nlh, VETH_INFO_PEER, NULL, 0);

    struct ifinfomsg peer_ifm = { .ifi_family = AF_UNSPEC };
    memcpy((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len), &peer_ifm, sizeof(peer_ifm));
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + sizeof(peer_ifm);
    netlink_add_attr(nlh, IFLA_IFNAME, veth_child, strlen(veth_child) + 1);

    peer->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)peer;
    infodata->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)infodata;
    linkinfo->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)linkinfo;

    if (netlink_send(sock, buf, nlh->nlmsg_len) < 0) { close(sock); return -1; }

    /* Message 2: Move to NS and RENAME to veth1 */
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    struct ifinfomsg *ifm2 = NLMSG_DATA(nlh);
    ifm2->ifi_index = if_nametoindex(veth_child);

    netlink_add_attr(nlh, IFLA_IFNAME, "veth1", 6);
    char ns_path[64];
    snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/net", pid);
    int ns_fd = open(ns_path, O_RDONLY);
    if (ns_fd >= 0) {
        netlink_add_attr(nlh, IFLA_NET_NS_FD, &ns_fd, sizeof(int));
        netlink_send(sock, buf, nlh->nlmsg_len);
        close(ns_fd);
    }

    /* Message 3: IP to veth_host */
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = 24;
    ifa->ifa_index = if_nametoindex(veth_host);
    struct in_addr host_addr;
    inet_pton(AF_INET, "10.0.0.1", &host_addr);
    netlink_add_attr(nlh, IFA_LOCAL, &host_addr, sizeof(host_addr));
    netlink_add_attr(nlh, IFA_ADDRESS, &host_addr, sizeof(host_addr));
    netlink_send(sock, buf, nlh->nlmsg_len);

    /* Message 4: veth_host UP */
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    struct ifinfomsg *ifi_up = NLMSG_DATA(nlh);
    ifi_up->ifi_index = if_nametoindex(veth_host);
    ifi_up->ifi_flags = IFF_UP;
    ifi_up->ifi_change = IFF_UP;
    netlink_send(sock, buf, nlh->nlmsg_len);

    system("iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -j MASQUERADE");
    close(sock);
    return 0;
}

void net_cleanup(const char *container_id) {
    char veth_host[IFNAMSIZ];
    snprintf(veth_host, IFNAMSIZ, "vH_%s", container_id);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ip link delete %s 2>/dev/null", veth_host);
    system(cmd);
    system("iptables -t nat -D POSTROUTING -s 10.0.0.0/24 -j MASQUERADE 2>/dev/null");
}


int net_setup_container(void) {
    int sock = netlink_open();
    if (sock < 0) return -1;
    char buf[4096];
    struct nlmsghdr *nlh;
    const char *ifs[] = {"lo", "veth1"};
    for (int i = 0; i < 2; i++) {
        memset(buf, 0, sizeof(buf));
        nlh = (struct nlmsghdr *)buf;
        nlh->nlmsg_type = RTM_NEWLINK;
        nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
        nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        struct ifinfomsg *ifm = NLMSG_DATA(nlh);
        ifm->ifi_index = if_nametoindex(ifs[i]);
        ifm->ifi_flags = IFF_UP;
        ifm->ifi_change = IFF_UP;
        netlink_send(sock, buf, nlh->nlmsg_len);
    }
    int veth1_idx = if_nametoindex("veth1");
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET;
    ifa->ifa_prefixlen = 24;
    ifa->ifa_index = veth1_idx;
    struct in_addr c_addr;
    inet_pton(AF_INET, "10.0.0.2", &c_addr);
    netlink_add_attr(nlh, IFA_LOCAL, &c_addr, sizeof(c_addr));
    netlink_add_attr(nlh, IFA_ADDRESS, &c_addr, sizeof(c_addr));
    netlink_send(sock, buf, nlh->nlmsg_len);
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    struct rtmsg *rtm = NLMSG_DATA(nlh);
    rtm->rtm_family = AF_INET;
    rtm->rtm_table = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_STATIC;
    rtm->rtm_scope = RT_SCOPE_UNIVERSE;
    rtm->rtm_type = RTN_UNICAST;
    struct in_addr gw;
    inet_pton(AF_INET, "10.0.0.1", &gw);
    netlink_add_attr(nlh, RTA_GATEWAY, &gw, sizeof(gw));
    netlink_add_attr(nlh, RTA_OIF, &veth1_idx, sizeof(int));
    netlink_send(sock, buf, nlh->nlmsg_len);
    close(sock);
    return 0;
}