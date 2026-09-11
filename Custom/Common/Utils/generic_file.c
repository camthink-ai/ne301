#include "generic_file.h"
#include "cmsis_os2.h"

static file_instance_t instances[FS_MAX];
static int current_handle = -1;
static int next_handle = 0;
static osMutexId_t     file_mtx;

void file_lock(void)
{
    if(file_mtx == NULL){
        file_mtx = osMutexNew(NULL);
    }
    osMutexAcquire(file_mtx, osWaitForever);
}

void  file_unlock(void)
{
    osMutexRelease(file_mtx);
}

static file_instance_t* get_current_instance() 
{
    for (int i = 0; i < FS_MAX; i++) {
        if (instances[i].handle == current_handle) {
            return &instances[i];
        }
    }
    return NULL;
}

FS_Type_t file_get_current_type(void)
{
    for (int i = 0; i < FS_MAX; i++) {
        if (instances[i].handle == current_handle) {
            return (FS_Type_t)i;
        }
    }
    return FS_MAX;
}

void* file_fopen(const char *path, const char *mode) 
{
    file_instance_t* inst = get_current_instance();
    if (inst == NULL || inst->ops == NULL || inst->ops->fopen == NULL)
        return NULL;
    
    void* fd = inst->ops->fopen(inst->context, path, mode);
    if (fd != NULL) {
        inst->open_count++;
    }
    return fd;
}

int file_fclose(void *fd) 
{
    file_instance_t* inst = get_current_instance();
    if (inst == NULL || inst->ops == NULL || inst->ops->fclose == NULL)
        return -1;
    
    int ret = inst->ops->fclose(inst->context, fd);
    if (ret == 0) {
        inst->open_count--;
    }
    return ret;
}

int file_fwrite(void *fd, const void *buf, size_t size) 
{
    file_instance_t* inst = get_current_instance();
    if (inst == NULL || inst->ops == NULL || inst->ops->fwrite == NULL)
        return -1;
    return inst->ops->fwrite(inst->context, fd, buf, size);
}

int file_fread(void *fd, void *buf, size_t size)
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->fread == NULL)
        return -1;
    return inst->ops->fread(inst->context, fd, buf, size);
}

int file_remove(const char *path) 
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->remove == NULL)
        return -1;
    return inst->ops->remove(inst->context, path);
}

int file_rename(const char *oldpath, const char *newpath) 
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->rename == NULL)
        return -1;
    return inst->ops->rename(inst->context, oldpath, newpath);
}

int file_fflush(void *fd) 
{
    file_instance_t* inst = get_current_instance();
    if(inst->ops == NULL || inst->ops->fflush == NULL)
        return -1;
    return inst->ops->fflush(inst->context, fd);
}

long file_ftell(void *fd) 
{
    file_instance_t* inst = get_current_instance();
    if(inst->ops == NULL || inst->ops->ftell == NULL)
        return -1;
    return inst->ops->ftell(inst->context, fd);
}

int file_fseek(void *fd, long offset, int whence) 
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->fseek == NULL)
        return -1;
    return inst->ops->fseek(inst->context, fd, offset, whence);
}

void* file_opendir(const char *path) 
{
    file_instance_t* inst = get_current_instance();
    if (inst == NULL || inst->ops == NULL || inst->ops->opendir == NULL)
        return NULL;
    
    void* dd = inst->ops->opendir(inst->context, path);
    if (dd != NULL) {
        inst->open_count++;
    }
    return dd;
}

int file_closedir(void *dd) 
{
    file_instance_t* inst = get_current_instance();
    if (inst == NULL || inst->ops == NULL || inst->ops->closedir == NULL)
        return -1;
    
    int ret = inst->ops->closedir(inst->context, dd);
    if (ret == 0) {
        inst->open_count--;
    }
    return ret;
}

int file_readdir(void *dd, char *info)
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->readdir == NULL)
        return -1;
    return inst->ops->readdir(inst->context, dd, info);
}

int file_stat(const char *filename, struct stat *st)
{
    file_instance_t* inst = get_current_instance();
    if(inst == NULL || inst->ops == NULL || inst->ops->stat == NULL)
        return -1;
    return inst->ops->stat(inst->context, filename, st);

}

int file_ops_register(FS_Type_t type, file_ops_t *ops, void *context) 
{
    if (ops == NULL || type < 0 || type >= FS_MAX) return -1;

    if (instances[type].ops == NULL) {
        instances[type].ops = ops;
        instances[type].context = context;
        instances[type].handle = next_handle++;
        instances[type].open_count = 0;
        return instances[type].handle;
    }

    return -1;
}

