/**
 * @file video_pipeline.c
 * @brief Video Pipeline Implementation
 * @details video pipeline with integrated flow manager and HAL support
 */

#include "video_pipeline.h"
#include "video_frame_mgr.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cmsis_os2.h"

#define VIDEO_THREAD_DELETE(handle) osThreadTerminate(handle)
#define VIDEO_MUTEX_CREATE() osMutexNew(NULL)
#define VIDEO_MUTEX_DESTROY(mutex) osMutexDelete(mutex)
#define VIDEO_MUTEX_LOCK(mutex) osMutexAcquire(mutex, osWaitForever)
#define VIDEO_MUTEX_UNLOCK(mutex) osMutexRelease(mutex)
#define VIDEO_SEM_CREATE(count) osSemaphoreNew(count, count, NULL)
#define VIDEO_SEM_DESTROY(sem) osSemaphoreDelete(sem)
#define VIDEO_SEM_WAIT(sem, timeout) osSemaphoreAcquire(sem, timeout)
#define VIDEO_SEM_POST(sem) osSemaphoreRelease(sem)
#define VIDEO_DELAY(ms) osDelay(ms)
/* Max wall-clock wait for one node thread to observe thread_active=FALSE and
 * exit. See video_pipeline_stop(). */
#define VIDEO_NODE_STOP_TIMEOUT_MS 3000U
static inline osThreadId_t video_thread_create(
    osThreadFunc_t func,
    void *arg,
    void *stack_mem,
    uint32_t stack_size,
    char *name,
    osPriority_t prio
) {
    osThreadAttr_t attr = {
        .stack_mem = stack_mem,
        .stack_size = stack_size,
		.name = name,
        .priority = prio,
        .attr_bits = 0
    };
    return osThreadNew(func, arg, &attr);
}





/* ==================== Global Variables ==================== */

static aicam_bool_t g_pipeline_system_initialized = AICAM_FALSE;
static video_pipeline_t *g_pipelines[8];  // Support up to 8 pipelines
static uint32_t g_pipeline_count = 0;
static uint32_t g_next_pipeline_id = 1;
static void *g_system_mutex = NULL;

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Get current timestamp in microseconds
 */
static uint64_t get_timestamp_us(void)
{
    // Use system tick to get real elapsed time
    uint32_t tick_count = osKernelGetTickCount();
    uint32_t tick_freq = osKernelGetTickFreq();
    // Convert ticks to microseconds: (tick_count * 1000000) / tick_freq
    return ((uint64_t)tick_count * 1000000ULL) / (uint64_t)tick_freq;
}

/**
 * @brief Initialize frame queue
 */
