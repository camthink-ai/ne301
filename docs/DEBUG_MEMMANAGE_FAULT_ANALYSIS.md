# MemManage Fault 调试分析报告

## 问题背景

commit `0e71bfbb` ("feat: support muilt models runtime") 引入了 MemManage fault 问题。该提交将 19 个 pp (postprocess) 文件从静态变量改为动态内存分配，以支持多模型运行时。

## 问题现象

系统启动后出现 MemManage_Handler，打印信息如下：

```
=== MemManage Fault ===
MMFSR: 0x01
  IACCVIOL: Instruction access violation
PSP: 0x91E8BEC0, MSP: 0x341D1B90
Stacked R0:  0x3406063C
Stacked R1:  0x00000002
Stacked R2:  0x91E8BF38
Stacked R3:  0xFFFF0203
Stacked R12: 0x7FFFFFFF
Stacked LR:  0x90085997
Stacked PC:  0xFFFF0202
Stacked xPSR:0xA1000000
========================
```

## 问题定位过程

### 1. 二分搜索定位问题文件

通过 git revert 和逐步恢复文件的方式，定位到问题与 `pp_sseg_deeplab_v3_ui.c` 相关：
- 18 个 pp 文件使用新版本 + 1 个旧版本 → **正常**
- 19 个 pp 文件全部使用新版本 → **问题出现**

### 2. 内存布局分析

对比两个版本的内存占用：
- text: +40 bytes
- bss: -32 bytes

差异极小，不应导致内存问题。

### 3. 增强 MemManage_Handler 调试

修改 `stm32n6xx_it.c` 中的 `MemManage_Handler`，添加详细的故障信息打印：

```c
void MemManage_Handler(void)
{
  uint32_t cfsr = SCB->CFSR;
  uint32_t mmfar = SCB->MMFAR;
  uint32_t mmfsr = (cfsr & SCB_CFSR_MEMFAULTSR_Msk);
  
  printf("\r\n=== MemManage Fault ===\r\n");
  printf("MMFSR: 0x%02lX\r\n", mmfsr);
  if (mmfsr & (1 << 7)) printf("  MMARVALID: MMFAR valid\r\n");
  if (mmfsr & (1 << 5)) printf("  MLSPERR: FP lazy state preservation\r\n");
  if (mmfsr & (1 << 4)) printf("  MSTKERR: Stacking error\r\n");
  if (mmfsr & (1 << 3)) printf("  MUNSTKERR: Unstacking error\r\n");
  if (mmfsr & (1 << 1)) printf("  DACCVIOL: Data access violation\r\n");
  if (mmfsr & (1 << 0)) printf("  IACCVIOL: Instruction access violation\r\n");
  
  // Print stack info and registers...
}
```

### 4. 故障原因分析

通过 `arm-none-eabi-addr2line` 解析 LR 地址：

```bash
$ arm-none-eabi-addr2line -e build/ne301_App.elf -f -C 0x90085997
mg_call
mongoose.c:518
```

对应代码：
```c
// mongoose.c:518
if (c->fn != NULL) c->fn(c, ev, ev_data);
```

**关键发现**：`c->fn` 函数指针被损坏为 `0xFFFF0202`（无效地址）

### 5. 内存分配问题分析

检查 `mg_connection` 结构体的分配：
- R0 = 0x3406063C（`mg_connection` 指针）
- 该地址在 `_user_heap_stack` 区域 (0x34060100 - 0x34061300)
- 这是**标准库的堆区域**，只有 512 bytes！

但预期的分配应该在：
- `external_slab_buffer`: 0x90854000 (PSRAM, 22MB)
- `internal_slab_buffer`: 0x34032100 (SRAM, 184KB)

## 内存分配链路分析

```
mongoose:
  mg_calloc() 
    → buffer_calloc() 
      → hal_mem_calloc_large() 
        → hal_mem_calloc(nmemb, size, MEM_LARGE)
          → hal_mem_alloc(total_size, MEM_LARGE)
            → mem_pool_alloc(g_external_mem_handle, size)
```

通过反汇编确认 `mg_calloc` 确实调用了 `buffer_calloc`：