int file_ops_unregister(int handle) 
{
    for (int i = 0; i < FS_MAX; i++) {
        if (instances[i].handle == handle) {
            if (instances[i].open_count > 0) {
                return -1;  
            }
            
            int is_current = (current_handle == handle);
            
            instances[i].ops = NULL;
            instances[i].context = NULL;
            instances[i].handle = -1;
            instances[i].open_count = 0;
            
            if (is_current) {
                current_handle = -1;  
                
                for (int j = 0; j < FS_MAX; j++) {
                    if (instances[j].ops != NULL) {
                        current_handle = instances[j].handle;
                        break;
                    }
                }

            }
            return 0;
        }
    }
    return -1;
}

int file_ops_switch(int handle) 
{
    file_instance_t* current_inst = get_current_instance();
    
    if (current_handle == handle) return 0;
    
    file_instance_t* target_inst = NULL;
    for (int i = 0; i < FS_MAX; i++) {
        if (instances[i].handle == handle) {
            target_inst = &instances[i];
            break;
        }
    }
    if (target_inst == NULL) return -1;

    if (current_inst != NULL && current_inst->open_count > 0) {
        uint32_t start = osKernelGetTickCount();
        while (current_inst->open_count > 0) {
            uint32_t elapsed = (osKernelGetTickCount() - start);
            if (elapsed > MAX_TIMEOUT_MS) {
                break;
            }
            
            uint32_t wait_start = osKernelGetTickCount();
            while ((osKernelGetTickCount() - wait_start) < CHECK_TIMEOUT_MS) {
                osDelay(1);
            }
        }
    }
    
    current_handle = handle;
    return 0;
}

void* disk_file_fopen(FS_Type_t type, const char *path, const char *mode) 
{
    if(type < 0 || type >= FS_MAX) return NULL;
    file_instance_t* inst = &instances[type];

    if (inst == NULL || inst->ops == NULL || inst->ops->fopen == NULL)
        return NULL;
    
    void* fd = inst->ops->fopen(inst->context, path, mode);
    if (fd != NULL) {
        inst->open_count++;
    }
    return fd;
}

int disk_file_fclose(FS_Type_t type, void *fd) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if (inst == NULL || inst->ops == NULL || inst->ops->fclose == NULL)
        return -1;
    
    int ret = inst->ops->fclose(inst->context, fd);
    if (ret == 0) {
        inst->open_count--;
    }
    return ret;
}

int disk_file_fwrite(FS_Type_t type, void *fd, const void *buf, size_t size) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if (inst == NULL || inst->ops == NULL || inst->ops->fwrite == NULL)
        return -1;
    return inst->ops->fwrite(inst->context, fd, buf, size);
}

int disk_file_fread(FS_Type_t type, void *fd, void *buf, size_t size)
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->fread == NULL)
        return -1;
    return inst->ops->fread(inst->context, fd, buf, size);
}

int disk_file_remove(FS_Type_t type, const char *path) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->remove == NULL)
        return -1;
    return inst->ops->remove(inst->context, path);
}

int disk_file_rename(FS_Type_t type, const char *oldpath, const char *newpath) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->rename == NULL)
        return -1;
    return inst->ops->rename(inst->context, oldpath, newpath);
}

int disk_file_fflush(FS_Type_t type, void *fd) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst->ops == NULL || inst->ops->fflush == NULL)
        return -1;
    return inst->ops->fflush(inst->context, fd);
}

long disk_file_ftell(FS_Type_t type, void *fd) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst->ops == NULL || inst->ops->ftell == NULL)
        return -1;
    return inst->ops->ftell(inst->context, fd);
}

int disk_file_fseek(FS_Type_t type, void *fd, long offset, int whence) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->fseek == NULL)
        return -1;
    return inst->ops->fseek(inst->context, fd, offset, whence);
}

void* disk_file_opendir(FS_Type_t type, const char *path) 
{
    if(type < 0 || type >= FS_MAX) return NULL;
    file_instance_t* inst = &instances[type];
    if (inst == NULL || inst->ops == NULL || inst->ops->opendir == NULL)
        return NULL;
    
    void* dd = inst->ops->opendir(inst->context, path);
    if (dd != NULL) {
        inst->open_count++;
    }
    return dd;
}

int disk_file_closedir(FS_Type_t type, void *dd) 
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if (inst == NULL || inst->ops == NULL || inst->ops->closedir == NULL)
        return -1;
    
    int ret = inst->ops->closedir(inst->context, dd);
    if (ret == 0) {
        inst->open_count--;
    }
    return ret;
}

int disk_file_readdir(FS_Type_t type, void *dd, char *info)
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->readdir == NULL)
        return -1;
    return inst->ops->readdir(inst->context, dd, info);
}

int disk_file_stat(FS_Type_t type, const char *filename, struct stat *st)
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->stat == NULL)
        return -1;
    return inst->ops->stat(inst->context, filename, st);
}

int disk_file_mkdir(FS_Type_t type, const char *path)
{
    if(type < 0 || type >= FS_MAX) return -1;
    file_instance_t* inst = &instances[type];
    if(inst == NULL || inst->ops == NULL || inst->ops->mkdir == NULL)
        return -1;
    return inst->ops->mkdir(inst->context, path);
}
