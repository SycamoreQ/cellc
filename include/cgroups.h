#ifndef CGROUPS_H
#define CGROUPS_H

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

typedef struct {
    const char *cgroup_path;   
    long memory_limit;          
    long cpu_quota;             
    long cpu_period;          
    long  pids_max;              
} cgroup_config_t;

int cgroups_setup(pid_t pid , cgroup_config_t *config); 

void cgroups_cleanup(); 

#endif 