#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "cgroups.h"

static int cgroup_write(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        perror(path);
        return -1;
    }

    if (write(fd, value, strlen(value)) == -1) {
        perror("write failed");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void cgroups_setup(pid_t pid, cgroup_config_t *config) {

    if (mkdir("/sys/fs/cgroup/cellc", 0755) == -1 && errno != EEXIST) {
        perror("mkdir failed");
        exit(1);
    }

    char mem_str[32];
    char cpu_str[64];
    char pids_str[32];

    snprintf(mem_str, sizeof(mem_str), "%ld", config->memory_limit);
    snprintf(cpu_str, sizeof(cpu_str), "%ld %ld", config->cpu_quota, config->cpu_period);
    snprintf(pids_str, sizeof(pids_str), "%ld", config->pids_max);

    if (cgroup_write("/sys/fs/cgroup/cellc/memory.max", mem_str) == -1)
        fprintf(stderr, "memory write failed\n");

    if (cgroup_write("/sys/fs/cgroup/cellc/cpu.max", cpu_str) == -1)
        fprintf(stderr, "cpu write failed\n");

    if (cgroup_write("/sys/fs/cgroup/cellc/pids.max", pids_str) == -1)
        fprintf(stderr, "pids write failed\n");

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    if (cgroup_write("/sys/fs/cgroup/cellc/cgroup.procs", pid_str) == -1)
        fprintf(stderr, "attach pid failed\n");
}

void cgroups_cleanup() {
    if (rmdir("/sys/fs/cgroup/cellc") == -1) {
        perror("cleanup failed");
    }
}