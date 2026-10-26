# IR 板适配迁移方案（ir_ver → main）

> 状态：**已实施**（分支 `feat/ir-board-adapt`，App/FSBL/Web 三端编译通过）。
> 来源分支：`ir_ver`（本地/origin/ljh 同一分支，HEAD `f733bff`），merge-base 为 `6bf6757`。
> 目标：main 在**同一份固件**内同时支持标准板与 IR 板，板型由**工厂 NVS 硬件版本字段**运行时判定。

---

## 0. 实施状态（as-built）——与原方案的差异，以本节为准

实机调参过程中以下设计被演进/推翻，正文相应章节仅保留当时的决策脉络：

1. **FSBL 归位逻辑：最终完全移除**（推翻 §2.3/§3.9/§4/§6-F 的"保留 FSBL 归位 + 版本 bump"方案）。IRCUT 归位改由 App 侧 camera 组件完成，FSBL 与 main 逐字节一致，**无需 FSBL 配对升级**。
2. **IRCUT 归属 camera 组件**（推翻 §3.1 的 misc io 组方案）：IR-CUT（PE9）由 camera 组件拥有——其驱动芯片挂在相机传感器电源轨上，`camera_init` 获取电源后抢占 PE9，天然保证所有写入带电；控制走 `CAM_CMD_IRCUT_SET/GET`（0=白天/1=夜间，0xFF=板型不支持）。misc io 设备保持单一 ALARM 组（与标准板一致）。
3. **极性（实机验证，与 ir_ver 假设相反）：LOW = 白天（彩色），HIGH = 夜间（红外）**，为 camera.c 内部实现细节，对外只暴露语义接口。
4. **每次上电的归位边沿**：`cameraProcess` 在 `CAM_Init` 前写一次低（引脚经 strap 上拉与初始化配置常态为高，高→低即归位跳变），低电平贯穿传感器启动全程（天然 settle），`sem_init` 释放前完成——**系统默认落在白天模式，零新增启动耗时，每次上电仅一次滤光片动作**。
5. **SET 为纯电平跟踪**：异电平写入（真实切换），同电平跳过（无动作）。开发过程中的 settle 可调参数（`ircut time`）、异步定时器方案、verified 标志均已被更简单的最终设计取代并移除。
6. **启动即时评估**：AUTO 模式服务启动时做一次无防抖评估（时间窗/光敏阈值；ISP 统计源用光敏代理），消除"默认夜间 + 防抖锁定"导致的启动期模式错误；此后才交给轮询/确认/保持的防抖逻辑。
7. **快拍路径修复**：`dn_ircut_set` 需懒查找 camera 设备（睡眠唤醒时 device_service 未启动、指针为空），否则快拍 IRCUT 完全不写入。
8. **Web 预览黑屏修复**（实施引入又修复）：硬件管理页新增的板型探测 await 拖长了 loading，与"相机已运行时 preview/start 秒回"竞争导致 `<video>` 晚挂载、播放器未创建——探测改为非阻塞，取流等待视频元素就绪（`waitFor`）。
9. **HaLow 本地构建默认开启**：`NETIF_WIFI_HALOW_IS_ENABLE` 源码弱默认改为 1（CI 两个变体仍显式传参，不受影响）——与 IR 适配无关的顺手调整。

---

## 1. 迁移范围盘点

### 1.1 ir_ver 领先 main 的提交（7 个，其中有效 3 个）

| 提交 | 说明 | 迁移处理 |
|---|---|---|
| `44d1249` feat(ir): ir-cut 日夜自动切换 + ISP IQ 热切换 | 核心提交，40 文件 +2788/-778 | **迁移主体** |
| `c71822a` fix(ir): 夜间模式拍照后恢复 IR 灯供预览 | device_service.c 小修复（-14/+2） | 已含在 diff 中，一并迁移 |
| `02a4456` feat(pir): 预览时抑制 PIR 抓拍 | **已通过 `3cd1017` 进入 main**（`disable_in_preview`/`pir_dis_pv` 已存在） | **不迁移**（跳过） |
| 3 个 merge 提交 | 同步 main 到 ir_ver | 忽略 |

结论：迁移内容 = `git diff main...ir_ver` 全量（40 文件），其中 PIR 相关文件已不存在差异。

### 1.2 与 ir_ver 分支的关键差异（必须重做的部分）

