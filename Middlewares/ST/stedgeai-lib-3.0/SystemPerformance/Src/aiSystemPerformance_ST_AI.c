/**
 ******************************************************************************
 * @file    aiSystemPerformance_ST_AI.c
 * @author  MCD/AIS Team
 * @brief   AI system performance application (ST AI embedded c-api)
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/* Description:
 *
 * Main entry points for AI validation on-target process.
 *
 * History:
 *  - v1.0 - Initial version (ST AI embedded c-api)
 *
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
#define USE_SYSTICK_ONLY     0 /* 1: use only the SysTick to evaluate the time-stamps (for Cortex-m0 based device, this define is forced)
                                *    (see aiTestUtility.h file)
                                */

/* APP header files */
#include <aiSystemPerformance.h>
#include <aiTestUtility.h>
#include <aiTestHelper_ST_AI.h>


/* AI header files */
#include <stai.h>


#if defined(SR5E1)
#include <app_stellar-studio-ai.h>
#else
#include <app_x-cube-ai.h>
#endif


/* -----------------------------------------------------------------------------
 * TEST-related definitions
 * -----------------------------------------------------------------------------
 */

#define _APP_VERSION_MAJOR_     (0x01)
#define _APP_VERSION_MINOR_     (0x00)
#define _APP_VERSION_           ((_APP_VERSION_MAJOR_ << 8) | _APP_VERSION_MINOR_)

#define _APP_NAME_              "AI system perf (ST.AI)"

#define _APP_ITER_              16  /* number of iteration for perf. test */


/* Global variables */
static bool observer_mode = true;
static bool profiling_mode = false;
static int  profiling_factor = 5;


/* -----------------------------------------------------------------------------
 * AI-related functions
 * -----------------------------------------------------------------------------
 */

struct ai_network_exec_ctx {
  stai_ptr handle;
  stai_network_info report;
  tensor_descs inputs;
  tensor_descs outputs;
  tensor_descs acts;
  tensor_descs states;
  tensor_descs params;

#if defined(USE_OBSERVER) && USE_OBSERVER == 1
  uint32_t  c_idx;
  int  n_inf;
  const stai_network_details *details;
  uint64_t tnodes;                /* number of cycles to execute the operators (including nn.init)
                                     nn.done is excluded but added by the adjust function */
  uint64_t *cycles;               /* cycles per node */
#endif

} net_exec_ctx[STAI_MNETWORK_NUMBER] = {0};


#if defined(USE_OBSERVER) && USE_OBSERVER == 1

/* -----------------------------------------------------------------------------
 * Observer services
 * -----------------------------------------------------------------------------
 */

#include "stai_events.h"

void _stai_event_cb(void* cb_cookie, const stai_event_type event_type,
    const void* event_payload)
{
  volatile uint64_t ts = cyclesCounterEnd(); /* time stamp to mark the entry */

  struct ai_network_exec_ctx *ctx = (struct ai_network_exec_ctx *)cb_cookie;

  if (!ctx || !ctx->details || !ctx->cycles)
    return;

  if (event_type == STAI_EVENT_NODE_START && ctx->c_idx == 0) {
    ctx->cycles[0] += ts;  // nn.init cycles
    ctx->tnodes += ts;
  }

  if (event_type == STAI_EVENT_NODE_STOP) {
    ctx->cycles[ctx->c_idx] += ts;  // nn.node(x) cycles
    ctx->c_idx += 1;
    if (ctx->c_idx == ctx->details->n_nodes) {
      ctx->c_idx = 0;
      ctx->n_inf += 1;
    }
    ctx->tnodes += ts;
  }

  cyclesCounterStart();
}

static void aiObserverInit(struct ai_network_exec_ctx *ctx)
{
  stai_return_code stai_err;

  if (!ctx)
    return;

  ctx->details = stai_mnetwork_get_details(ctx->handle);
  if (!ctx->details) {
    LC_PRINT("E:OBS: details info not available\r\n");
    return;
  }

  stai_err = stai_mnetwork_set_callback(ctx->handle, _stai_event_cb, ctx);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "stai_mnetwork_set_callback");
    return;
  }

  ctx->cycles = (uint64_t *)malloc(ctx->details->n_nodes * sizeof(uint64_t));

  if (!ctx->cycles) {
    LC_PRINT("E:OBS: resources allocation fails..\r\n");
    return;
  }
}

