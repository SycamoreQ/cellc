#ifndef FS_H
#define FS_H 

// four directories to be created
typedef struct {
    char *lower; 
    char *upper; 
    char *work; 
    char *merged; 
} fs_config_t; 

int fs_setup_overlay(fs_config_t *config); 

int fs_pivot_root(const char *merged); 
#endif 