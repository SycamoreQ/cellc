#ifndef net.h
#define net.h 

int netlink_open(); 

int netlink_send(int fd , char[] msg , long len); 


#endif