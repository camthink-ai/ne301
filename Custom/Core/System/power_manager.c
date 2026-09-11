#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"
#include "power_manager.h"

// Create power manager
PowerManager* power_manager_create() 
{
    PowerManager *manager = hal_mem_alloc_fast(sizeof(PowerManager));
    if (!manager) return NULL;

    manager->capacity = 10;
    manager->count = 0;
    manager->powers = hal_mem_alloc_fast(sizeof(PowerState*) * manager->capacity);
    if (!manager->powers) {
        hal_mem_free(manager);
        return NULL;
    }
    manager->lock = osMutexNew(NULL);
    manager->next_handle = 1;
    if (!manager->lock) {
        hal_mem_free(manager->powers);
        hal_mem_free(manager);
        return NULL;
    }
    return manager;
}

// Destroy power manager
void power_manager_destroy(PowerManager *manager) 
{
    if (!manager) return;
    osMutexAcquire(manager->lock, osWaitForever);
    size_t count = manager->count;
    PowerState **powers = manager->powers;
    manager->powers = NULL;
    manager->count = 0;
    osMutexRelease(manager->lock);

    for (size_t i = 0; i < count; i++) {
        PowerState *ps = powers[i];
        osMutexDelete(ps->lock);
        hal_mem_free(ps);
    }
    hal_mem_free(powers);
    osMutexDelete(manager->lock);
    hal_mem_free(manager);
}

// Register new power, return handle
PowerHandle power_manager_register(PowerManager *manager, const char *name, void (*power_on)(void), void (*power_off)(void)) 
{
    if (!manager || !name || !power_on || !power_off) return 0;

    osMutexAcquire(manager->lock, osWaitForever);

    for (size_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->powers[i]->name, name) == 0) {
            osMutexRelease(manager->lock);
            return 0; // Already exists
        }
    }

    if (manager->count >= manager->capacity) {
        size_t new_cap = manager->capacity * 2;
        PowerState **new_powers = realloc(manager->powers, sizeof(PowerState*) * new_cap);
        if (!new_powers) {
            osMutexRelease(manager->lock);
            return 0;
        }
        manager->powers = new_powers;
        manager->capacity = new_cap;
    }

    PowerState *ps = hal_mem_alloc_fast(sizeof(PowerState));
    if (!ps) {
        osMutexRelease(manager->lock);
        return 0;
    }

    strncpy(ps->name, name, sizeof(ps->name) - 1);
    ps->name[sizeof(ps->name) - 1] = '\0';
    ps->power_on = power_on;
    ps->power_off = power_off;
    ps->ref_count = 0;
    ps->is_on = 0;
    ps->lock = osMutexNew(NULL);
    if (!ps->lock) {
        hal_mem_free(ps);
        osMutexRelease(manager->lock);
        return 0;
    }
    ps->handle = manager->next_handle++;

    manager->powers[manager->count++] = ps;
    PowerHandle handle = ps->handle;
    osMutexRelease(manager->lock);
    return handle;
}

// Get handle by name
PowerHandle power_manager_get_handle(PowerManager *manager, const char *name) 
{
    if (!manager || !name) return 0;
    osMutexAcquire(manager->lock, osWaitForever);
    for (size_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->powers[i]->name, name) == 0) {
            PowerHandle handle = manager->powers[i]->handle;
            osMutexRelease(manager->lock);
            return handle;
        }
    }
    osMutexRelease(manager->lock);
    return 0;
}

// Get PowerState pointer by handle
static PowerState* get_power_state_by_handle(PowerManager *manager, PowerHandle handle) 
{
    if (!manager || handle == 0) return NULL;
    osMutexAcquire(manager->lock, osWaitForever);
    for (size_t i = 0; i < manager->count; i++) {
        if (manager->powers[i]->handle == handle) {
            PowerState *ps = manager->powers[i];
            osMutexRelease(manager->lock);
            return ps;
        }
    }
    osMutexRelease(manager->lock);
    return NULL;
}

// Request power by handle
int power_manager_acquire_by_handle(PowerManager *manager, PowerHandle handle) 
{
    PowerState *ps = get_power_state_by_handle(manager, handle);
    if (!ps) return -1;
    osMutexAcquire(ps->lock, osWaitForever);
    ps->ref_count++;
    if (ps->ref_count == 1) {
        ps->power_on();
        ps->is_on = 1;
    }
    osMutexRelease(ps->lock);
    return 0;
}

// Release power by handle
int power_manager_release_by_handle(PowerManager *manager, PowerHandle handle) 
{
    PowerState *ps = get_power_state_by_handle(manager, handle);
    if (!ps) return -1;
    osMutexAcquire(ps->lock, osWaitForever);
    // if (ps->ref_count <= 0) {
    //     osMutexRelease(ps->lock);
    //     return -2;
    // }
    if (ps->ref_count > 0) ps->ref_count--;
    if (ps->ref_count == 0) {
        ps->power_off();
        ps->is_on = 0;
    }
    osMutexRelease(ps->lock);
    return 0;
}

// Get power state by handle
int power_manager_get_state_by_handle(PowerManager *manager, PowerHandle handle, int *is_on, int *ref_count) 
{
    PowerState *ps = get_power_state_by_handle(manager, handle);
    if (!ps) return -1;
    osMutexAcquire(ps->lock, osWaitForever);
    if (is_on) *is_on = ps->is_on;
    if (ref_count) *ref_count = ps->ref_count;
    osMutexRelease(ps->lock);
    return 0;
}

// Compatible with original interface: request and release by name
int power_manager_acquire(PowerManager *manager, const char *name) 
{
    PowerHandle handle = power_manager_get_handle(manager, name);
    if (!handle) return -1;
    return power_manager_acquire_by_handle(manager, handle);
}
int power_manager_release(PowerManager *manager, const char *name) 
{
    PowerHandle handle = power_manager_get_handle(manager, name);
    if (!handle) return -1;
    return power_manager_release_by_handle(manager, handle);
}
int power_manager_get_state(PowerManager *manager, const char *name, int *is_on, int *ref_count) 
{
    PowerHandle handle = power_manager_get_handle(manager, name);
    if (!handle) return -1;
    return power_manager_get_state_by_handle(manager, handle, is_on, ref_count);
}

#if 0
void gpu_power_on() { printf("GPU power ON\n"); }
void gpu_power_off() { printf("GPU power OFF\n"); }
void sensor_power_on() { printf("Sensor power ON\n"); }
void sensor_power_off() { printf("Sensor power OFF\n"); }

int main() {
    // Create power manager
    PowerManager *manager = power_manager_create();
    
    // Register power
    power_manager_register(manager, "GPU", gpu_power_on, gpu_power_off);
    power_manager_register(manager, "SENSOR", sensor_power_on, sensor_power_off);
    
    // Use power
    power_manager_acquire(manager, "GPU");
    power_manager_acquire(manager, "GPU");
    power_manager_acquire(manager, "SENSOR");
    
    // Get and print state
    int is_on, ref_count;
    power_manager_get_state(manager, "GPU", &is_on, &ref_count);
    printf("GPU state: %s, ref_count: %d\n", is_on ? "ON" : "OFF", ref_count);
    
    // Release power
    power_manager_release(manager, "GPU");
    power_manager_release(manager, "GPU"); // This will turn off GPU power
    power_manager_release(manager, "SENSOR");
    
    // Cleanup
    power_manager_destroy(manager);
    return 0;
}

#endif