**ir_ver 是"纯 IR 固件"，所有 IR 行为无条件生效；main 要求按板型条件化。** ir_ver 上没有任何运行时板型判定代码（已全文搜索确认），因此"检测 IR 板 + 条件注册设备"是本次迁移的**新增设计**，不是照搬。

另经确认：**ir_ver 分支面向测试板，正式 IR 版本硬件相对标准板仅有灯板差异**（白光 LED→红外 LED、外部指示灯→光敏、新增 IR-CUT/PE9）。因此 ir_ver 中与灯板无关的硬件层改动（WakeCore 引脚重映射、netif/CAT1 裁剪等）均按测试板残留处理，不迁移（详见 §3.8、§3.9）。

---

## 2. 板型检测设计（新增）

### 2.1 检测依据（沿用现有机制，零新增存储）

- 工厂产测写入 `NVS_FACTORY` 分区 `hw_version`（`factory_config_t.hw_version[16]`，见 `Custom/Core/Factory/factory_test.c:466`），同时同步到 `NVS_USER` 分区 `dev_info_hw_ver`。
- 运行时读 `NVS_USER/dev_info_hw_ver`（`json_config_nvs.c:1830` 一带已有读取逻辑），**大小写不敏感地匹配子串 "IR"**（如 `"V1.3-IR"`、`"ir"`）→ 判定为 IR 板；字段缺失/不含 IR → 标准板。
- 不使用 PE9 strap 判定 IR：PE9（`MM_BOARD_REV`）只区分 v1.2-/v1.3+ 硬件版本带（HaLow WAKE 选 PB2/PD9），与 IR 与否正交。

### 2.2 新增公共接口（建议放 `Custom/Hal/board_hw.c/h`）

```c
/* 首次调用读 NVS（dev_info_hw_ver，回退 NVS_FACTORY/hw_version）并缓存；
   NVS 不可达时按标准板处理并记日志。 */
aicam_bool_t board_is_ir(void);
```

时序依据：`driver_core_init()` 中 `storage_register()`（第 45 行）先于 `misc_register()`（第 52 行），因此 misc 注册时 NVS 已可用；睡眠快拍路径 `quick_snapshot` 本身通过 `quick_storage` 直读 NVS，同样可用。

### 2.3 PE9 引脚时序与 FSBL 提前控制【最终：FSBL 方案被推翻，见 §0-1/§0-4，归位由 App camera 组件完成】

main 上 PE9 启动时被 `board_hw_init()`（`driver_core_init()` 末尾，第 94 行）配置为下拉输入采样一次判定 v1.2/v1.3+，随后 DeInit 释放。ir_ver 在 **FSBL** 里把 PE9 配成 IR-CUT 推挽输出并在拷贝 App 前置高（=白天）。

**先更正本方案早期版本的一个误判**：FSBL 把 PE9 驱动为高**不会**导致 v1.2 标准板被误判为 v1.3+——`board_hw_detect()` 采样前会先 `HAL_GPIO_Init(PE9, GPIO_MODE_INPUT, GPIO_PULLDOWN)`，输入模式电气上断开 FSBL 留下的输出驱动器，采样电平只由板上 strap 网络与内部下拉决定，与引脚此前是否被驱动无关。

**FSBL 提前控制 PE9 保留（已确认）**，目的：IR-CUT 滤光片从启动最早期——包括深睡唤醒（standby 复位后 FSBL 重新运行）——就处于确定的"白天"状态，不依赖 strap 弱上拉保持。迁移 FSBL 改动：`main.h` IR_CUT_A 宏、`gpio.c` GPIOE 时钟 + PE9 初始化、`main.c` 拷贝 App 前置高。FSBL 读不到 NVS，**无条件执行**。

**FSBL 序列按 ir_ver 原样迁移（已确认）**：`MX_GPIO_Init` 先写低再配输出，`main()` 拷贝 App 前写高——FSBL 外设初始化阶段的短暂低电平窗口（约几~几十 ms）已确认为可接受，不做"初始即写高"的偏离；硬性要求只有一条：**`BOOT_Copy_Application()` 执行前 PE9 必须已稳定为高（白天）**，保证 App 从第一行代码起滤光片就处于白天态。

**App 侧重配为输入不会引发额外切换**：`board_hw_detect()` 将 PE9 重配为输入+内部下拉的采样窗口内，电平由外部 strap 上拉与内部下拉分压决定（v1.3+ 板带检测要求外部上拉存在且已量产验证，分压约 2.6~3.0V，仍为逻辑高、高于 IR-CUT 驱动模块输入阈值）；`HAL_GPIO_DeInit` 转模拟态后仅剩外部上拉，稳态 3.3V。即从 FSBL 置高之后，滤光片在 App 全程（采样窗口/模拟态间隙/misc 接管）始终看到"白天"电平，不会再切换。实施时需在 IR-CUT 驱动模块手册上确认其输入阈值（典型 ≤2V@3.3V）低于分压值。

