# PIR传感器配置 Web API 文档

## 概述

PIR传感器配置API提供对PIR（被动红外）传感器的配置功能，包括传感器参数设置和触发器启用/禁用。配置会持久化存储到NVS。

**Base URL:** `/api/v1`  
**认证:** 所有接口需要认证 (Authorization Header)

---

## API 端点

PIR传感器配置已整合到工作模式触发器配置中，通过以下接口访问：

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/api/v1/work-mode/triggers` | 获取所有触发器配置（包括PIR） |
| POST | `/api/v1/work-mode/triggers` | 设置触发器配置（包括PIR） |

---

## 1. 获取PIR传感器配置

通过获取工作模式触发器配置接口获取PIR传感器配置。

### 请求

```
GET /api/v1/work-mode/triggers
Authorization: Bearer <token>
```

### 响应

**成功 (200 OK)**

```json
{
  "code": 0,
  "message": "Work mode triggers retrieved successfully",
  "data": {
    "timer_trigger": {
      "enable": true,
      "capture_mode": "interval",
      "interval_sec": 300,
      "time_node_count": 0,
      "time_node": [],
      "weekdays": []
    },
    "pir_trigger": {
      "enable": true,
      "trigger_type": "rising_edge",
      "sensitivity_level": 30,
      "ignore_time_s": 7,
      "pulse_count": 2,
      "window_time_s": 0
    },
    "remote_trigger": {
      "enable": false
    }
  }
}
```

### PIR配置字段说明

| 字段 | 类型 | 范围/选项 | 描述 | 默认值 |
|------|------|----------|------|--------|
| `enable` | boolean | - | PIR触发器是否启用 | `false` |
| `trigger_type` | string | `"rising_edge"`, `"falling_edge"`, `"high_level"`, `"low_level"`, `"both_edges"` | PIR唤醒触发方式 | `"rising_edge"` |
| `sensitivity_level` | number | 10-255 | 灵敏度级别，值越小越灵敏（推荐>30） | `30` |
| `ignore_time_s` | number | 0-15 | 忽略时间寄存器值，实际时间 = 0.5 + 0.5 × 值（秒） | `7` (4秒) |
| `pulse_count` | number | 1-4 | 脉冲计数，实际脉冲数 = 值 | `1` (2个脉冲) |
| `window_time_s` | number | 0-3 | 窗口时间寄存器值，实际时间 = 2 + 2 × 值（秒） | `0` (2秒) |

**参数详细说明：**

- **trigger_type (触发方式)**
  - `"rising_edge"`: 上升沿触发（检测到运动时唤醒）- **推荐，最常用**
  - `"falling_edge"`: 下降沿触发（运动结束时唤醒）
  - `"high_level"`: 高电平触发（PIR信号为高时唤醒）
  - `"low_level"`: 低电平触发（PIR信号为低时唤醒）
  - `"both_edges"`: 双边沿触发（实际使用上升沿）
  - 默认值：`"rising_edge"`

- **sensitivity_level (灵敏度级别)**
  - 范围：10-255
  - 推荐值：>30
  - 值越小越灵敏，但容易误触发
  - 无干扰环境可设置最小值10

- **ignore_time_s (忽略时间)**
  - 范围：0-15
  - 实际忽略时间 = 0.5 + 0.5 × 值（秒）
  - 例如：值7 = 4秒，值15 = 8秒
  - 中断输出切换回0后忽略运动检测的时间

- **pulse_count (脉冲计数)**
  - 范围：1-4
  - 实际脉冲数 = 值
  - 窗口时间内需要达到的脉冲数
  - 值越大抗干扰能力越强，但灵敏度略有降低

- **window_time_s (窗口时间)**
  - 范围：0-3
  - 实际窗口时间 = 2 + 2 × 值（秒）
  - 例如：值0 = 2秒，值3 = 8秒
  - 脉冲计数的统计窗口时间

---

## 2. 设置PIR传感器配置

通过设置工作模式触发器配置接口更新PIR传感器配置。

### 请求

```
POST /api/v1/work-mode/triggers
Content-Type: application/json
Authorization: Bearer <token>
```

### 请求体

所有字段均为可选，仅更新提供的字段。可以只更新PIR配置，其他触发器配置保持不变。

```json
{
  "pir_trigger": {
    "enable": true,
    "trigger_type": "rising_edge",
    "sensitivity_level": 35,
    "ignore_time_s": 5,
    "pulse_count": 2,
    "window_time_s": 1
  }
}
```

### 请求字段验证

| 字段 | 验证规则 |
|------|---------|
| `enable` | boolean类型 |
| `trigger_type` | 字符串类型，必须是有效的触发方式（见上方选项），无效值将使用默认值"rising_edge" |
| `sensitivity_level` | 10-255之间的整数，超出范围将被忽略 |
| `ignore_time_s` | 0-15之间的整数，超出范围将被忽略 |
| `pulse_count` | 1-4之间的整数，超出范围将被忽略 |
| `window_time_s` | 0-3之间的整数，超出范围将被忽略 |

### 响应

**成功 (200 OK)**

```json
{
  "code": 0,
  "message": "Image mode triggers configured successfully"
}
```

**错误 (400 Bad Request)**

```json
{
  "code": 400,
  "message": "Invalid JSON"
}
```

**错误 (500 Internal Server Error)**

```json
{
  "code": 500,
  "message": "Failed to configure image mode triggers"
}
```

---

## 3. 配置应用说明

### 配置生效时机

1. **系统启动时**：如果PIR触发器启用，系统会自动使用配置的参数初始化PIR传感器
2. **配置更新时**：通过API更新配置后，如果PIR触发器启用，配置会立即应用到PIR传感器
3. **进入睡眠前**：系统进入睡眠模式前，会使用当前配置的参数配置PIR传感器

### 配置持久化

- 所有配置参数会自动保存到NVS（非易失性存储）
- 系统重启后配置会自动恢复
- 配置同时保存到JSON配置文件

### 默认值

如果某些参数未配置或为0，系统会使用以下默认值：

- `sensitivity_level`: 30
- `ignore_time_s`: 7 (实际4秒)
- `pulse_count`: 1 (实际2个脉冲)
- `window_time_s`: 0 (实际2秒)

---

## 4. 使用示例

### JavaScript/TypeScript 示例

```typescript
// 获取PIR配置
async function getPIRConfig() {
  const response = await fetch('/api/v1/work-mode/triggers', {
    method: 'GET',
    headers: {
      'Authorization': `Bearer ${token}`
    }
  });
  
  const data = await response.json();
  if (data.code === 0) {
    const pirConfig = data.data.pir_trigger;
    console.log('PIR配置:', pirConfig);
    return pirConfig;
  }
}