static uint64_t aiObserverAdjust(struct ai_network_exec_ctx *ctx, uint64_t tend)
{
  if (!ctx || !ctx->details || !ctx->cycles)
    return tend;

  ctx->cycles[ctx->details->n_nodes - 1] += tend;  // add nn.deinit to the last node
  tend = ctx->tnodes + tend;
  ctx->tnodes = 0UL;
  return tend;
}


static void aiObserverReset(struct ai_network_exec_ctx *ctx)
{
  if (!ctx || !ctx->details || !ctx->cycles)
    return;
  ctx->c_idx = 0U;
  ctx->tnodes = 0UL;
  ctx->n_inf = 0;
  memset(ctx->cycles, 0, ctx->details->n_nodes * sizeof(uint64_t));
}

extern const char* ai_layer_type_name(const int type);


static void aiObserverLog(struct ai_network_exec_ctx *ctx)
{
  struct dwtTime t;

  if (!ctx || !ctx->details || !ctx->cycles)
    return;

  ctx->tnodes = 0UL;
  for (uint32_t i = 0; i < ctx->details->n_nodes; i++) {
    ctx->tnodes += ctx->cycles[i];
  }
  ctx->tnodes /= ctx->n_inf;

  LC_PRINT("\r\n");
  // LC_PRINT(" Performance per c-node - %d iterations, %s cycles (average)\r\n", ctx->n_inf,
  //    uint64ToDecimal(ctx->tnodes));

  LC_PRINT(" Performance per c-node\r\n");

  LC_PRINT(" ------------------------------------------------------\r\n");
  LC_PRINT(" %-6s %-6s %-20s %s\r\n","c_id", "id", "type", "time (ms)");
  LC_PRINT(" ------------------------------------------------------\r\n");

  for (uint32_t i = 0; i < ctx->details->n_nodes; i++) {
    uint64_t cycles = ctx->cycles[i] / ctx->n_inf;
    dwtCyclesToTime(cycles, &t);
    double per_ = ((double)cycles / (double)ctx->tnodes) * (double)100.0;
    LC_PRINT(" %-6d %-6d %-20s %6d.%03d %6.02f %c\r\n", i,
        ctx->details->nodes[i].id,
        ai_layer_type_name(ctx->details->nodes[i].type),
        t.s * 1000 + t.ms, t.us,
        (double)per_, '%');
  }

  LC_PRINT(" ------------------------------------------------------\r\n");
  dwtCyclesToTime(ctx->tnodes, &t);
  LC_PRINT(" %34s %6d.%03d ms\r\n", "", t.s * 1000 + t.ms, t.us);
}

static void aiObserverDeinit(struct ai_network_exec_ctx *ctx)
{
  if (!ctx || !ctx->details || !ctx->cycles)
    return;

  aiObserverReset(ctx);
  free((void *)ctx->cycles);
  ctx->cycles = NULL;
}

#else

#define aiObserverInit(_ctx)
#define aiObserverReset(_ctx)
#define aiObserverAdjust(_ctx, _tend) (_tend)
#define aiObserverLog(_ctx)
#define aiObserverDeinit(_ctx)

#endif



