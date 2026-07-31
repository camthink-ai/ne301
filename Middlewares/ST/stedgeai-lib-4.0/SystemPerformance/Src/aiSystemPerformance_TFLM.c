/**
 ******************************************************************************
 * @file    aiSystemPerformance_TFLM.c
 * @author  MCD/AIS Team
 * @brief   AI System perf. application - TFLM runtime
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2019,2023 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/*
 * Description
 *
 * - Entry points) for the AI System Perf. which uses a TFLM runtime.
 *
 * History:
 *  - v1.0 - Initial version
 *  - v1.1 - Code clean-up
 *           Add CB log (APP_DEBUG only)
 *  - v1.2 - Add support to use SYSTICK only (remove direct call to DWT fcts)
 *  - v2.0 - align code with TFLM from tflite-micro repo and new ai_buffer definition
 *           86c8d52 - Fix erroneous write from EXPAND_DIMS to an array that can be in read-only region (#649)
 *  - v2.1 - Add support for IO tensor with shape > 4 (up to 6)
 *  - v3.0 - align code with ai_stm32_adpaptor.h file (remove direct call of HAL_xx fcts)
 *  - v3.1 - Fix the log of uint64_t value
 */

/* System headers */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#if !defined(USE_OBSERVER)
#define USE_OBSERVER         1 /* 0: remove the registration of the user CB to evaluate the inference time by layer */
#endif
#define USE_CORE_CLOCK_ONLY  0 /* 1: remove usage of the HAL_GetTick() to evaluate the number of CPU clock. Only the Core
                                *    DWT IP is used. HAL_Tick() is requested to avoid an overflow with the DWT clock counter
                                *    (32b register) - USE_SYSTICK_ONLY should be set to 0.
                                */
#define USE_SYSTICK_ONLY     0 /* 1: use only the SysTick to evaluate the time-stamps (for Cortex-m0 based device, this define is forced) */

#if !defined(APP_DEBUG)
#define APP_DEBUG     	     0 /* 1: add debug trace - application level */
#endif


#if APP_DEBUG != 1
#define _APP_ITER_     16  /* number of iteration for perf. test */
#else
#define _APP_ITER_     4  /* number of iteration for perf. test */
#endif

/* APP header files */
#include <aiSystemPerformance.h>
#include <aiTestUtility.h>

#include <tflm_c.h>

#include "network_tflite_data.h"

/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */
#define _APP_VERSION_MAJOR_     (0x03)
#define _APP_VERSION_MINOR_     (0x01)
#define _APP_VERSION_           ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_              "AI system performance TFLM"

/* Global variables */
static bool profiling_mode = false;
static int  profiling_factor = 5;


/* -----------------------------------------------------------------------------
 * Object definition/declaration for AI-related execution context
 * -----------------------------------------------------------------------------
 */

struct tflm_context {
  uint32_t hdl;
} net_exec_ctx[1] = {0};


/* Local activations buffer */
MEM_ALIGNED(16)
static uint8_t tensor_arena[TFLM_NETWORK_TENSOR_AREA_SIZE+32];

extern UART_HandleTypeDef UartHandle;

#ifdef __cplusplus
extern "C"
{
#endif

int tflm_io_write(const void *buff, uint16_t count)
{
    HAL_StatusTypeDef status;

    status = HAL_UART_Transmit(&UartHandle, (uint8_t *)buff, count,
            HAL_MAX_DELAY);

    return (status == HAL_OK ? count : 0);
}

#ifdef __cplusplus
}
#endif

void log_tensor(struct tflm_c_tensor_info* t_info, int idx)
{
  LC_PRINT(" - %d:%s:%d:(", idx,
      tflm_c_TfLiteTypeGetName(t_info->type), (int)t_info->bytes);
  for (size_t i=0; i<t_info->shape.size; i++) {
    LC_PRINT("%d", t_info->shape.data[i]);
    if (i==(t_info->shape.size-1))
      LC_PRINT(")");
    else
      LC_PRINT("x");
  }
  if (t_info->scale)
    LC_PRINT(":s=%f:zp=%d\r\n", (double)t_info->scale, t_info->zero_point);
  else
    LC_PRINT("\r\n");
}