static aicam_result_t video_frame_queue_init(video_frame_queue_t *queue, uint32_t max_size)
{
    if (!queue) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memset(queue, 0, sizeof(video_frame_queue_t));
    queue->max_size = (max_size > VIDEO_FRAME_QUEUE_SIZE) ? VIDEO_FRAME_QUEUE_SIZE : max_size;
    
    queue->mutex = VIDEO_MUTEX_CREATE();
    queue->not_empty_sem = VIDEO_SEM_CREATE(0);
    queue->not_full_sem = VIDEO_SEM_CREATE(queue->max_size);
    
    if (!queue->mutex || !queue->not_empty_sem || !queue->not_full_sem) {
        LOG_CORE_ERROR("Failed to create frame queue synchronization objects");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    return AICAM_OK;
}

/**
 * @brief Deinitialize frame queue
 */
static void video_frame_queue_deinit(video_frame_queue_t *queue)
{
    if (!queue) return;
    
    VIDEO_MUTEX_DESTROY(queue->mutex);
    VIDEO_SEM_DESTROY(queue->not_empty_sem);
    VIDEO_SEM_DESTROY(queue->not_full_sem);
    
    memset(queue, 0, sizeof(video_frame_queue_t));
}

/**
 * @brief Push frame to queue (blocking)
 */
static aicam_result_t video_frame_queue_push(video_frame_queue_t *queue, video_frame_t *frame, uint32_t timeout_ms)
{
    if (!queue || !frame) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Wait for space in queue using semaphore
    if (VIDEO_SEM_WAIT(queue->not_full_sem, timeout_ms) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    VIDEO_MUTEX_LOCK(queue->mutex);
    
    // Add frame to queue (semaphore already guaranteed space)
    queue->frames[queue->tail] = frame;
    queue->tail = (queue->tail + 1) % queue->max_size;
    queue->count++;
    
    VIDEO_MUTEX_UNLOCK(queue->mutex);
    
    // Signal that queue is not empty
    VIDEO_SEM_POST(queue->not_empty_sem);
    
    return AICAM_OK;
}

/**
 * @brief Pop frame from queue (blocking)
 */
static aicam_result_t video_frame_queue_pop(video_frame_queue_t *queue, video_frame_t **frame, uint32_t timeout_ms)
{
    if (!queue || !frame) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Wait for frame in queue using semaphore
    if (VIDEO_SEM_WAIT(queue->not_empty_sem, timeout_ms) != osOK) {
        return AICAM_ERROR_TIMEOUT;
    }
    
    VIDEO_MUTEX_LOCK(queue->mutex);
    
    // Get frame from queue (semaphore already guaranteed data)
    *frame = queue->frames[queue->head];
    queue->frames[queue->head] = NULL;
    queue->head = (queue->head + 1) % queue->max_size;
    queue->count--;
    
    VIDEO_MUTEX_UNLOCK(queue->mutex);
    
    // Signal that queue is not full
    VIDEO_SEM_POST(queue->not_full_sem);
    
    return AICAM_OK;
}




/* ==================== Unified Node Processing Thread ==================== */

/**
 * @brief Unified node processing thread for all node types
 */
void video_node_processing_thread(void *argument)
{
    video_node_t *node = (video_node_t*)argument;
    if (!node) {
        LOG_CORE_ERROR("Invalid node context in processing thread");
        return;
    }
    
    LOG_CORE_INFO("Starting processing thread for node: %s (type: %d), node->thread_active: %d", 
                 node->config.name, node->config.type, node->thread_active);
    while (1) {
        video_frame_t *input_frames[4] = {0}; // Support up to 4 input frames
        video_frame_t *output_frames[4] = {0}; // Support up to 4 output frames
        uint32_t input_count = 0;
        uint32_t output_count = 0;
        
        // Update state to waiting
        node->stats.current_state = NODE_EXEC_WAITING;
        
        // For source nodes, we don't wait for input frames - they generate their own
        if (node->config.type == VIDEO_NODE_TYPE_SOURCE) {
            // Source nodes generate frames internally via process callback
            input_count = 0;
        } else {
            // Non-source nodes get input frames directly from upstream nodes' output queues
            input_count = 0;
            
            // Find the upstream connection and get frame from its output queue
            for (uint32_t i = 0; i < node->pipeline->connection_count; i++) {
                video_connection_t *conn = &node->pipeline->connections[i];
                if (!conn->is_active || conn->sink_node != node) {
                    continue;
                }
                
                video_node_t *source_node = conn->source_node;
                if (!source_node) {
                    continue;
                }
                
                // Try to get frame from source node's output queue (blocking)
                video_frame_t *frame = NULL;
                aicam_result_t result = video_frame_queue_pop(&source_node->output_queue, &frame, 0); // No timeout
                if (result == AICAM_OK && frame) {
                    input_frames[input_count] = frame;
                    input_count++;
                
                    
                    // Update connection statistics
                    conn->frames_transferred++;
                    conn->bytes_transferred += frame->info.size;
                    
                    break; // Get one frame for now
                }
            }
            
        }

        if (input_count == 0) {
            if(node->thread_active) {
                if(node->config.type != VIDEO_NODE_TYPE_SOURCE ) {
                    VIDEO_DELAY(1); // 1ms delay
                    continue;
                 }
            }
            else {
                //if thread is not active, exit the thread
                printf("Node %s thread is not active, exit the thread\r\n", node->config.name);
                goto exit_thread;
            }
        }
        
        // Update state to processing
        node->stats.current_state = NODE_EXEC_PROCESSING;
        uint64_t start_time = get_timestamp_us();
        
        // Call node's process callback
        aicam_result_t result = node->config.callbacks.process(
                node,
                input_frames, input_count,
                output_frames, &output_count
            );
        
        
        uint64_t end_time = get_timestamp_us();
        uint64_t process_time = end_time - start_time;
        
        // Update statistics
        if(result == AICAM_OK) {
            node->stats.frames_processed++;
            if (process_time > node->stats.max_processing_time_us) {
                node->stats.max_processing_time_us = process_time;
            }
        }
        
        // Update average processing time
        if (node->stats.frames_processed == 1) {
            node->stats.avg_processing_time_us = process_time;
        } else {
            node->stats.avg_processing_time_us = 
                (node->stats.avg_processing_time_us * 9 + process_time) / 10;
        }
        
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Node %s processing failed: %d", 
                          node->config.name, result);
            node->stats.current_state = NODE_EXEC_ERROR;
            
            // Release input frames if auto-release is enabled
            if (node->auto_release_input) {
                for (uint32_t i = 0; i < input_count; i++) {
                    if (input_frames[i]) {
                        video_frame_unref(input_frames[i]); // Use ref counting
                    }
                }
            }
            continue;
        }
        
        // Push output frames to output queue 
        for (uint32_t i = 0; i < output_count; i++) {
            if (output_frames[i]) {
                result = video_frame_queue_push(&node->output_queue, output_frames[i], 0); // No timeout
                if (result != AICAM_OK) {
                    LOG_CORE_WARN("Failed to push output frame %u for node %s: %d", 
                                 i, node->config.name, result);
                    node->stats.frames_dropped++;
                    node->stats.queue_overflows++;
                    video_frame_unref(output_frames[i]); // Release dropped frame
                }
            }
        }
        
        // Release input frames if auto-release is enabled
        if (node->auto_release_input) {
            for (uint32_t i = 0; i < input_count; i++) {
                if (input_frames[i]) {
                    video_frame_unref(input_frames[i]); // Use ref counting
                }
            }
        }
        
        node->stats.current_state = NODE_EXEC_IDLE;
        node->last_process_time = end_time;
        
    }

exit_thread:
    LOG_CORE_INFO("Processing thread for node %s terminated", node->config.name);
    node->thread_exited = AICAM_TRUE;
    osThreadExit();
    return;
}

/* ==================== System Management ==================== */