static int aiBootstrap(struct ai_network_exec_ctx *ctx, const char *nn_name)
{
  stai_return_code stai_err;

  /* Creating the instance of the  network ------------------------- */
  LC_PRINT("Creating the instance for \"%s\" model..\r\n", nn_name);

  stai_err = stai_mnetwork_init(nn_name, &ctx->handle);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "ai_mnetwork_get_info");
    return -1;
  }

  /* Initialize the instance --------------------------------------- */
  LC_PRINT("Initializing..\r\n");

  stai_err = stai_mnetwork_get_info(ctx->handle, &ctx->report);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "ai_mnetwork_get_info");
    stai_mnetwork_deinit(ctx->handle);
    ctx->handle = NULL;
  }

  ctx->inputs.tensors = ctx->report.inputs;
  ctx->outputs.tensors = ctx->report.outputs;
  ctx->acts.tensors = ctx->report.activations;
  ctx->params.tensors = ctx->report.weights;

  /* Set the activations ------------------------------------------- */
  LC_PRINT(" Set activation buffers..\r\n");
  if (ctx->report.flags & STAI_FLAG_ACTIVATIONS)
    stai_err = stai_mnetwork_get_activations(ctx->handle, data_activations);
  else
    stai_err = stai_mnetwork_set_activations(ctx->handle, data_activations);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "stai_mnetwork_set_activations");
    stai_mnetwork_deinit(ctx->handle);
    ctx->handle = NULL;
  }
  ctx->acts.n_tensor = ctx->report.n_activations;
  ctx->acts.address = (uintptr_t *)data_activations;

  /* Set the states ------------------------------------------- */
  LC_PRINT(" Set state buffers..\r\n");
  if (ctx->report.flags & STAI_FLAG_STATES)
    stai_err = stai_mnetwork_get_states(ctx->handle, data_states);
  else
    stai_err = stai_mnetwork_set_states(ctx->handle, data_states);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "stai_mnetwork_set_states");
    stai_mnetwork_deinit(ctx->handle);
    ctx->handle = NULL;
  }

  ctx->states.n_tensor = ctx->report.n_states;
  ctx->states.address = (uintptr_t *)data_states;

  /* Set params/weights -------------------------------------------- */
  ctx->params.n_tensor = ctx->report.n_weights;
  ctx->params.address = (uintptr_t *)stai_mnetwork_get_weights_ext(ctx->handle);

  /* Set IO -------------------------------------------------------- */
  LC_PRINT(" Set IO buffers..\r\n");
  if (ctx->report.flags & STAI_FLAG_INPUTS)
    stai_err = stai_mnetwork_get_inputs(ctx->handle, data_ins);
  else
    stai_err = stai_mnetwork_set_inputs(ctx->handle, data_ins);
  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "stai_mnetwork_set/get_inputs");
    stai_mnetwork_deinit(ctx->handle);
    ctx->handle = NULL;
  }
  ctx->inputs.n_tensor = ctx->report.n_inputs;
  ctx->inputs.address = (uintptr_t *)data_ins;

  if (ctx->report.flags & STAI_FLAG_OUTPUTS)
    stai_err = stai_mnetwork_get_outputs(ctx->handle, data_outs);
  else
    stai_err = stai_mnetwork_set_outputs(ctx->handle, data_outs);

  if (stai_err != STAI_SUCCESS) {
    stai_log_err(stai_err, "stai_mnetwork_set/get_outputs");
    stai_mnetwork_deinit(ctx->handle);
    ctx->handle = NULL;
  }
  ctx->outputs.n_tensor = ctx->report.n_outputs;
  ctx->outputs.address = (uintptr_t *)data_outs;

  /* Alloc/init oberserver resources */
  aiObserverInit(ctx);

  /* Display the network info -------------------------------------- */
  stai_log_network_info(&ctx->report,
      &ctx->inputs, &ctx->outputs, &ctx->acts, &ctx->states, &ctx->params);

  return 0;
}


static int aiInit(void)
{
  int res = -1;
  const char *nn_name;
  int idx;

  stai_platform_version();

  /* Reset the contexts -------------------------------------------- */
  for (idx=0; idx < STAI_MNETWORK_NUMBER; idx++) {
    net_exec_ctx[idx].handle = NULL;
  }

  /* Discover and initialize the network(s) ------------------------ */
  LC_PRINT("Discovering the network(s)...\r\n");

  idx = 0;
  do {
    nn_name = stai_mnetwork_find(NULL, idx);
    if (nn_name) {
      LC_PRINT("\r\nFound network \"%s\"\r\n", nn_name);
      res = aiBootstrap(&net_exec_ctx[idx], nn_name);
      if (res)
        nn_name = NULL;
    }
    idx++;
  } while (nn_name);

  return res;
}

static void aiDeInit(void)
{
  stai_return_code stai_err;
  int idx;

  /* Releasing the instance(s) ------------------------------------- */
  LC_PRINT("Releasing the instance(s)...\r\n");

  for (idx=0; idx<STAI_MNETWORK_NUMBER; idx++) {
    if (net_exec_ctx[idx].handle != NULL) {
      aiObserverDeinit(&net_exec_ctx[idx]);
      stai_err = stai_mnetwork_deinit(net_exec_ctx[idx].handle);
      if (stai_err != STAI_SUCCESS) {
        stai_log_err(stai_err, "ai_mnetwork_deinit");
      }
      net_exec_ctx[idx].handle = NULL;
    }
  }
}