#if defined(USE_OBSERVER) && USE_OBSERVER == 1

struct u_node_stat {
  char name[22];
  uint64_t dur;
};

static uint64_t _current_time_ticks_cb(int mode)
{
  uint64_t val = cyclesCounterEnd();
  // if (mode)
  //  cyclesCounterStart();
  return val;
}

static int _observer_node_cb(const void* cookie,
    const uint32_t flags,
    const struct tflm_c_node* node)
{
  if (cookie) {
    struct u_node_stat* stat = (struct u_node_stat*)cookie;
    // stat[node->node_info.idx].name =
    strncpy(stat[node->node_info.idx].name, node->node_info.name, 20); // strlen(node->node_info.name));
    stat[node->node_info.idx].dur += node->node_info.dur;
  }

#if APP_DEBUG == 1
  if (node->node_info.n_outputs) {
      LC_PRINT("CB %s:%d:%d %d %d\r\n", node->node_info.name,
          (int)node->node_info.builtin_code, (int)node->node_info.idx,
          (int)node->node_info.dur, (int)node->node_info.n_outputs);
      for (int i=0; i<node->node_info.n_outputs; i++) {
          struct tflm_c_tensor_info* t_info = &node->output[i];
          log_tensor(t_info, i);
        }
  }
  else
    LC_PRINT("CB %s:%d %d\r\n", node->node_info.name, (int)node->node_info.idx,
        (int)node->node_info.dur);
#endif

  return 0;
}

struct tflm_c_observer_options _observer_options = {
  .notify = _observer_node_cb,
  .get_time = _current_time_ticks_cb,
  .cookie = NULL,
#if APP_DEBUG == 1
  .flags = OBSERVER_FLAGS_DEFAULT,
#else
  .flags = OBSERVER_FLAGS_TIME_ONLY,
#endif
};

static TfLiteStatus observer_init(struct tflm_context *ctx)
{
  TfLiteStatus res;
  int sz;

  if (_observer_options.cookie)
      return kTfLiteError;

  sz = tflm_c_operators_size(ctx->hdl) * sizeof(struct u_node_stat);
  _observer_options.cookie = (struct u_node_stat*)malloc(sz);

  if (!_observer_options.cookie)
    return kTfLiteError;

  memset(_observer_options.cookie, 0, sz);

  res = tflm_c_observer_register(ctx->hdl, &_observer_options);
  if (res != kTfLiteOk) {
    LC_PRINT("Unable to register the callback observer..\r\n");
    return kTfLiteError;
  }
  return kTfLiteOk;
}

static void observer_done(struct tflm_context *ctx)
{
  struct dwtTime t;
  struct tflm_c_profile_info p_info;

  if (ctx->hdl == 0)
    return;

  tflm_c_observer_info(ctx->hdl, &p_info);

  if (_observer_options.cookie && p_info.node_dur) {
    struct u_node_stat* stat = (struct u_node_stat*)_observer_options.cookie;
    uint64_t cumul = 0;
    LC_PRINT("\r\n Inference time by c-node\r\n");
    dwtCyclesToTime(p_info.node_dur / p_info.n_invoks, &t);
    LC_PRINT("  kernel  : %d.%03dms (time passed in the c-kernel fcts)\r\n", t.s * 1000 + t.ms, t.us);
    dwtCyclesToTime(p_info.cb_dur / p_info.n_invoks, &t);
    LC_PRINT("  user    : %d.%03dms (time passed in the user cb)\r\n", t.s * 1000 + t.ms, t.us);

    LC_PRINT("\r\n %-6s%-25s %s\r\n", "idx", "name", "time (ms)");
    LC_PRINT(" ---------------------------------------------------\r\n");

    const char *fmt = " %-6d%-25s %6d.%03d %6.02f %c\r\n";
    for (int i=0; i<tflm_c_operators_size(ctx->hdl); i++) {
      dwtCyclesToTime(stat[i].dur / p_info.n_invoks, &t);
      cumul += stat[i].dur;
      LC_PRINT(fmt,
          i,
          stat[i].name,
          t.s * 1000 + t.ms, t.us,
          ((double)stat[i].dur * 100.0) / (double)p_info.node_dur,
          '%');
    }
    LC_PRINT(" ---------------------------------------------------\r\n");
    dwtCyclesToTime(cumul / p_info.n_invoks, &t);
    LC_PRINT(" %31s %6d.%03d ms\r\n", "", t.s * 1000 + t.ms, t.us);

    free(_observer_options.cookie);
    _observer_options.cookie = NULL;

    tflm_c_observer_unregister(ctx->hdl, &_observer_options);
  }
}