各板型下的安全性（FSBL 无条件置高的影响）：

- **IR 板**：即目的行为。
- **标准 v1.3+ 板**：strap 本就将 PE9 拉高，FSBL 驱动到同一电平无冲突；灯板连接器上的 IR-CUT 引脚未贴器件，无负载。
- **标准 v1.2 板**：PE9 空接或外部下拉 strap。若为下拉 strap，FSBL 驱动为高的窗口（FSBL 起点至 App 侧 `board_hw_init()` 重配输入为止）存在 ~0.3mA 级静态电流；`board_hw_init()` 前移后该窗口缩至 ms 级。需硬件侧确认 v1.2 板 PE9 是否空接（若是则完全无害）。

配套时序约束：

- App `MX_GPIO_Init` 仍**不**配置 PE9（该时刻读不到 NVS 无法条件化，且 FSBL 已提供确定态）；
- `board_hw_init()` 前移到 `driver_core_init()` 开头（`storage_register()` 之后）——缩短 v1.2 板上的驱动窗口，并让 misc 的 io 设备尽早接管 PE9；
- misc `io_groups` 的 IR_CUT 组仍**仅 IR 板注册**（标准板 App 侧不占用 PE9）；
- `SystemIsolation_Config` 的 PE9 RIF attribute 配置保留（标准板无害，IR 板 io 接管需要）；
- 采样窗口（~5ms 输入态）与 DeInit 后至 misc 接管前的间隙由 strap 上拉维持白天电平（v1.3+/IR 板 strap 必然存在，否则带检测本身不成立）。

**代价（随方案接受）**：FSBL 恢复有代码改动 → `version.mk FSBL_VERSION_OVERRIDE`、`FSBL/Core/Inc/fsbl_version.h`、`EXPECTED_FSBL_VERSION` 三处需同步 bump；现场 OTA 必须先升 FSBL 再升 App（参照 docs OTA 指南的 fsbl 前置升级步骤），FSBL 与 App 配对发布。

FSBL 的 `PWR_SENSOR_ON`(PF9) 提前上电**不保留**：App `MX_GPIO_Init` 会在早期把 PF9 写回 RESET 并配置为输出（`Appli/Core/Src/gpio.c:63/125`，传感器上电由 App/pwr 驱动管理），FSBL 的提前上电随即被覆盖，收益不成立；如需缩短启动出图时间，应连同 App 侧时序另行评估。

### 2.4 标准板零回归不变式（本方案的硬性约束）

**原则：IR 相关一切功能仅在 `board_is_ir()` 为真时启用；标准板保留 main 现有全部功能与业务逻辑，行为零改动。** 实施与验收均以下表为准，任何一条不满足即视为实现缺陷：

| # | 项目 | 标准板（board_is_ir()==false）必须保持 |
|---|---|---|
| 1 | 设备注册 | ind_ext（外部指示灯）注册、light（光敏）**不**注册、io_groups **无** IR_CUT 组——与 main 现状完全一致 |
| 2 | PE9 | App 侧全程不驱动（misc 的 IR_CUT 组仅 IR 板注册）；FSBL 无条件将 PE9 置为白天电平——对标准板无功能影响（§2.3）；strap 采样判定结果与 main 一致（HaLow WAKE 引脚选择不变） |
| 3 | 指示灯 | `IND_EXT_DEVICE_NAME`（device_service、wifi.c），与 main 一致 |
| 4 | 补光灯 PWM | TIM3 PSC=15/ARR=1599（1MHz 调光），`TIM_set_duty` 行为与 main 一致 |
| 5 | 灯光业务 | `apply_light_control`/`fill_light_while_streaming`/`fill_light_sync` 任务/灯光页全部字段——main 现逻辑原样 |
| 6 | 快拍灯光 | `QS_LIGHT_MODE_*` 含 CUSTOM 时间窗——main 现逻辑原样；`device_service_day_night_capture_prepare` 直接短路返回 day、不介入灯光 |
| 7 | 日夜任务 | dayNightTask **不创建**；IR-CUT/IR 灯控制函数静默空操作 |
| 8 | 日夜 API/CLI | `GET /device/day-night/*` 可用但 `status.configured=false`；`POST`（配置/模式）返回 `AICAM_ERROR_NOT_SUPPORTED`；`ir/ircut/daynight` CLI 同样报不支持 |
| 9 | isp_mode 校验 | 标准板合法集 `OUTDOOR(0)/INDOOR(1)/CUSTOM(255)` 与 main 完全一致；`NIGHT/AUTO` 仅 IR 板可设，标准板 set 返回 `NOT_SUPPORTED` |
| 10 | 画质 | 标准板三条 IQ 路径（outdoor 基底 / indoor ROI / NVS 自定义 IQ）原样保留；NIGHT/AUTO 分支仅 IR 板可达 |