```asm
9008e0b4 <mg_calloc>:
9008e0b4:	b580      	push	{r7, lr}
...
9008e0c2:	f779 fe28 	bl	90007d16 <buffer_calloc>
```

## 问题根因

1. **mongoose.h 配置问题**：
   - 定义了 `MG_ENABLE_CUSTOM_CALLOC 1`
   - 但自定义的 `mg_calloc`/`mg_free` 实现被注释掉
   - 导致链接时使用了错误的实现

2. **内存池初始化时序问题**（待确认）：
   - 可能 web server 在内存池初始化之前启动
   - `hal_mem_calloc` 返回 NULL
   - 某处有 fallback 到标准库 `calloc`

## 内存布局总结

| 区域 | 起始地址 | 大小 | 用途 |
|------|----------|------|------|
| AXISRAM1_2_S | 0x90000400 | 2535K | .text, .rodata, .data |
| SRAM_POOL | 0x34000000 | 1863K | .bss, heap, stack |
| PSRAM | 0x90800000 | 24M | .psram_section |
| internal_slab_buffer | 0x34032100 | 184K | 小内存分配池 |
| external_slab_buffer | 0x90854000 | 22M | 大内存分配池 |
| _user_heap_stack | 0x34060100 | 4.5K | 标准库堆栈 |

## 修复方案

### 方案 1：修复 mongoose.h 配置

```c
// 取消 MG_ENABLE_CUSTOM_CALLOC，使用 mongoose.c 中的默认实现
#undef MG_ENABLE_CUSTOM_CALLOC
#define MG_ENABLE_CUSTOM_CALLOC 0
```

mongoose.c 中的默认实现会使用 `buffer_calloc`：
```c
#if MG_ENABLE_CUSTOM_CALLOC
#else
void *mg_calloc(size_t count, size_t size) {
  return buffer_calloc(count, size);
}
#endif
```

### 方案 2：添加调试日志确认问题

在 `buffer_calloc` 中添加日志：
```c
void* buffer_calloc(size_t count, size_t size)
{
    void *ptr = hal_mem_calloc_large(count, size);
    if (ptr == NULL && count > 0 && size > 0) {
        printf("[BUFFER_CALLOC] FAILED: count=%u, size=%u\r\n", 
               (unsigned)count, (unsigned)size);
    }
    return ptr;
}
```

## 调试工具和命令

### 1. 查看函数地址对应的源码
```bash
arm-none-eabi-addr2line -e build/ne301_App.elf -f -C <address>
```

### 2. 查看符号表
```bash
arm-none-eabi-nm build/ne301_App.elf | grep <symbol>
```

### 3. 反汇编查看函数实现
```bash
arm-none-eabi-objdump -d build/ne301_App.elf | grep -A 20 "<function_name>:"
```

### 4. 查看内存段使用情况
```bash
arm-none-eabi-size build/ne301_App.elf
```

### 5. 比较 map 文件
```bash
diff map_good.map map_bad.map | head -50
```

## 相关文件

- `Custom/Common/Lib/mongoose/mongoose.h` - mongoose 配置
- `Custom/Common/Lib/mongoose/mongoose.c` - mongoose 实现
- `Custom/Core/Data/buffer_mgr.c` - 内存分配封装
- `Custom/Hal/mem.c` - HAL 内存管理
- `Custom/Hal/driver_core.c` - 驱动初始化顺序
- `Custom/Services/Web/web_server.c` - Web 服务
- `Appli/Core/Src/stm32n6xx_it.c` - 中断处理

## 后续待确认

1. 运行带调试日志的版本，确认是否有 `[BUFFER_CALLOC] FAILED` 输出
2. 如果有，说明内存池初始化时序问题，需要调整启动顺序
3. 如果没有，需要进一步排查内存覆盖的来源

## 经验总结

1. **MemManage fault 调试**：增强 handler 打印 MMFSR、PSP、栈帧寄存器非常有用
2. **函数指针损坏**：通常是内存越界写入导致，需要检查相邻的内存分配
3. **内存分配问题**：注意检查分配器的配置和初始化时序
4. **二分搜索**：定位问题文件时非常有效的方法