aicam_result_t video_pipeline_system_init(void)
{
    if (g_pipeline_system_initialized) {
        return AICAM_OK;
    }
    
    // Initialize global state
    memset(g_pipelines, 0, sizeof(g_pipelines));
    g_pipeline_count = 0;
    g_next_pipeline_id = 1;
    
    // Create system mutex
    g_system_mutex = VIDEO_MUTEX_CREATE();
    if (!g_system_mutex) {
        LOG_CORE_ERROR("Failed to create system mutex");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    g_pipeline_system_initialized = AICAM_TRUE;
    
    LOG_CORE_INFO("Video pipeline system initialized");
    return AICAM_OK;
}

aicam_result_t video_pipeline_system_deinit(void)
{
    if (!g_pipeline_system_initialized) {
        return AICAM_OK;
    }
    
    VIDEO_MUTEX_LOCK(g_system_mutex);
    
    // Destroy all active pipelines
    for (uint32_t i = 0; i < g_pipeline_count; i++) {
        if (g_pipelines[i]) {
            video_pipeline_destroy(g_pipelines[i]);
        }
    }
    
    VIDEO_MUTEX_UNLOCK(g_system_mutex);
    
    // Cleanup system resources
    VIDEO_MUTEX_DESTROY(g_system_mutex);
    g_system_mutex = NULL;
    g_pipeline_system_initialized = AICAM_FALSE;
    
    LOG_CORE_INFO("Video pipeline system deinitialized");
    return AICAM_OK;
}

/* ==================== Pipeline Management ==================== */

aicam_result_t video_pipeline_create(const video_pipeline_config_t *config, video_pipeline_t **pipeline)
{
    if (!config || !pipeline) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!g_pipeline_system_initialized) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    VIDEO_MUTEX_LOCK(g_system_mutex);
    
    if (g_pipeline_count >= 8) {
        VIDEO_MUTEX_UNLOCK(g_system_mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Allocate pipeline
    video_pipeline_t *new_pipeline = buffer_calloc(1, sizeof(video_pipeline_t));
    if (!new_pipeline) {
        VIDEO_MUTEX_UNLOCK(g_system_mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    memset(new_pipeline, 0, sizeof(video_pipeline_t));
    
    // Initialize pipeline
    new_pipeline->pipeline_id = g_next_pipeline_id++;
    memcpy(&new_pipeline->config, config, sizeof(video_pipeline_config_t));
    new_pipeline->state = VIDEO_PIPELINE_STATE_IDLE;
    new_pipeline->next_node_id = 1;
    new_pipeline->next_connection_id = 1;
    new_pipeline->start_time = get_timestamp_us();
    
    // Create pipeline mutex
    new_pipeline->mutex = VIDEO_MUTEX_CREATE();
    if (!new_pipeline->mutex) {
        buffer_free(new_pipeline);
        VIDEO_MUTEX_UNLOCK(g_system_mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Add to global pipeline list
    g_pipelines[g_pipeline_count++] = new_pipeline;
    
    *pipeline = new_pipeline;
    
    VIDEO_MUTEX_UNLOCK(g_system_mutex);
    
    LOG_CORE_INFO("Created video pipeline '%s' (ID: %u)", config->name, new_pipeline->pipeline_id);
    return AICAM_OK;
}

aicam_result_t video_pipeline_destroy(video_pipeline_t *pipeline)
{
    if (!pipeline) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Stop pipeline if running (this already cleans up threads)
    if (pipeline->is_running) {
        video_pipeline_stop(pipeline);
    }
    
    VIDEO_MUTEX_LOCK(pipeline->mutex);
    
    // Cleanup all nodes (threads already stopped by video_pipeline_stop)
    for (uint32_t i = 0; i < pipeline->node_count; i++) {
        if (pipeline->nodes[i]) {
            video_node_t *node = pipeline->nodes[i];
            
            // Deinitialize node
            if (node->config.callbacks.deinit) {
                node->config.callbacks.deinit(node);
            }
            
            // Cleanup queues
            video_frame_queue_deinit(&node->output_queue);
            
            video_node_destroy(node);
        }
    }
    
    
    VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    
    // Remove from global list
    VIDEO_MUTEX_LOCK(g_system_mutex);
    for (uint32_t i = 0; i < g_pipeline_count; i++) {
        if (g_pipelines[i] == pipeline) {
            // Shift remaining pipelines
            for (uint32_t j = i; j < g_pipeline_count - 1; j++) {
                g_pipelines[j] = g_pipelines[j + 1];
            }
            g_pipeline_count--;
            break;
        }
    }
    VIDEO_MUTEX_UNLOCK(g_system_mutex);
    
    // Cleanup pipeline resources
    VIDEO_MUTEX_DESTROY(pipeline->mutex);
    buffer_free(pipeline);
    
    LOG_CORE_INFO("Destroyed video pipeline");
    return AICAM_OK;
}

aicam_result_t video_pipeline_start(video_pipeline_t *pipeline)
{
    if (!pipeline) {
        LOG_CORE_ERROR("Invalid parameters for start pipeline");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (pipeline->is_running) {
        LOG_CORE_WARN("Pipeline is already running");
        return AICAM_OK; // Already running
    }
    
    VIDEO_MUTEX_LOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Starting video pipeline '%s' with %u nodes", 
                 pipeline->config.name, pipeline->node_count);
    
    // Start all node processing threads
    for (int i = pipeline->node_count - 1; i >= 0; i--) {
        video_node_t *node = pipeline->nodes[i];
        if (!node) continue;
        
        node->thread_active = AICAM_TRUE;
        node->state = VIDEO_NODE_STATE_RUNNING;


        // Initialize node if callback provided
        // LOG_CORE_INFO("Register node init callback");
        // if (node->config.callbacks.init) {
        //     LOG_CORE_INFO("enter Register node init callback");
        //     aicam_result_t result = node->config.callbacks.init(node);
        //     if (result != AICAM_OK) {
        //         LOG_CORE_ERROR("Failed to initialize node: %d", result);
        //         VIDEO_MUTEX_UNLOCK(pipeline->mutex);
        //         return result;
        //     }
        // }
        
        // All nodes use the same unified processing thread
        node->stack_memory = buffer_calloc_ex(1, VIDEO_THREAD_STACK_SIZE, BUFFER_MEMORY_TYPE_RAM);
        if (!node->stack_memory) {
            LOG_CORE_ERROR("Failed to create stack memory for node %s", node->config.name);
            VIDEO_MUTEX_UNLOCK(pipeline->mutex);
            return AICAM_ERROR_NO_MEMORY;
        }
     

        // add retry count
        int retry_count = 0;
        while (retry_count < 3) {   
            node->thread_handle = video_thread_create(
                video_node_processing_thread,
                node,
                node->stack_memory,
                VIDEO_THREAD_STACK_SIZE,
                node->config.name,
                node->thread_priority
            );
            if (node->thread_handle) {
                break;
            }
            osDelay(100);
            retry_count++;
        }

        node->thread_exited = AICAM_FALSE;
        
        if (!node->thread_handle) {
            LOG_CORE_ERROR("Failed to create processing thread for node %s", 
                          node->config.name);
            VIDEO_MUTEX_UNLOCK(pipeline->mutex);
            return AICAM_ERROR_NO_MEMORY;
        }
        
        
        //LOG_CORE_INFO("Started processing thread for node: %s", node->config.name);
    }
    
    pipeline->is_running = AICAM_TRUE;
    pipeline->state = VIDEO_PIPELINE_STATE_RUNNING;
    pipeline->start_time = get_timestamp_us();
    
    VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Video pipeline started successfully");
    return AICAM_OK;
}

aicam_result_t video_pipeline_stop(video_pipeline_t *pipeline)
{
    if (!pipeline) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (!pipeline->is_running) {
        return AICAM_OK; // Already stopped
    }
    
    VIDEO_MUTEX_LOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Stopping video pipeline '%s'", pipeline->config.name);
    
    // Signal all threads to stop
    for (uint32_t i = 0; i < pipeline->node_count; i++) {
        video_node_t *node = pipeline->nodes[i];
        if (node) {
            node->thread_active = AICAM_FALSE;
            node->state = VIDEO_NODE_STATE_STOPPING;
        }

        /* ponytail: bounded wait. A node's process() callback (camera capture,
         * encoder, AI inference) can block indefinitely; without this ceiling the
         * HTTP task wedges here holding pipeline->mutex forever. If the thread
         * doesn't exit in time, osThreadTerminate() below force-kills it (best
         * effort — any RTOS/driver lock it holds stays held, so a later start may
         * still stall on the same resource; strictly better than hanging stop).
         * Signed-diff comparison handles uint32 tick wrap-around. */
        uint32_t stop_deadline = osKernelGetTickCount() + VIDEO_NODE_STOP_TIMEOUT_MS;
        while (node && !node->thread_exited &&
               (int32_t)(osKernelGetTickCount() - stop_deadline) < 0) {
            osDelay(100);
        }
        if (node && !node->thread_exited) {
            LOG_CORE_ERROR("Node %s did not exit within %ums, force-terminating",
                           node->config.name, VIDEO_NODE_STOP_TIMEOUT_MS);
        } else if (node) {
            LOG_CORE_INFO("Thread for node %s exited", node->config.name);
        }
    }

    
    //delete all threads (force-terminate any stragglers the bounded wait gave up on)
    for (uint32_t i = 0; i < pipeline->node_count; i++) {
        video_node_t *node = pipeline->nodes[i];
        if (!node) continue;

        if (node->thread_handle) {
            LOG_CORE_INFO("Deleting thread for node %s", node->config.name);
            osStatus_t status = osThreadTerminate(node->thread_handle);
            if (status == osOK) {
                LOG_CORE_INFO("Thread for node %s deleted", node->config.name);
                node->thread_handle = NULL;
            }
        }

        if (node->stack_memory) {
            buffer_free(node->stack_memory);
            node->stack_memory = NULL;
        }
        node->state = VIDEO_NODE_STATE_IDLE;
    }

    //call deinit callback for all nodes
    // for (uint32_t i = 0; i < pipeline->node_count; i++) {
    //     video_node_t *node = pipeline->nodes[i];
    //     if (node && node->config.callbacks.deinit) {
    //         node->config.callbacks.deinit(node);
    //     }
    // }

    pipeline->is_running = AICAM_FALSE;
    pipeline->state = VIDEO_PIPELINE_STATE_IDLE;
    
    VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Video pipeline stopped");
    return AICAM_OK;
}

/* ==================== Node Management ==================== */

aicam_result_t video_pipeline_register_node(video_pipeline_t *pipeline,
                                           video_node_t *standalone_node,
                                           uint32_t *node_id)
{
    if (!pipeline || !standalone_node || !node_id) {
        LOG_CORE_ERROR("Invalid parameters for register_node");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Check if pipeline is already running
    if (pipeline->state == VIDEO_PIPELINE_STATE_RUNNING) {
        LOG_CORE_ERROR("Cannot register node to running pipeline");
        return AICAM_ERROR;
    }
    
    // Check if pipeline has space for more nodes
    if (pipeline->node_count >= VIDEO_PIPELINE_MAX_NODES) {
        LOG_CORE_ERROR("Pipeline is full, cannot register more nodes");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Check if standalone node is already registered to a pipeline
    if (standalone_node->pipeline != NULL) {
        LOG_CORE_ERROR("Node is already registered to a pipeline");
        return AICAM_ERROR;
    }
    
    // Lock pipeline mutex
    VIDEO_MUTEX_LOCK(pipeline->mutex);

    //call node init callback
    if (standalone_node->config.callbacks.init) {
        standalone_node->config.callbacks.init(standalone_node);
    }
    
    // Assign node ID and link to pipeline
    standalone_node->node_id = pipeline->next_node_id++;
    standalone_node->pipeline = pipeline;
    
    // Update node state to ready
    standalone_node->state = VIDEO_NODE_STATE_READY;
    
    // Add node to pipeline's node list
    pipeline->nodes[pipeline->node_count++] = standalone_node;
    
    // Return node ID
    *node_id = standalone_node->node_id;
    
    // Unlock pipeline mutex
    VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Registered standalone node '%s' to pipeline (ID: %u)", 
                 standalone_node->config.name, standalone_node->node_id);
    
    return AICAM_OK;
}


/* ==================== Connection Management ==================== */

aicam_result_t video_pipeline_connect_nodes(video_pipeline_t *pipeline,
                                           uint32_t source_node_id, uint32_t source_port,
                                           uint32_t sink_node_id, uint32_t sink_port)
{
    if (!pipeline) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    VIDEO_MUTEX_LOCK(pipeline->mutex);
    
    if (pipeline->connection_count >= VIDEO_PIPELINE_MAX_CONNECTIONS) {
        VIDEO_MUTEX_UNLOCK(pipeline->mutex);
        return AICAM_ERROR_NO_MEMORY;
    }
    
    // Find source and sink nodes
    video_node_t *source_node = NULL, *sink_node = NULL;
    for (uint32_t i = 0; i < pipeline->node_count; i++) {
        if (pipeline->nodes[i]->node_id == source_node_id) {
            source_node = pipeline->nodes[i];
        }
        if (pipeline->nodes[i]->node_id == sink_node_id) {
            sink_node = pipeline->nodes[i];
        }
    }
    
    if (!source_node || !sink_node) {
        VIDEO_MUTEX_UNLOCK(pipeline->mutex);
        return AICAM_ERROR_NOT_FOUND;
    }
    
    // Create connection
    video_connection_t *conn = &pipeline->connections[pipeline->connection_count];
    memset(conn, 0, sizeof(video_connection_t));
    
    conn->connection_id = pipeline->next_connection_id++;
    conn->source_node = source_node;
    conn->source_port = source_port;
    conn->sink_node = sink_node;
    conn->sink_port = sink_port;
    conn->format = VIDEO_FORMAT_UNKNOWN; // Auto-detect
    conn->is_active = AICAM_TRUE;
    
    pipeline->connection_count++;
    
    VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    
    LOG_CORE_INFO("Connected nodes: %s[%u] -> %s[%u]", 
                 source_node->config.name, source_port,
                 sink_node->config.name, sink_port);
    
    return AICAM_OK;
}

/* ==================== frame management ==================== */

video_node_t* video_pipeline_find_node(video_pipeline_t *pipeline, const char *name)
{
    if (!pipeline || !name) {
        return NULL;
    }

    for (uint32_t i = 0; i < pipeline->node_count; i++) {
        if (pipeline->nodes[i] && strcmp(pipeline->nodes[i]->config.name, name) == 0) {
            return pipeline->nodes[i];
        }
    }
    return NULL;
}

aicam_result_t video_pipeline_push_frame(video_pipeline_t *pipeline,
                                        const char *node_name,
                                        video_frame_t *frame)
{
    if (!pipeline || !frame) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    video_node_t *node = video_pipeline_find_node(pipeline, node_name);
    if (!node) {
        return AICAM_ERROR_NOT_FOUND;
    }

    return video_frame_queue_push(&node->output_queue, frame, 0);
}

aicam_result_t video_pipeline_pull_frame(video_pipeline_t *pipeline,
                                        const char *node_name,
                                        video_frame_t **frame)
{
    if (!pipeline || !frame) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    video_node_t *node = video_pipeline_find_node(pipeline, node_name);
    if (!node) {
        return AICAM_ERROR_NOT_FOUND;
    }

    //Note: 
    // 1. This function is used to pull a frame from the output queue of a node.
    // 2. The frame is not removed from the queue, but the pointer to the frame is returned.
    // 3. The frame is not copied, but the pointer to the frame is returned.
    // 4. The frame is not released, but the pointer to the frame is returned.
    return video_frame_queue_pop(&node->output_queue, frame, 0);
}

/* ==================== Statistics ==================== */

aicam_result_t video_node_get_stats(video_node_t *node, video_node_stats_t *stats)
{
    if (!node || !stats) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(stats, &node->stats, sizeof(video_node_stats_t));
    
    // Update current queue depths
    stats->current_queue_depth = node->output_queue.count;
    if (node->output_queue.count > stats->max_queue_depth) {
        stats->max_queue_depth = node->output_queue.count;
    }
    
    return AICAM_OK;
}

aicam_result_t video_pipeline_get_stats(video_pipeline_t *pipeline,
                                       float *total_fps,
                                       uint64_t *total_frames)
{
    if (!pipeline) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (total_fps) {
        *total_fps = pipeline->current_fps;
    }
    
    if (total_frames) {
        *total_frames = pipeline->total_frames_processed;
    }
    
    return AICAM_OK;
}

/* ==================== Node Creation and Management ==================== */

/**
 * @brief Create a standalone video node
 * @param name Node name
 * @param type Node type
 * @return Node handle or NULL on failure
 */
video_node_t* video_node_create(const char *name, video_node_type_t type)
{
    if (!name) {
        LOG_CORE_ERROR("Invalid node name");
        return NULL;
    }
    
    // Allocate node
    video_node_t *node = buffer_calloc(1, sizeof(video_node_t));
    if (!node) {
        LOG_CORE_ERROR("Failed to allocate node memory");
        return NULL;
    }
    
    memset(node, 0, sizeof(video_node_t));
    
    // Initialize basic node structure
    strncpy(node->config.name, name, VIDEO_PIPELINE_NODE_NAME_LEN - 1);
    node->config.name[VIDEO_PIPELINE_NODE_NAME_LEN - 1] = '\0';
    node->config.type = type;
    node->config.max_input_count = 1;
    node->config.max_output_count = 1;
    node->config.thread_safe = AICAM_TRUE;
    
    // Set default state
    node->state = VIDEO_NODE_STATE_IDLE;
    node->thread_priority = VIDEO_THREAD_PRIORITY;
    node->flow_mode = FLOW_MODE_PUSH;
    node->auto_release_input = AICAM_TRUE;
    node->zero_copy_mode = AICAM_FALSE;
    node->max_output_queue_size = VIDEO_FRAME_QUEUE_SIZE;
    node->processing_timeout_ms = 1000;
    
    // Initialize output queue
    aicam_result_t result = video_frame_queue_init(&node->output_queue, node->max_output_queue_size);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize output queue: %d", result);
        buffer_free(node);
        return NULL;
    }
    
    LOG_CORE_INFO("Created standalone node: %s (type: %d)", name, type);
    return node;
}

/**
 * @brief Destroy a standalone video node
 * @param node Node to destroy
 * @return AICAM_OK on success
 */
aicam_result_t video_node_destroy(video_node_t *node)
{
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Stop node if running
    if (node->state == VIDEO_NODE_STATE_RUNNING) {
        //video_node_control(node, VIDEO_NODE_CMD_STOP, NULL);
    }
    
    // Call deinit callback if available
    if (node->config.callbacks.deinit) {
        node->config.callbacks.deinit(node);
    }
    
    // Clean up output queue
    video_frame_queue_deinit(&node->output_queue);
    
    // Free private data
    if (node->private_data) {
        buffer_free(node->private_data); 
        node->private_data = NULL;
    }
    
    LOG_CORE_INFO("Destroyed node: %s", node->config.name);
    buffer_free(node);
    
    return AICAM_OK;
}

/**
 * @brief Set node callbacks
 * @param node Node handle
 * @param callbacks Callback functions
 * @return AICAM_OK on success
 */
aicam_result_t video_node_set_callbacks(video_node_t *node, const video_node_callbacks_t *callbacks)
{
    if (!node || !callbacks) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    memcpy(&node->config.callbacks, callbacks, sizeof(video_node_callbacks_t));
    
    LOG_CORE_DEBUG("Set callbacks for node: %s", node->config.name);
    return AICAM_OK;
}

/**
 * @brief Set node private data
 * @param node Node handle
 * @param data Private data pointer
 * @return AICAM_OK on success
 */
aicam_result_t video_node_set_private_data(video_node_t *node, void *data)
{
    if (!node) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    node->private_data = data;
    
    LOG_CORE_DEBUG("Set private data for node: %s", node->config.name);
    return AICAM_OK;
}

/**
 * @brief Get node private data
 * @param node Node handle
 * @return Private data pointer
 */
void* video_node_get_private_data(video_node_t *node)
{
    if (!node) {
        return NULL;
    }
    
    return node->private_data;
}

/**
 * @brief Set node configuration
 * @param node Node handle
 * @param config New configuration
 * @return AICAM_OK on success
 */
aicam_result_t video_node_set_config(video_node_t *node, const video_node_config_t *config)
{
    if (!node || !config) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Update configuration
    memcpy(&node->config, config, sizeof(video_node_config_t));
    
    
    if (config->max_output_count != node->max_output_queue_size) {
        video_frame_queue_deinit(&node->output_queue);
        node->max_output_queue_size = config->max_output_count;
        aicam_result_t result = video_frame_queue_init(&node->output_queue, node->max_output_queue_size);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to reinitialize output queue: %d", result);
            return result;
        }

    }
    
    LOG_CORE_INFO("Updated configuration for node: %s", node->config.name);
    return AICAM_OK;
}

/* ==================== Video Pipeline Status Command ==================== */

/**
 * @brief Get node type name string
 */
static const char* get_node_type_name(video_node_type_t type)
{
    switch (type) {
        case VIDEO_NODE_TYPE_SOURCE:   return "SOURCE";
        case VIDEO_NODE_TYPE_SINK:     return "SINK";
        case VIDEO_NODE_TYPE_FILTER:   return "FILTER";
        case VIDEO_NODE_TYPE_ENCODER:  return "ENCODER";
        case VIDEO_NODE_TYPE_DECODER: return "DECODER";
        case VIDEO_NODE_TYPE_ANALYZER: return "ANALYZER";
        case VIDEO_NODE_TYPE_MIXER:    return "MIXER";
        case VIDEO_NODE_TYPE_SPLITTER: return "SPLITTER";
        case VIDEO_NODE_TYPE_CUSTOM:   return "CUSTOM";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Get node state name string
 */
static const char* get_node_state_name(video_node_state_t state)
{
    switch (state) {
        case VIDEO_NODE_STATE_IDLE:    return "IDLE";
        case VIDEO_NODE_STATE_READY:   return "READY";
        case VIDEO_NODE_STATE_RUNNING: return "RUNNING";
        case VIDEO_NODE_STATE_PAUSED:  return "PAUSED";
        case VIDEO_NODE_STATE_STOPPING: return "STOPPING";
        case VIDEO_NODE_STATE_ERROR:   return "ERROR";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Get node execution state name string
 */
static const char* get_node_exec_state_name(node_exec_state_t state)
{
    switch (state) {
        case NODE_EXEC_IDLE:       return "IDLE";
        case NODE_EXEC_WAITING:    return "WAITING";
        case NODE_EXEC_PROCESSING: return "PROCESSING";
        case NODE_EXEC_BLOCKED:    return "BLOCKED";
        case NODE_EXEC_ERROR:      return "ERROR";
        default:                   return "UNKNOWN";
    }
}

/**
 * @brief Get pipeline state name string
 */
static const char* get_pipeline_state_name(video_pipeline_state_t state)
{
    switch (state) {
        case VIDEO_PIPELINE_STATE_IDLE:    return "IDLE";
        case VIDEO_PIPELINE_STATE_READY:   return "READY";
        case VIDEO_PIPELINE_STATE_RUNNING: return "RUNNING";
        case VIDEO_PIPELINE_STATE_PAUSED:  return "PAUSED";
        case VIDEO_PIPELINE_STATE_STOPPING: return "STOPPING";
        case VIDEO_PIPELINE_STATE_ERROR:   return "ERROR";
        default:                           return "UNKNOWN";
    }
}

/**
 * @brief Display video pipeline status
 */
static void video_pipeline_display_status(void)
{
    if (!g_pipeline_system_initialized) {
        printf("Video pipeline system not initialized\r\n");
        return;
    }
    
    VIDEO_MUTEX_LOCK(g_system_mutex);
    
    printf("\r\n========== VIDEO PIPELINE STATUS ==========\r\n");
    printf("Total Pipelines: %lu\r\n", (unsigned long)g_pipeline_count);
    printf("\r\n");
    
    if (g_pipeline_count == 0) {
        printf("No active pipelines\r\n");
        VIDEO_MUTEX_UNLOCK(g_system_mutex);
        printf("==========================================\r\n\r\n");
        return;
    }
    
    for (uint32_t p = 0; p < g_pipeline_count; p++) {
        video_pipeline_t *pipeline = g_pipelines[p];
        if (!pipeline) continue;
        
        VIDEO_MUTEX_LOCK(pipeline->mutex);
        
        printf("--- Pipeline: %s (ID: %lu) ---\r\n", 
               pipeline->config.name, (unsigned long)pipeline->pipeline_id);
        printf("  State: %s\r\n", get_pipeline_state_name(pipeline->state));
        printf("  Running: %s\r\n", pipeline->is_running ? "YES" : "NO");
        printf("  Nodes: %lu/%lu\r\n", 
               (unsigned long)pipeline->node_count, 
               (unsigned long)VIDEO_PIPELINE_MAX_NODES);
        printf("  Connections: %lu/%lu\r\n", 
               (unsigned long)pipeline->connection_count,
               (unsigned long)VIDEO_PIPELINE_MAX_CONNECTIONS);
        
        // Calculate pipeline FPS and total frames from node statistics
        uint64_t min_frames_processed = UINT64_MAX;
        uint64_t max_frames_processed = 0;
        float pipeline_fps = 0.0f;
        
        if (pipeline->node_count > 0) {
            for (uint32_t n = 0; n < pipeline->node_count; n++) {
                video_node_t *node = pipeline->nodes[n];
                if (!node) continue;
                
                video_node_stats_t node_stats;
                video_node_get_stats(node, &node_stats);
                
                if (node_stats.frames_processed < min_frames_processed) {
                    min_frames_processed = node_stats.frames_processed;
                }
                if (node_stats.frames_processed > max_frames_processed) {
                    max_frames_processed = node_stats.frames_processed;
                }
            }
            
            // Pipeline total frames = minimum frames processed (bottleneck node)
            // Pipeline FPS = based on bottleneck node
            // Only calculate after minimum runtime to avoid initial spike
            if (min_frames_processed != UINT64_MAX && pipeline->is_running) {
                uint64_t runtime_ms = (get_timestamp_us() - pipeline->start_time) / 1000;
                const uint32_t MIN_RUNTIME_MS = 2000;  // Minimum 2 second for accurate FPS
                if (runtime_ms >= MIN_RUNTIME_MS) {
                    pipeline_fps = (min_frames_processed * 1000.0f) / runtime_ms;
                }
            }
        }
        
        printf("  FPS: %.2f\r\n", pipeline_fps);
        printf("  Total Frames: %lu\r\n", 
               min_frames_processed != UINT64_MAX ? (unsigned long)min_frames_processed : 0);
        
        if (pipeline->node_count > 0) {
            printf("\r\n  Nodes:\r\n");
            for (uint32_t n = 0; n < pipeline->node_count; n++) {
                video_node_t *node = pipeline->nodes[n];
                if (!node) continue;
                
                video_node_stats_t stats;
                video_node_get_stats(node, &stats);
                
                printf("    [%lu] %s (%s)\r\n", 
                       (unsigned long)node->node_id,
                       node->config.name,
                       get_node_type_name(node->config.type));
                printf("      State: %s | Exec: %s\r\n",
                       get_node_state_name(node->state),
                       get_node_exec_state_name(stats.current_state));
                printf("      Thread: %s | Active: %s\r\n",
                       node->thread_handle ? "RUNNING" : "STOPPED",
                       node->thread_active ? "YES" : "NO");
                printf("      Frames: Processed=%lu, Dropped=%lu\r\n",
                       (unsigned long)stats.frames_processed,
                       (unsigned long)stats.frames_dropped);
                printf("      Queue: Current=%lu/%lu, Max=%lu, Overflows=%lu\r\n",
                       (unsigned long)stats.current_queue_depth,
                       (unsigned long)node->max_output_queue_size,
                       (unsigned long)stats.max_queue_depth,
                       (unsigned long)stats.queue_overflows);
                printf("      Processing Time: Avg=%.2f ms, Max=%.2f ms\r\n",
                       stats.avg_processing_time_us / 1000.0f,
                       stats.max_processing_time_us / 1000.0f);
                
                // Calculate current FPS for this node
                // Only calculate after minimum runtime to avoid initial spike
                if (stats.frames_processed > 0 && pipeline->is_running) {
                    uint64_t runtime_ms = (get_timestamp_us() - pipeline->start_time) / 1000;
                    const uint32_t MIN_RUNTIME_MS = 2000;  // Minimum 2 second for accurate FPS
                    
                    if (runtime_ms >= MIN_RUNTIME_MS) {
                        float node_fps = (stats.frames_processed * 1000.0f) / runtime_ms;
                        printf("      Node FPS: %.2f\r\n", node_fps);
                    } else {
                        // Initial phase: show progress instead of inaccurate FPS
                        printf("      Node FPS: calculating... (%lu ms, %lu frames)\r\n", 
                               (unsigned long)runtime_ms,
                               (unsigned long)stats.frames_processed);
                    }
                }
                printf("\r\n");
            }
        }
        
        if (pipeline->connection_count > 0) {
            printf("  Connections:\r\n");
            for (uint32_t c = 0; c < pipeline->connection_count; c++) {
                video_connection_t *conn = &pipeline->connections[c];
                if (!conn->is_active) continue;
                
                printf("    [%lu] %s[%lu] -> %s[%lu]\r\n",
                       (unsigned long)conn->connection_id,
                       conn->source_node ? conn->source_node->config.name : "NULL",
                       (unsigned long)conn->source_port,
                       conn->sink_node ? conn->sink_node->config.name : "NULL",
                       (unsigned long)conn->sink_port);
                printf("      Frames: %lu, Bytes: %lu, Overruns: %lu\r\n",
                       (unsigned long)conn->frames_transferred,
                       (unsigned long)conn->bytes_transferred,
                       (unsigned long)conn->queue_overruns);
            }
            printf("\r\n");
        }
        
        VIDEO_MUTEX_UNLOCK(pipeline->mutex);
    }
    
    VIDEO_MUTEX_UNLOCK(g_system_mutex);
    printf("==========================================\r\n\r\n");
}

/**
 * @brief Command handler for video pipeline status
 */
static int video_pipeline_status_cmd(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    video_pipeline_display_status();
    return 0;
}

/**
 * @brief Register video pipeline commands
 */
void video_pipeline_register_commands(void)
{
    static const debug_cmd_reg_t video_pipeline_cmd_table[] = {
        {"vstatus", "Display video pipeline status", video_pipeline_status_cmd},
    };
    
    debug_register_commands(video_pipeline_cmd_table, 
                            sizeof(video_pipeline_cmd_table) / sizeof(video_pipeline_cmd_table[0]));
}

