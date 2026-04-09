#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     
#include <sys/wait.h>   
#include <sys/stat.h>
#include <signal.h>     
#include <sched.h>      
#include "container.h"

#include "container.h"
#include "state.h"

void container_exec(const char *container_id, char **argv) {
    //Get the PID of the running container from state files
    pid_t target_pid = state_get_pid(container_id);
    if (target_pid == -1) {
        fprintf(stderr, "Container %s not found or not running\n", container_id);
        exit(1);
    }

    //Define the namespaces to enter
    const char *namespaces[] = { "mnt", "net", "pid", "uts", "ipc" };
    char ns_path[256];

    for (int i = 0; i < 5; i++) {
        snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns/%s", target_pid, namespaces[i]);
        
        int fd = open(ns_path, O_RDONLY);
        if (fd == -1) {
            perror("Failed to open namespace file");
            exit(1);
        }

        // 3. JOIN the namespace
        if (setns(fd, 0) == -1) {
            perror("setns failed");
            exit(1);
        }
        close(fd);
    }

    //The setns(pid_ns) only affects children of the calling process.
    if (fork() == 0) {
        // We are now inside the container namespaces!

        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        
        //Execute the user's command (e.g., /bin/sh)
        if (execvp(argv[0], argv) == -1) {
            perror("execvp failed");
            exit(1);
        }
    } else {
        //Parent waits for the exec'd process to finish
        wait(NULL);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./cellc <command> [args...]\n");
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 4) { 
            printf("Usage: ./cellc run <id> <program>\n"); 
            return 1; 
        }
        container_run(argv[2], argv[3]);

    } else if (strcmp(argv[1], "ps") == 0) {
        state_list();

    } else if (strcmp(argv[1], "kill") == 0) {
        if (argc < 3) { printf("Usage: ./cellc kill <id>\n"); return 1; }
        pid_t pid = state_get_pid(argv[2]);
        if (pid > 0) kill(pid, SIGKILL);

    } else if (strcmp(argv[1], "exec") == 0) {
        if (argc < 4) { printf("Usage: ./cellc exec <id> <program>\n"); return 1; }
        container_exec(argv[2], &argv[3]);

    } else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}