标准板上**唯一**的用户可见简化：grayscale 开关随决策点 A 全局删除（已批准）。ISP 模式选择器在标准板保留室内/室外/自定义三选项（决策点 B 更正），不收敛为 DAY。

> 其余所有 ir_ver 引入的代码路径（day/night 配置模型、NVS key、IQ 热切换、API/Web 页面）在标准板上"存在但休眠"：配置结构可存取（为 OTA 跨板型升级留兼容），但没有任何运行时效果。

---

## 3. 按模块的迁移明细

### 3.1 灯板设备层（`Custom/Hal/misc.c/h`）——核心条件化点【最终：IR_CUT 不走 misc io 组，见 §0-2】

| ir_ver 改动 | main 迁移方式 |
|---|---|
| 注释掉 `ind_ext_register()`（外部指示灯 PF3 与光敏 ADC1_IN16 冲突） | **条件化**：`board_is_ir()` 真→不注册 ind_ext、注册 light（光敏）；假→维持 main 现状（注册 ind_ext、不注册 light） |
| `light_register()` 恢复启用 | 同上，仅 IR 板 |
| io_groups 新增 IR_CUT（PE9，高=白天） | 仅 IR 板注册该组 |
| `ind_ext_register/unregister` 加 `__attribute__((unused))` | 保留（标准板仍编译该函数） |
| 光敏 `light_get_value` 去掉 pwr_manager 控电、电压×2、去掉 osDelay(1000) | 照搬（该代码路径仅 IR 板可达） |
| `MISC_CMD_PWM_GET_DUTY` 新增、SET_DUTY 限幅 100 | 照搬（通用增强，对标准板无行为差异：`TIM_set_duty` 本就限幅 100，GET_DUTY 为新增只读命令） |

**指示灯切换（用户需求核心）**：ir_ver 将 `device_service.c` 两处 `device_find_pattern(IND_EXT_DEVICE_NAME)` 改为 `IND_DEVICE_NAME`（1451、3034 行一带）。main 迁移为**优先级查找**：

```
board_is_ir() ? IND_DEVICE_NAME : IND_EXT_DEVICE_NAME
```

涉及：`device_service.c`（led_device 初始化 + led_blink 懒查找）、`Custom/Hal/wifi.c:682`（wifi 状态指示灯同样切换）、`cli_cmd.c` led 命令（保留 0/1 索引，但索引 1 在 IR 板上查不到设备时报"not found"即可）。

### 3.2 补光灯/IR 灯 PWM（`Appli/Core/Src/tim.c`）

ir_ver 将 TIM3 PWM 从 1MHz/16000 级（PSC=15, ARR=1599，白光 LED 调光）改为 8MHz/200 级（PSC=0, ARR=199，IR LED 占空比）。**这是 IR 灯板的电气特性差异**：
- 方案：按板型条件设置——`MX_TIM3_Init` 后、或首次 `TIM_set_duty` 前，`board_is_ir()` 为真时重配 PSC/ARR 为 0/199，否则维持 main 现值 15/1599。`TIM_set_duty` 内按当前 ARR 计算比较值（新增一个全局或读取 `__HAL_TIM_GET_AUTORELOAD`）。
- 注：标准板白光与 IR 板红外 LED 的 PWM 频率/分辨率要求不同，硬编码单值无法两者兼顾，必须条件化。

### 3.3 日夜切换服务（`device_service.c/h`，ir_ver 主体 +560 行）

代码照搬但**入口全部按板型门控**（满足 §2.4 不变式第 7/9 条）：

