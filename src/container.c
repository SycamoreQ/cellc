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
#include "fs.h"

typedef struct {
    char *program;
    char **argv;
    int read_end;
    int write_end;
    fs_config_t *fs;
} child_args_t;

int child_fn(void *arg) {
    child_args_t *args = (child_args_t *)arg;

    close(args->write_end);

    char buf;
    read(args->read_end, &buf, 1);
    close(args->read_end);

    printf("inside container\n");

    fs_setup_overlay(args->fs);
    fs_pivot_root(args->fs->merged);

    // proc
    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", 0, NULL);

    // sys
    mkdir("/sys", 0555);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    // cgroup
    mkdir("/sys/fs", 0555);
    mkdir("/sys/fs/cgroup", 0555);
    mount("none", "/sys/fs/cgroup", "cgroup2", 0, NULL);

    sethostname("container", 9);

    char *child_argv[] = { args->program, NULL };
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    execv(args->program, child_argv);

    perror("execv failed");
    return 1;
}

void set_map(char* file, int inside_id, int outside_id, int len) {
    FILE* mapfd = fopen(file, "w");
    if (mapfd == NULL) {
        perror("open map file failed");
        return;
    }
    fprintf(mapfd, "%d %d %d", inside_id, outside_id, len);
    fclose(mapfd);
}

void setup_user_ns(pid_t pid) {
    char path[PATH_MAX];
    
    // Map UID 0 inside to current UID outside
    sprintf(path, "/proc/%d/uid_map", pid);
    set_map(path, 0, getuid(), 1);

    // Deny setgroups (required before mapping GID in many kernels)
    sprintf(path, "/proc/%d/setgroups", pid);
    int fd = open(path, O_WRONLY);
    write(fd, "deny", 4);
    close(fd);

    // Map GID 0 inside to your current GID outside
    sprintf(path, "/proc/%d/gid_map", pid);
    set_map(path, 0, getgid(), 1);
}

void container_run(char *program) {

    fs_config_t fs_config = {
        .lower  = "/root/container_fs/alpine",
        .upper  = "/root/container_fs/upper",
        .work   = "/root/container_fs/work",
        .merged = "/root/container_fs/merged"
    };

    child_args_t args = {
        program,
        NULL,
        0,
        0,
        &fs_config,
    };

    int stack_size = 1024 * 1024;
    char *stack = malloc(stack_size);
    char *stack_top = stack + stack_size;

    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS |
                CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWCGROUP | SIGCHLD;

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    args.read_end  = fd[0];
    args.write_end = fd[1];

    cgroup_config_t cgroup_config = {
        .cgroup_path  = "/sys/fs/cgroup/cellc",
        .memory_limit = 104857600,
        .cpu_quota    = 100000,
        .cpu_period   = 100000,
        .pids_max     = 32
    };

    pid_t pid = clone(child_fn, stack_top, flags | CLONE_NEWUSER, &args);
    if (pid == -1) {
        perror("clone failed");
        free(stack);
        return;
    }

    setup_user_ns(pid);

    // host configures cgroup
    cgroups_setup(pid, &cgroup_config);

    close(fd[0]);
    write(fd[1], "", 1);
    close(fd[1]);

    waitpid(pid, NULL, 0);

    cgroups_cleanup();
    free(stack);
}