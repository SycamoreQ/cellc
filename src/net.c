#define _GNU_SOURCE 

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include "container.h"
#include "cgroups.h"
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