- `day_night_config_t` 配置模型 + NVS 持久化（18 个 `dn_*` key）——两板均可存取（休眠无害）；
- `dayNightTask` 周期评估（TIME / LIGHT_SENSOR / ISP_STATS 三源，滞回 + EMA）——**仅 IR 板创建**，标准板不建任务；
- `device_service_day_night_set_config/set_mode`——标准板返回 `AICAM_ERROR_NOT_SUPPORTED`（get/status 正常，`configured=false`）；
- `dn_ircut_set` / `dn_ir_light_set` / `day_night_apply`——IR-CUT 设备不存在时静默返回（标准板天然空操作）；
- `device_service_day_night_get_config / get_status / capture_prepare`（睡眠快拍快路径复用）——标准板上 `capture_prepare` 短路返回 day 且不介入灯光（§2.4 第 6 条）；
- 抓拍路径 `device_service_camera_capture*` 的 IR 灯开关时序（含 `c71822a` 的夜间拍照后恢复修复）——仅 IR 板生效，标准板抓拍灯光时序维持 main 现逻辑。

**与 main 新特性的融合点（冲突解决策略）**：
- main 新增了 `fill_light_while_streaming` + `fill_light_sync` 常驻任务（预览补光同步，device_service.c 746 行一带）。ir_ver 无此机制且改写了 `apply_light_control`/抓拍灯光时序。融合原则：**标准板走 main 的 fill_light 逻辑不动；IR 板由 dayNightTask+IR 灯接管**，`apply_light_control` 内按 `board_is_ir()` 分流，避免两套灯控同时操作 PWM。

### 3.4 ISP 层（`camera.c/h`、`isp_param_conf.h`）

| ir_ver 改动 | main 迁移方式 |
|---|---|
| `CAM_IQ_SCENE_INDOOR/OUTDOOR` → `DAY(0)/NIGHT(2)`，全局替换枚举 | **不采用 ir_ver 的全局换枚举（决策点 B 更正）**：main 枚举 `IMAGE_ISP_MODE_OUTDOOR(0)/INDOOR(1)/CUSTOM(255)` 数值与语义原样保留，标准板三条 IQ 路径（outdoor 基底、indoor ROI X0=109/Y0=92、CUSTOM 套用 NVS 自定义 IQ）全部不动；在其上**新增 IR 专用值 `NIGHT=2`、`AUTO=3`**，并定义 `IMAGE_ISP_MODE_DAY` 为 `OUTDOOR(0)` 的别名供 IR 代码路径使用。ir_ver 的 NIGHT=1/AUTO=2 编码弃用（1 与 INDOOR 撞值）。`cam_iq_scene_t` 同理：保留 INDOOR/OUTDOOR，新增 NIGHT。IR 板 DAY 与标准板 outdoor 共用 **main #53 最新基底 profile**（决策点 D，ir_ver 对基底的重调值 exposure 5779/BLC 16/对比度曲线/AWB 表等**不采用**） |
| 新增 `camera_apply_isp_iq_runtime` + `CAM_CMD_APPLY_ISP_IQ`（运行中热切换 IQ，不重启管线） | 照搬（通用能力）。注意 main 已有 `5fbe957` "defer dcmipp pipe gamma to isp service" 等新 ISP 逻辑，热切换函数需与其对齐（gamma 部分两边都动过） |
| 删除 grayscale（`camera_apply_grayscale_iq` 等） | **已定（决策点 A）**：全链路删除——`camera_apply_grayscale_iq`/`camera_configure_pipe1_grayscale`、`image_config_t.grayscale` 字段、NVS key `img_gray`、JSON 字段、Web 开关一并移除；老配置 gray=on 升级后灰度不再生效（预期行为），NVS 残留 key 随下次写入/恢复出厂清理 |
| NIGHT profile `ISP_IQParamCacheInit_OS04C10_NIGHT` | 照搬（`__attribute__((unused))` 已处理非 OS04C10 场景），仅 `CAM_IQ_SCENE_NIGHT`（IR 板）可达 |

> 附注：标准板 `CUSTOM(255)` 套用 NVS 自定义 IQ 的现状路径与 `isp_config_t`/`isp_valid` 机制**完整保留**；`quick_storage_fill_isp_iq_param` 内按板型分流——标准板走 main 现逻辑（indoor ROI / CUSTOM→NVS IQ），IR 板走 DAY/NIGHT 二选一（ir_ver 的"废弃 CUSTOM"仅适用于 IR 分支）。

### 3.5 配置层（`json_config_*`、`quick_storage`）

