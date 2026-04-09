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
#include "net.h"
#include "state.h"

typedef struct {
    char *program;
    char **argv;
    int read_end;
    int write_end;
    fs_config_t *fs;
} child_args_t;

int child_fn(void *arg) {
    child_args_t *args = (child_args_t *)arg;

    //block and wait for the host to finish its setup
    char buf;
    if (read(args->read_end, &buf, 1) < 0) {
        perror("Sync read failed");
        return 1;
    }
    close(args->read_end);
    close(args->write_end);

    //Setup mounts and filesystem
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        perror("make root private failed");
        return 1;
    }

    if (fs_setup_overlay(args->fs) < 0) {
        fprintf(stderr, "overlay failed\n");
        return 1;
    }

    if (fs_pivot_root(args->fs->merged) < 0) {
        fprintf(stderr, "pivot_root failed\n");
        return 1;
    }

    //Mount special filesystems
    mkdir("/proc", 0555);
    mount("proc", "/proc", "proc", 0, NULL);

    mkdir("/sys", 0555);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    sethostname("cellc-container", 15);

    //Finalize container-side network
    net_setup_container();

    //Prepare environment and transform
    char *child_argv[] = { args->program, NULL };
    char *envp[] = {
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "HOME=/root",
        "TERM=xterm",
        NULL
    };

    // Use execve at the VERY END to pass your custom PATH
    execve(args->program, child_argv, envp);

    // If we reach here, execve failed
    perror("execve failed");
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

void container_run(char *program , const char *container_id) {

    snprintf(lower,  MAX_PATH, "/root/container_fs/%s/alpine", container_id);
    snprintf(upper,  MAX_PATH, "/root/container_fs/%s/upper",  container_id);
    snprintf(work,   MAX_PATH, "/root/container_fs/%s/work",   container_id);
    snprintf(merged, MAX_PATH, "/root/container_fs/%s/merged", container_id);

    fs_config_t fs_config = {
        .lower  = lower,
        .upper  = upper,
        .work   = work,
        .merged = merged
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

    pid_t pid = clone(child_fn, stack_top, flags, &args);
    if (pid == -1) {
        perror("clone failed");
        free(stack);
        return;
    }

    if (net_setup_host(pid, container_id) < 0) {
        fprintf(stderr, "Network setup failed\n");
    }
    
    state_create(container_id, pid, program);
    state_update(container_id, "running");




    // host configures cgroup
    if (cgroups_setup(pid, &cgroup_config) < 0){
        fprintf(stderr , "cgroup setup failed , continuing anyway");
    }
    net_setup_host(pid);

    close(fd[0]);
    write(fd[1], "", 1);
    close(fd[1]);

    waitpid(pid, NULL, 0);
    state_create(container_id, pid, program);
    state_update(container_id, "running");

    cgroups_cleanup();
    net_cleanup();
    free(stack);
}