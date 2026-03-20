#define _GNU_SOURCE

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#include "container.h"
#include <sys/syscall.h>
#include "fs.h"



typedef struct {
    char *program;
    char **argv;
    int read_end; 
    int write_end; 
    fs_config_t *fs; 
} child_args_t;

fs_config_t fs_config = {
    .lower  = "/root/container_fs/alpine",
    .upper  = "/root/container_fs/upper",
    .work   = "/root/container_fs/work",
    .merged = "/root/container_fs/merged"
};



int child_fn(void *arg) {
    child_args_t *args = (child_args_t *)arg;
    printf("inside container\n");

    close(args -> write_end); 

    char buf[100];
    read(args-> read_end, &buf, 1);

    close(args -> read_end);


    char *child_argv[] = { args->program, NULL };
    fs_setup_overlay(args -> fs); 
    fs_pivot_root(args -> fs -> merged); 
    mount("proc", "/proc", "proc", 0, NULL);
    sethostname("container", 9) ; // sets the hostname inside the UTS (unix time sys) namespace without affecting the host
    execv(args->program, child_argv);
    perror("execv failed");
    return 1;
}

void container_run(char *program) {
    child_args_t args = { 
        program, 
        NULL ,
        0 , 
        0 , 
        &fs_config
    }; 

    int stack_size = 1024 * 1024;
    char *stack = malloc(stack_size);
    char *stack_top = stack + stack_size;

    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | SIGCHLD;

    int fd[2];
    
    if (pipe(fd) == -1) {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    args.read_end  = fd[0];   
    args.write_end = fd[1]; 

    pid_t pid = clone(child_fn, stack_top, flags, &args);
    if (pid == -1) {
        perror("clone failed");
        free(stack);
        return;
    }

    close(fd[0]); 
    char message[] = "\0";
    write(fd[1], message, sizeof(message));
    close(fd[1]);

    waitpid(pid, NULL, 0);
    free(stack);
}