- `IMAGE_ISP_MODE_OUTDOOR(0)/INDOOR(1)/CUSTOM(255)` 枚举原样保留，新增 IR 专用 `NIGHT=2`、`AUTO=3`（`IMAGE_ISP_MODE_DAY` 为 `OUTDOOR(0)` 别名）；`image_config_t.grayscale` 字段删除；`device_service_config_t` 新增 `day_night_config`。
- **NVS 旧值兼容（决策点 B 更正后大幅简化）**：main 枚举数值 `OUTDOOR(0)/INDOOR(1)/CUSTOM(255)` 原样保留、`NIGHT=2/AUTO=3` 为全新值，因此**标准板老配置零迁移**——数值与语义均不变（ir_ver 方案里 `INDOOR(1)` 与 `NIGHT(1)` 撞值、`CUSTOM(255)` 非法化的坑全部不存在）。加载/写入时按板型校验合法集（标准板 {0,1,255}，IR 板 {0,2,3}），非法值回退默认 0。ir_ver 测试板固件写入的 NIGHT=1/AUTO=2 在正式固件下属非法、回退 DAY（测试板不承诺兼容）。同时丢弃/忽略 `img_gray` 残留 key（决策点 A）。
- `quick_storage`：`QS_IMAGE_ISP_MODE_*` 同步保留 0/1/255 并新增 NIGHT/AUTO、`quick_storage_fill_isp_iq_param` 去 grayscale 参数并**按板型分流**（标准板 main 现逻辑 / IR 板 DAY-NIGHT 二选一）、新增 `quick_storage_read_day_night_config`（睡眠快拍路径不初始化 json_config，直读 NVS）。
- `quick_snapshot`：抓拍前 `device_service_day_night_capture_prepare` 评估生效模式（AUTO 时 TIME/LIGHT_SENSOR 可评、ISP_STATS 在起流前不可评→按 day）、切 IR-CUT、按 `ir_brightness` 点 IR 灯。**已定（决策点 B）**：标准板**保留** main 现有 `QS_LIGHT_MODE_CUSTOM` 白光时间窗等 light_mode 逻辑不动，`capture_prepare` 在 `board_is_ir()` 为假时直接返回 day 且不介入灯光；IR 板的日夜切换本质上就是"夜间模式"判定，取代灯光时间窗逻辑（时间窗改由 day/night 配置的 TIME 源承担）。

### 3.6 HTTP API（`api_device_module.c`）

- 新增 `GET/POST /device/day-night/config`、`GET /device/day-night/status` 三个路由 + handler：照搬，并按 §2.4 第 8 条加板型门控——标准板 GET 正常返回（`configured=false`）、POST 返回 not supported 错误。
- `device_image_config_handler`/`device_camera_config_handler`：删除 grayscale 字段（决策点 A）；isp_mode 合法集按板型区分（§2.4 第 9 条）——标准板 outdoor/indoor/custom 语义不变，IR 板 day(=outdoor)/night/auto。
- 建议补充（ir_ver 未做）：`day-night/status` 返回值已含 `configured` 字段，Web 端用它区分板型即可，API 不必暴露 board_is_ir。也可在 device_info 里加 `board_variant: "ir"|"standard"` 方便排查（可选）。

### 3.7 Web（前端）

- 新增 `day-night-auto.tsx`（日夜自动配置组件 327 行）：照搬；**仅 IR 板展示该配置入口**（标准板 `configured=false` 时隐藏或整页置灰，与后端 POST 拒绝保持一致）。
- `graphics.tsx`：ISP 模式选择器**按板型分流**——标准板保留 室内/室外/自定义 三选项（main 现状，含 NVS 自定义 IQ）；IR 板展示 DAY(室外)/NIGHT/AUTO（无室内/自定义）。选项集依据 `day-night/status.configured` 判定；同时移除 grayscale 开关（决策点 A，全局）。
- `light.tsx` 大幅精简（-202 行，IR 板灯光页并入日夜页）：**已定（决策点 B）按板型条件化**——标准板保留 main 现有灯光页全部字段（白光亮度/模式/时间窗/自动触发/阈值）不动；IR 板展示 ir_ver 精简版 + IR 亮度，原灯光页的"时间窗"字段语义由日夜页 TIME 源的时段字段（`dn_t_sh` 等）承接更新。渲染分支依据 `day-night/status.configured`（或新增 board_variant 字段）切换。
- locales（zh/en sys.json +47 行）、`hardware-management.ts` API 类型：照搬，并按上述字段语义更新补齐文案；`reload.svg`/`package.json`：reload.svg 可顺手带过，版本号按 main 版本策略重新定，不取 ir_ver 的 .116。

