#ifndef GENERIC_FILE_H
#define GENERIC_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#define MAX_FILENAME_LEN 64

#define CHECK_TIMEOUT_MS 10
#define MAX_TIMEOUT_MS 1000

typedef enum
{
    FS_FLASH = 0,
    FS_SD,
    FS_MAX,
} FS_Type_t;


typedef struct file_ops_t {
    void* (*fopen)(void *context, const char *path, const char *mode);
    int (*fclose)(void *context, void *fd);
    int (*fwrite)(void *context, void *fd, const void *buf, size_t size);
    int (*fread)(void *context, void *fd, void *buf, size_t size);
    int (*remove)(void *context, const char *path);
    int (*rename)(void *context, const char *oldpath, const char *newpath);
    int (*mkdir)(void *context, const char *path);
    long (*ftell)(void *context, void *fd);
    int (*fseek)(void *context, void *fd, long offset, int whence);
    int (*fflush)(void *context, void *fd);
    void* (*opendir)(void *context, const char *path);
    int (*readdir)(void *context, void *dd, char *info);
    int (*closedir)(void *context, void *dd);
    int (*stat)(void *context, const char *filename, struct stat *st);
} file_ops_t;

typedef struct {
    file_ops_t *ops;
    void *context;
    int handle;
    int open_count;  
} file_instance_t;

FS_Type_t file_get_current_type(void);

void* file_fopen(const char *path, const char *mode);

int file_fclose(void *fd);

int file_fwrite(void *fd, const void *buf, size_t size);

int file_fread(void *fd, void *buf, size_t size);

int file_remove(const char *path);

int file_rename(const char *oldpath, const char *newpath);

int file_fflush(void *fd);

long file_ftell(void *fd);

int file_fseek(void *fd, long offset, int whence);

void* file_opendir(const char *path);

int file_closedir(void *dd);

int file_readdir(void *dd, char *info);

int file_stat(const char *filename, struct stat *st);

int file_ops_register(FS_Type_t type, file_ops_t *ops, void *context);

int file_ops_unregister(int handle);

int file_ops_switch(int handle);

void* disk_file_fopen(FS_Type_t type, const char *path, const char *mode);

int disk_file_fclose(FS_Type_t type, void *fd);

int disk_file_fwrite(FS_Type_t type, void *fd, const void *buf, size_t size);

int disk_file_fread(FS_Type_t type, void *fd, void *buf, size_t size);

int disk_file_remove(FS_Type_t type, const char *path);

int disk_file_rename(FS_Type_t type, const char *oldpath, const char *newpath);

int disk_file_fflush(FS_Type_t type, void *fd);

long disk_file_ftell(FS_Type_t type, void *fd);

int disk_file_fseek(FS_Type_t type, void *fd, long offset, int whence);

void* disk_file_opendir(FS_Type_t type, const char *path);

int disk_file_closedir(FS_Type_t type, void *dd);

int disk_file_readdir(FS_Type_t type, void *dd, char *info);

int disk_file_stat(FS_Type_t type, const char *filename, struct stat *st);

int disk_file_mkdir(FS_Type_t type, const char *path);

#endif