#endif

static int aiBootstrap(struct tflm_context *ctx)
{
  TfLiteStatus res;
  struct tflm_c_version ver;

  /* Creating an instance of the network ------------------------- */
  LC_PRINT("\r\nInstancing the network.. (cWrapper: v%s)\r\n", TFLM_C_VERSION_STR);

  /* TFLm runtime expects that the tensor arena is aligned on 16-bytes */
  uint32_t uaddr = (uint32_t)tensor_arena;
  uaddr = (uaddr + (16 - 1)) & (uint32_t)(-16);  // Round up to 16-byte boundary

  MON_ALLOC_RESET();
  MON_ALLOC_ENABLE();

  res = tflm_c_create(g_tflm_network_model_data, (uint8_t*)uaddr,
          TFLM_NETWORK_TENSOR_AREA_SIZE, &ctx->hdl);

  MON_ALLOC_DISABLE();

  if (res != kTfLiteOk) {
    return -1;
  }

  tflm_c_rt_version(&ver);

  LC_PRINT(" TFLM version       : %d.%d.%d\r\n", (int)ver.major, (int)ver.minor, (int)ver.patch);
#if defined(TFLM_NETWORK_NAME)
  LC_PRINT(" Network name       : %s\r\n", TFLM_NETWORK_NAME);
#else
  LC_PRINT(" Network name       : network\r\n");
#endif
  LC_PRINT(" TFLite file        : 0x%08x (%d bytes)\r\n", (int)g_tflm_network_model_data,
      (int)g_tflm_network_model_data_len);
  LC_PRINT(" Arena location     : 0x%08x\r\n", (int)uaddr);
  LC_PRINT(" Opcode size        : %d\r\n", (int)tflm_c_operator_codes_size(ctx->hdl));
  LC_PRINT(" Operator size      : %d\r\n", (int)tflm_c_operators_size(ctx->hdl));
  LC_PRINT(" Tensor size        : %d\r\n", (int)tflm_c_tensors_size(ctx->hdl));
  LC_PRINT(" Allocated size     : %d / %d\r\n", (int)tflm_c_arena_used_bytes(ctx->hdl),
      TFLM_NETWORK_TENSOR_AREA_SIZE);
  LC_PRINT(" Inputs size        : %d\r\n", (int)tflm_c_inputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_inputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_input(ctx->hdl, i, &t_info);
    log_tensor(&t_info, i);
  }
  LC_PRINT(" Outputs size       : %d\r\n", (int)tflm_c_outputs_size(ctx->hdl));
  for (int i=0; i<tflm_c_outputs_size(ctx->hdl); i++) {
    struct tflm_c_tensor_info t_info;
    tflm_c_output(ctx->hdl, i, &t_info);
    log_tensor(&t_info, i);
  }
#if defined(_APP_HEAP_MONITOR_) && _APP_HEAP_MONITOR_ == 1
  LC_PRINT(" Used heap          : %d bytes (max=%d bytes) (for c-wrapper %s)\r\n",
      MON_ALLOC_USED(), MON_ALLOC_MAX_USED(), TFLM_C_VERSION_STR);
  // MON_ALLOC_REPORT();
#endif

  return 0;
}

static void aiDone(struct tflm_context *ctx)
{
  /* Releasing the instance(s) ------------------------------------- */
  LC_PRINT("Releasing the instance...\r\n");

  if (ctx->hdl != 0) {
    tflm_c_destroy(ctx->hdl);
    ctx->hdl = 0;
  }
}