/* -----------------------------------------------------------------------------
 * Specific APP/test functions
 * -----------------------------------------------------------------------------
 */

static int aiTestPerformance(int idx)
{
  stai_return_code stai_err;
  int iter;
  int niter;

  struct dwtTime t;
  uint64_t tcumul;
  uint64_t tend;
  uint64_t cmacc;
  struct ai_network_exec_ctx *ctx = &net_exec_ctx[idx];


  if (ctx->handle == NULL) {
    LC_PRINT("E: network handle is NULL\r\n");
    return -1;
  }

  MON_STACK_INIT();

  if (profiling_mode)
    niter = _APP_ITER_ * profiling_factor;
  else
    niter = _APP_ITER_;

  LC_PRINT("\r\nRunning PerfTest on \"%s\" with random inputs (ST.AI)...\r\n",
      ctx->report.c_model_name);

#if APP_DEBUG == 1
  MON_STACK_STATE("stack before test");
#endif

  MON_STACK_CHECK0();

  /* reset/init cpu clock counters */
  tcumul = 0ULL;

  MON_STACK_MARK();

  if ((ctx->report.n_inputs > STAI_MNETWORK_IN_NUM) ||
      (ctx->report.n_outputs > STAI_MNETWORK_OUT_NUM))
  {
    LC_PRINT("E: STAI_MNETWORK_IN/OUT_NUM definition are incoherent\r\n");
    port_hal_delay(100);
    return -1;
  }

  MON_ALLOC_RESET();

  /* Main inference loop */
  aiObserverReset(ctx);
  for (iter = 0; iter < niter; iter++) {

    MON_ALLOC_ENABLE();

    // free(malloc(20));

    cyclesCounterStart();
    stai_err = stai_mnetwork_run(ctx->handle);
    if (stai_err != STAI_SUCCESS) {
      stai_log_err(stai_err, "ai_mnetwork_run");
      break;
    }
    tend = cyclesCounterEnd();
    tend = aiObserverAdjust(ctx, tend);

    MON_ALLOC_DISABLE();

    tcumul += tend;

    dwtCyclesToTime(tend, &t);

#if APP_DEBUG == 1
    LC_PRINT(" #%02d %8d.%03dms (%ld cycles)\r\n", iter,
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

  tcumul /= (uint64_t)iter;

  dwtCyclesToTime(tcumul, &t);

  LC_PRINT("Results for \"%s\", %d inferences @%dMHz/%dMHz (complexity: %s MACC)\r\n",
      ctx->report.c_model_name, (int)iter,
      (int)(port_hal_get_cpu_freq() / 1000000),
      (int)(port_hal_get_sys_freq() / 1000000),
      uint64ToDecimal(ctx->report.n_macc));

  LC_PRINT(" duration     : %d.%03d ms (average)\r\n", t.s * 1000 + t.ms, t.us);
  LC_PRINT(" CPU cycles   : %s (average)\r\n", uint64ToDecimal(tcumul));
  LC_PRINT(" CPU Workload : %d%c (duty cycle = 1s)\r\n", (int)((tcumul * 100) / t.fcpu), '%');
  cmacc = (uint64_t)((tcumul * 100)/ net_exec_ctx[idx].report.n_macc);
  LC_PRINT(" cycles/MACC  : %d.%02d (average for all layers)\r\n",
    (int)(cmacc / 100), (int)(cmacc - ((cmacc / 100) * 100)));

  MON_STACK_REPORT();
  MON_ALLOC_REPORT();

  aiObserverLog(ctx);

  return 0;
}

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
  LC_PRINT("# %s %d.%d\r\n", _APP_NAME_ , _APP_VERSION_MAJOR_,
      _APP_VERSION_MINOR_ );
  LC_PRINT("#\r\n");

  systemSettingLog();

  cyclesCounterInit();

  aiInit();

  srand(3); /* deterministic outcome */

  return 0;
}

int aiSystemPerformanceProcess(void)
{
  int r;
  int idx = 0;

  do {
    r = aiTestPerformance(idx);
    idx = (idx+1) % STAI_MNETWORK_NUMBER;

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
        observer_mode = false;
        r = 0;
      }

      if (r == CONS_EVT_RESTART) {
        profiling_mode = false;
        observer_mode = true;
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