### 3.8 WakeCore（STM32U0 唤醒 MCU）——已确认：不迁移

ir_ver 对 WakeCore 做了引脚重映射（`PIR_TRIGGER PA1→PA2`、`PIR_SERIAL PA3→PB0`、弃用 `NET_WKUP(PA2)`/`PWR_EXT(PB0)`、PIR 待机唤醒线 WUF3→WUF4）。经确认：**ir_ver 分支是给测试板验证用的，正式 IR 版本硬件相对标准板仅有灯板差异，唤醒 MCU 引脚不变**。

处理：WakeCore 相关 4 文件（`main.h`/`gpio.c`/`stm32u0xx_it.c`/`pwr_manager.c`）及 `sys_config.h` 版本号**一律不迁移**，main 现有 WakeCore 代码对标准板与正式 IR 板通用，深睡唤醒/PIR 路径无需任何板型区分。

### 3.9 测试板残留（建议不迁移）

结合已确认的背景——**ir_ver 分支面向测试板，正式 IR 版本硬件相对标准板仅有灯板差异**——以下改动均与灯板无关，基本可断定为测试板调试残留，不迁移：

| 改动 | 判断 |
|---|---|
| `netif_manager.h`：CAT1/USB_ECM/HaLow 三个 `*_IS_ENABLE` 全部置 0 | 与灯板无关，测试板调机时为加速启动临时关闭；main 近期刚移植 wiseconnect 4.2.0 WiFi，关闭会砍功能。**不迁移**（NETIF 开关是编译期宏，本就无法按板型条件化） |
| `pwr.c` CAT1 电源控制注释掉 + `Appli/gpio.c` 去掉 `PWR_CAT1_ON_Pin`(PG8) 初始化 | 同上，**不迁移**（正式 IR 板 CAT1 电源脚与标准板相同） |
| FSBL `PWR_SENSOR_ON`(PF9) 提前上电 | **不迁移**：App `MX_GPIO_Init` 早期即把 PF9 写回 RESET（gpio.c:63/125），FSBL 提前上电被覆盖、收益不成立（§2.3） |
| WakeCore 引脚重映射 + `sys_config.h` 版本号 | 见 §3.8，**不迁移** |
| `version.mk` `.115` 版本号、`VERSION_SUFFIX := ir_ver` | `.115`/suffix 数值不迁移、统一按 main 版本节奏；但因 FSBL 恢复 PE9 改动（§2.3），`FSBL_VERSION_OVERRIDE`/`fsbl_version.h`/`EXPECTED_FSBL_VERSION` 需按 main 流程同步 bump 新版本号，OTA 需 FSBL 前置升级 |
| `Web/package.json`/`reload.svg` | 版本号不迁移；reload.svg 是 2 字节格式差异，顺手带过或丢弃均可 |

---

## 4. 建议实施顺序（每步可独立编译验证）

1. **板型检测基础设施**：`board_is_ir()` + `board_hw_init()` 前移 + PE9 时序保障 + 单测（CLI 命令打印板型，如 `board` 命令扩展）。
2. **配置层**：保留 0/1/255 枚举并新增 NIGHT/AUTO、day_night 配置结构/NVS key、按板型的合法集校验（标准板零迁移）+ quick_storage 分流对齐（此步完成后全工程编译报错点即灰度清理清单）。
3. **设备层**：misc.c 条件注册（ind_ext/light/IR_CUT）+ tim.c PWM 条件化 + 指示灯查找切换（device_service/wifi.c）。
4. **camera/ISP 层**：标准板三场景逻辑不动；IR 分支 DAY 共用 main #53 现行基底 profile（不取 ir_ver 重调值）、NIGHT 引入 ir_ver profile、`CAM_CMD_APPLY_ISP_IQ` 热切换、grayscale 全链路删除（决策点 A/B/D）。
5. **device_service 日夜切换**：dayNightTask + 抓拍灯光时序 + 与 fill_light_sync 的板型分流。
6. **API + Web**：三个新端点 + 前端页面（含标准板/IR 板 UI 分支）。
7. **FSBL/WakeCore**：WakeCore 不改（测试板改动）；FSBL 保留 PE9 提前控制——`main.h`/`gpio.c`/`main.c` 三处迁移 + `version.mk`/`fsbl_version.h`/`EXPECTED_FSBL_VERSION` 三处版本同步 bump + 发布说明标注 OTA 需 FSBL 前置升级（§2.3）。
8. **回归验证**（§5）。