// 设置PIR配置
async function setPIRConfig(config: {
  enable: boolean;
  trigger_type?: string;  // "rising_edge" | "falling_edge" | "high_level" | "low_level" | "both_edges"
  sensitivity_level?: number;
  ignore_time_s?: number;
  pulse_count?: number;
  window_time_s?: number;
}) {
  const response = await fetch('/api/v1/work-mode/triggers', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${token}`
    },
    body: JSON.stringify({
      pir_trigger: config
    })
  });
  
  const data = await response.json();
  if (data.code === 0) {
    console.log('PIR配置更新成功');
    return true;
  } else {
    console.error('配置更新失败:', data.message);
    return false;
  }
}

// 使用示例
async function configurePIR() {
  // 启用PIR并设置参数
  await setPIRConfig({
    enable: true,
    trigger_type: "rising_edge",  // 上升沿触发（检测到运动时唤醒）
    sensitivity_level: 35,    // 中等灵敏度
    ignore_time_s: 5,         // 3秒忽略时间
    pulse_count: 2,           // 2个脉冲
    window_time_s: 1          // 4秒窗口时间
  });
  
  // 获取当前配置
  const currentConfig = await getPIRConfig();
  console.log('当前PIR配置:', currentConfig);
}
```

### React 组件示例

```tsx
import React, { useState, useEffect } from 'react';

interface PIRConfig {
  enable: boolean;
  trigger_type: string;  // "rising_edge" | "falling_edge" | "high_level" | "low_level" | "both_edges"
  sensitivity_level: number;
  ignore_time_s: number;
  pulse_count: number;
  window_time_s: number;
}

function PIRConfigComponent() {
  const [config, setConfig] = useState<PIRConfig>({
    enable: false,
    trigger_type: "rising_edge",
    sensitivity_level: 30,
    ignore_time_s: 7,
    pulse_count: 1,
    window_time_s: 0
  });
  const [loading, setLoading] = useState(false);

  // 加载配置
  useEffect(() => {
    loadConfig();
  }, []);

  const loadConfig = async () => {
    try {
      const response = await fetch('/api/v1/work-mode/triggers', {
        headers: { 'Authorization': `Bearer ${token}` }
      });
      const data = await response.json();
      if (data.code === 0 && data.data.pir_trigger) {
        setConfig(data.data.pir_trigger);
      }
    } catch (error) {
      console.error('加载配置失败:', error);
    }
  };

  const saveConfig = async () => {
    setLoading(true);
    try {
      const response = await fetch('/api/v1/work-mode/triggers', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`
        },
        body: JSON.stringify({ pir_trigger: config })
      });
      const data = await response.json();
      if (data.code === 0) {
        alert('配置保存成功');
      } else {
        alert(`保存失败: ${data.message}`);
      }
    } catch (error) {
      alert('保存失败');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="pir-config">
      <h2>PIR传感器配置</h2>
      
      <label>
        <input
          type="checkbox"
          checked={config.enable}
          onChange={(e) => setConfig({...config, enable: e.target.checked})}
        />
        启用PIR触发器
      </label>

      <div>
        <label>
          触发方式:
          <select
            value={config.trigger_type}
            onChange={(e) => setConfig({...config, trigger_type: e.target.value})}
          >
            <option value="rising_edge">上升沿 (检测到运动时唤醒)</option>
            <option value="falling_edge">下降沿 (运动结束时唤醒)</option>
            <option value="high_level">高电平</option>
            <option value="low_level">低电平</option>
            <option value="both_edges">双边沿 (使用上升沿)</option>
          </select>
        </label>
        <small>推荐使用上升沿触发</small>
      </div>

      <div>
        <label>
          灵敏度级别 (10-255, 推荐>30):
          <input
            type="number"
            min="10"
            max="255"
            value={config.sensitivity_level}
            onChange={(e) => setConfig({
              ...config,
              sensitivity_level: parseInt(e.target.value) || 30
            })}
          />
        </label>
        <small>值越小越灵敏，但容易误触发</small>
      </div>

      <div>
        <label>
          忽略时间 (0-15):
          <input
            type="number"
            min="0"
            max="15"
            value={config.ignore_time_s}
            onChange={(e) => setConfig({
              ...config,
              ignore_time_s: parseInt(e.target.value) || 0
            })}
          />
        </label>
        <small>
          实际时间: {(0.5 + 0.5 * config.ignore_time_s).toFixed(1)}秒
        </small>
      </div>

      <div>
        <label>
          脉冲计数 (1-4):
          <input
            type="number"
            min="1"
            max="4"
            value={config.pulse_count}
            onChange={(e) => setConfig({
              ...config,
              pulse_count: parseInt(e.target.value) || 1
            })}
          />
        </label>
        <small>窗口时间内需要达到的脉冲数</small>
      </div>

      <div>
        <label>
          窗口时间 (0-3):
          <input
            type="number"
            min="0"
            max="3"
            value={config.window_time_s}
            onChange={(e) => setConfig({
              ...config,
              window_time_s: parseInt(e.target.value) || 0
            })}
          />
        </label>
        <small>
          实际时间: {2 + 2 * config.window_time_s}秒
        </small>
      </div>

      <button onClick={saveConfig} disabled={loading}>
        {loading ? '保存中...' : '保存配置'}
      </button>
    </div>
  );
}
```

---

## 5. 参数推荐值

### 室内环境（低干扰）

```json
{
  "enable": true,
  "trigger_type": "rising_edge",
  "sensitivity_level": 30,
  "ignore_time_s": 7,
  "pulse_count": 1,
  "window_time_s": 0
}
```

### 室外环境（中等干扰）

```json
{
  "enable": true,
  "trigger_type": "rising_edge",
  "sensitivity_level": 40,
  "ignore_time_s": 10,
  "pulse_count": 2,
  "window_time_s": 1
}
```

### 高干扰环境

```json
{
  "enable": true,
  "trigger_type": "rising_edge",
  "sensitivity_level": 50,
  "ignore_time_s": 12,
  "pulse_count": 3,
  "window_time_s": 2
}
```

---

## 6. 注意事项

1. **配置验证**：所有参数都有范围限制，超出范围的值会被忽略，不会更新配置
2. **配置生效**：配置更新后，如果PIR触发器已启用，配置会立即应用到PIR传感器
3. **睡眠模式**：系统进入睡眠模式前会自动使用当前配置重新配置PIR传感器
4. **电源要求**：在低功耗模式下使用PIR唤醒时，需要保持3V3电源开启
5. **触发方式**：支持4种PIR触发方式（上升沿、下降沿、高电平、低电平），推荐使用上升沿触发（检测到运动时唤醒）
6. **触发方式选择**：`trigger_type`字段支持字符串（如"rising_edge"）或数字（0-4），无效值将使用默认值"rising_edge"

---

## 7. 错误处理

### 常见错误码

| 错误码 | 描述 | 解决方案 |
|--------|------|---------|
| 400 | 无效的JSON格式 | 检查请求体JSON格式 |
| 400 | 无效的Content-Type | 确保Content-Type为application/json |
| 401 | 未授权 | 检查Authorization header |
| 405 | 方法不允许 | 使用正确的HTTP方法（GET/POST） |
| 500 | 内部服务器错误 | 检查服务器日志 |

### 错误响应示例

```json
{
  "code": 400,
  "message": "Invalid JSON"
}
```

---

## 8. 相关API

- 工作模式触发器配置 API - 完整的触发器配置文档
- 电源模式配置 API - 电源模式相关配置

---

## 版本历史

- **v1.0** (2026-01-13): 初始版本，支持PIR传感器参数配置
