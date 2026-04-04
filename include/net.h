#ifndef net
#define net

#include <sys/types.h>

int net_setup_host(pid_t pid);
int net_setup_container(void);
void net_cleanup(void);


#endif