static int aiInit(void)
{
  int res;

  net_exec_ctx[0].hdl = 0;
  res = aiBootstrap(&net_exec_ctx[0]);

  return res;
}

static void aiDeInit(void)
{
  aiDone(&net_exec_ctx[0]);
}


/* -----------------------------------------------------------------------------
 * Specific APP/test functions
 * -----------------------------------------------------------------------------
 */

static int aiTestPerformance(void)
{
  int iter;
  TfLiteStatus res;
  int niter;
  struct tflm_c_version ver;

  struct dwtTime t;
  uint64_t tcumul;
  uint64_t tend;

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  struct tflm_c_profile_info p_info;
#endif

  struct tflm_context *ctx = &net_exec_ctx[0];

  tflm_c_rt_version(&ver);

  if (ctx->hdl == 0) {
    LC_PRINT("E: network handle is NULL\r\n");
    return -1;
  }

  MON_STACK_INIT();

  if (profiling_mode)
    niter = _APP_ITER_ * profiling_factor;
  else
    niter = _APP_ITER_;

  LC_PRINT("\r\nRunning PerfTest with random inputs (%d iterations)...\r\n", niter);


#if APP_DEBUG == 1
  MON_STACK_STATE("stack before loop");
#endif


  MON_STACK_CHECK0();

  /* reset/init cpu clock counters */
  tcumul = 0ULL;

  MON_STACK_MARK();

  if (profiling_mode) {
    LC_PRINT("Profiling mode (%d)...\r\n", profiling_factor);
    fflush(stdout);
  }

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  observer_init(ctx);
  tflm_c_observer_start(ctx->hdl);
#endif

  MON_ALLOC_RESET();

  /* Main inference loop */
  for (iter = 0; iter < niter; iter++) {

    /* Fill input tensors with random data */
    /* .. */

    MON_ALLOC_ENABLE();

    // free(malloc(20));

    cyclesCounterStart();
    res = tflm_c_invoke(ctx->hdl);
    tend = cyclesCounterEnd();

    if (res != kTfLiteOk) {
      LC_PRINT("tflm_c_invoke() fails\r\n");
      return res;
    }

    MON_ALLOC_DISABLE();

    tcumul += tend;

    dwtCyclesToTime(tend, &t);

#if APP_DEBUG == 1
    LC_PRINT("#%02d %8d.%03dms (%ld cycles)\r\n", iter,
        t.ms, t.us, (long)tend);
#else
    if (!profiling_mode) {
      if (t.s > 10)
        niter = iter;
      LC_PRINT(".");
      fflush(stdout);
    }
#endif
  } /* end of the main loop */

#if APP_DEBUG != 1
  LC_PRINT("\r\n");
#endif

  MON_STACK_EVALUATE();

  LC_PRINT("\r\n");

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  tflm_c_observer_info(ctx->hdl, &p_info);
  if (p_info.node_dur)
    tcumul -= p_info.cb_dur;
#endif

  tcumul /= (uint64_t)iter;

  dwtCyclesToTime(tcumul, &t);

  LC_PRINT("Results TFLM %d.%d.%d, %d inferences @%dMHz/%dMHz\r\n",
      (int)ver.major, (int)ver.minor, (int)ver.patch,
      (int)iter,
      (int)(port_hal_get_cpu_freq() / 1000000),
      (int)(port_hal_get_sys_freq() / 1000000));

  LC_PRINT(" duration     : %d.%03d ms (average)\r\n", t.s * 1000 + t.ms, t.us);
  LC_PRINT(" CPU cycles   : %s (average)\r\n", uint64ToDecimal(tcumul));
  LC_PRINT(" CPU Workload : %d%c (duty cycle = 1s)\r\n", (int)((tcumul * 100) / t.fcpu), '%');

  MON_STACK_REPORT();
  MON_ALLOC_REPORT();

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  observer_done(ctx);
#endif

  return 0;
}

/* -----------------------------------------------------------------------------
 * Basic interactive console
 * -----------------------------------------------------------------------------
 */

