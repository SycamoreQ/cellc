#ifndef STATE_H
#define STATE_H

#define STATE_DIR "/run/cellc/containers"
#define MAX_PATH 512 


typedef struct {
    char container_id[64];
    pid_t pid;
    char status[32];
    char program[MAX_PATH];
} container_state_t;

void state_create(const char *container_id , pid_t pid , const char *program);
void state_update(const char *container_id , const char *status);
void state_list(); 
int state_get_pid(const char *container_id );
void state_cleanup(const char *container_id );

#endif 