## 5. 测试要点

- **标准板（无 ir 的 hw_version）**：逐条核对 §2.4 不变式 1–10——外部指示灯、白光 PWM（1MHz 调光）、灯光页全字段、`fill_light_while_streaming`、快拍 `QS_LIGHT_MODE_CUSTOM` 时间窗、**ISP 室内/室外/自定义三模式（indoor ROI 生效、CUSTOM 套用 NVS 自定义 IQ）**均与迁移前完全一致；day/night POST 与 `isp_mode=NIGHT/AUTO` 被拒绝；dayNightTask 未创建（内存/线程数与迁移前一致）；PE9 strap 仍正确区分 v1.2/v1.3+（HaLow WAKE 脚）。**用户可见差异仅剩已批准的 A（grayscale 移除）一处。**
- **IR 板（hw_version 含 ir/IR）**：板载指示灯工作、外部指示灯设备不存在、光敏百分比可读、IR-CUT 随 DAY/NIGHT 切换（高=彩色/低=红外，PE9）、NIGHT 画质用 NIGHT IQ、IR 亮度 0-100 生效、三源自切换（时间窗/光敏阈值/ISP 统计滞回+EMA）、睡眠快拍按 NVS 配置点亮 IR 灯并恢复 IR-CUT、深睡-唤醒路径 PIR 正常；ISP 选择器仅 DAY(室外)/NIGHT/AUTO。
- **兼容性**：标准板老 NVS 的 `img_isp_mode`（0/1/255）**零迁移**、语义不变；`img_gray` 忽略（决策点 A，老配置灰度不再生效应为预期行为）；ir_ver 测试板固件的 NIGHT=1/AUTO=2 旧值回退 DAY（测试板不承诺兼容）。Web 老页面缓存清后正常。
- **标准板快拍白光时间窗**（决策点 B）：`QS_LIGHT_MODE_CUSTOM` 时段开灯行为与迁移前一致。
- **边界**：hw_version 字段为空（产测未写）→ 标准板；同时插 UVC/多传感器场景 ISP 热切换无崩溃。
- **FSBL/PE9（§2.3）**：IR 板冷启动与深睡唤醒后，启动早期 IR-CUT 即处于白天状态——逻辑分析仪抓 PE9：`BOOT_Copy_Application()` 前已稳定为高，且此后（App 全程）无下降沿；FSBL 初期的短暂低电平窗口为已确认可接受行为，不需消除；标准板 v1.2 与 v1.3+ 的 strap 判定不受 FSBL PE9 驱动影响（board_hw 日志核对 HaLow WAKE 引脚分别为 PB2/PD9）；FSBL/App 版本配对升级与单独升 App（旧 FSBL）两种路径验证 OTA 兼容。

## 6. 决策点汇总（已全部关闭；F 的结论后经实机推翻，最终以 §0 为准）

| # | 问题 | 结论 |
|---|---|---|
| A | 标准板 grayscale 灰度功能保留？ | **已确认：随 ir_ver 全链路删除**（函数/字段/NVS key/Web 开关），老配置升级后灰度不再生效 |
| B | ISP 模式语义与标准板灯光逻辑 | **已确认（含更正）**：①标准板快拍保留 main 现有白光时间窗逻辑；②**标准板 ISP 模式保留 室内/室外/自定义 原语义与数值（0/1/255），零迁移**；③仅 IR 板收敛为 室外(=DAY)+NIGHT(+AUTO 自动切换)，IR 板的日夜切换即"夜间模式"逻辑，Web 灯光页时间窗字段由日夜页 TIME 源字段承接 |
| C | WakeCore 引脚重映射归属 | **已确认**：ir_ver 为测试板分支，正式 IR 硬件无此改动，不迁移（§3.8） |
| D | OS04C10 画质取值 | **已确认**：日间保留 main #53 最新调优值——标准板 outdoor/indoor 路径原样不动，IR 板 DAY 共用同款基底 profile（ir_ver 重调值不采用）；夜间按 ir 分支的 OS04C10_NIGHT profile 原样 |
| E | netif 关闭/CAT1 移除是否真实硬件差异 | 结合"正式 IR 硬件仅灯板差异"已基本可断定为测试板残留，**不迁移**（§3.9） |
| F | FSBL 提前控制 PE9（IR-CUT 确定态）是否保留 | 评审时确认保留，**实施后期推翻**：App camera 组件归位更可靠且免 FSBL 升级（§0-1/§0-4） |
