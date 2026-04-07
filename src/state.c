#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <linux/if_link.h>
#include <sys/stat.h>
#include <linux/if_addr.h>
#include "net.h"
#include "state.h"

void state_create(const char *container_id, pid_t pid, const char *program) {
    char path[MAX_PATH];
    
    // Create base and container dirs
    mkdir(STATE_DIR, 0755); 
    snprintf(path, sizeof(path), "%s/%s", STATE_DIR, container_id);
    mkdir(path, 0755);

    // Write PID
    char pid_path[MAX_PATH];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", path);
    FILE *f = fopen(pid_path, "w");
    if (f) { fprintf(f, "%d", pid); fclose(f); }

    // Write Program
    char prog_path[MAX_PATH];
    snprintf(prog_path, sizeof(prog_path), "%s/program", path);
    f = fopen(prog_path, "w");
    if (f) { fprintf(f, "%s", program); fclose(f); }

    // Initial Status
    state_update(container_id, "created");
}

void state_update(const char *container_id, const char *status) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s/status", STATE_DIR, container_id);
    
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s", status);
        fclose(f);
    }
}

pid_t state_get_pid(const char *container_id) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s/pid", STATE_DIR, container_id);
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}



void state_cleanup(const char *container_id) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", STATE_DIR, container_id);
    
    char cmd[MAX_PATH + 10];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    
    if (system(cmd) == 0) {
        printf("Cleaned up state for %s\n", container_id);
    }
}