#define CONS_EVT_TIMEOUT    (0)
#define CONS_EVT_QUIT       (1)
#define CONS_EVT_RESTART    (2)
#define CONS_EVT_HELP       (3)
#define CONS_EVT_PAUSE      (4)
#define CONS_EVT_PROF       (5)
#define CONS_EVT_HIDE       (6)

#define CONS_EVT_UNDEFINED  (100)

static int aiTestConsole(void)
{
  uint8_t c = 0;

  if (ioRawGetUint8(&c, 5000) == -1) /* Timeout */
    return CONS_EVT_TIMEOUT;

  if ((c == 'q') || (c == 'Q'))
    return CONS_EVT_QUIT;

  if ((c == 'd') || (c == 'D'))
    return CONS_EVT_HIDE;

  if ((c == 'r') || (c == 'R'))
    return CONS_EVT_RESTART;

  if ((c == 'h') || (c == 'H') || (c == '?'))
    return CONS_EVT_HELP;

  if ((c == 'p') || (c == 'P'))
    return CONS_EVT_PAUSE;

  if ((c == 'x') || (c == 'X'))
    return CONS_EVT_PROF;

  return CONS_EVT_UNDEFINED;
}


/* -----------------------------------------------------------------------------
 * Exported/Public functions
 * -----------------------------------------------------------------------------
 */

int aiSystemPerformanceInit(void)
{
  LC_PRINT("\r\n#\r\n");
  LC_PRINT("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_, _APP_VERSION_MINOR_ );
  LC_PRINT("#\r\n");

  systemSettingLog();

  cyclesCounterInit();

  if (aiInit()) {
    while (1);
  }

  srand(3); /* deterministic outcome */

  return 0;
}

int aiSystemPerformanceProcess(void)
{
  int r;

  do {
    r = aiTestPerformance();

    if (!r) {
      r = aiTestConsole();

      if (r == CONS_EVT_UNDEFINED) {
        r = 0;
      } else if (r == CONS_EVT_HELP) {
        LC_PRINT("\r\n");
        LC_PRINT("Possible key for the interactive console:\r\n");
        LC_PRINT("  [q,Q]      quit the application\r\n");
        LC_PRINT("  [r,R]      re-start (NN de-init and re-init)\r\n");
        LC_PRINT("  [p,P]      pause\r\n");
        LC_PRINT("  [d,D]      hide detailed information ('r' to restore)\r\n");
        LC_PRINT("  [h,H,?]    this information\r\n");
        LC_PRINT("   xx        continue immediately\r\n");
        LC_PRINT("\r\n");
        LC_PRINT("Press any key to continue..\r\n");

        while ((r = aiTestConsole()) == CONS_EVT_TIMEOUT) {
          port_hal_delay(1000);
        }
        if (r == CONS_EVT_UNDEFINED)
          r = 0;
      }
      if (r == CONS_EVT_PROF) {
        profiling_mode = true;
        profiling_factor *= 2;
        r = 0;
      }

      if (r == CONS_EVT_HIDE) {
        // observer_mode = false;
        r = 0;
      }

      if (r == CONS_EVT_RESTART) {
        profiling_mode = false;
        // observer_mode = true;
        profiling_factor = 5;
        LC_PRINT("\r\n");
        aiDeInit();
        aiSystemPerformanceInit();
        r = 0;
      }
      if (r == CONS_EVT_QUIT) {
        profiling_mode = false;
        LC_PRINT("\r\n");
        disableInts();
        aiDeInit();
        LC_PRINT("\r\n");
        LC_PRINT("Board should be reseted...\r\n");
        while (1) {
          port_hal_delay(1000);
        }
      }
      if (r == CONS_EVT_PAUSE) {
        LC_PRINT("\r\n");
        LC_PRINT("Press any key to continue..\r\n");
        while ((r = aiTestConsole()) == CONS_EVT_TIMEOUT) {
          port_hal_delay(1000);
        }
        r = 0;
      }
    }
  } while (r==0);

  return r;
}

void aiSystemPerformanceDeInit(void)
{
  LC_PRINT("\r\n");
  aiDeInit();
  LC_PRINT("bye bye ...\r\n");
}

