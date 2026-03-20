#define _GNU_SOURCE

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include "container.h"
#include "fs.h"


//function builds the options string for the mount call. 
//The kernel expects a single string in this exact format:
//lowerdir=/path/to/alpine, upperdir=/path/to/upper, workdir=/path/to/work

int fs_setup_overlay(fs_config_t *config) {
    char str_buf[1024];

    snprintf(str_buf, sizeof(str_buf), "lowerdir=%s,upperdir=%s,workdir=%s",
             config->lower, config->upper, config->work);

    int ret = mount("overlay", config->merged, "overlay", 0, str_buf);

    if (ret == -1) {
        perror("overlay mount failed");
        return -1;
    }

    return 0;
}


// atomically swaps what is present in the cloned process to the Alpine merged dir and the host fs which is 
// in a temporary mount point ready to be unmounted 

int fs_pivot_root(const char *merged) {
    // MS_BIND , MS_REC are flags that allow directory to be visible to other points in the fs
    int ret = mount(merged , merged , NULL , MS_BIND | MS_REC , NULL);
    if (ret == -1) {
        perror("overlay mount failed");
        return -1;
    }

    int ret1 = chdir(merged); 
    if (ret1 == -1) {
        perror("moving from curr to new dir failed");
        return -1;
    }

    //At this point the kernel swaps the root. Alpine filesystem is now /. The old host root is temporarily accessible but stacked underneath.
    long ret2 = syscall(SYS_pivot_root, ".", ".");
    if (ret2 == -1) {
        perror("failed to make syscall");
        return -1;
    }

    chdir("/"); 

    //detach old fs
    int ret3 = umount2("/", MNT_DETACH);
    if (ret3 == -1){
        perror("failed to unmount old fs "); 
        return -1;
    }

    return 0 ;
    
}