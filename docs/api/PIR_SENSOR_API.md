# PIR Sensor Configuration Web API

## Overview

The PIR sensor configuration API manages the PIR (passive infrared) sensor, including sensor parameters and trigger enable/disable. The configuration is persisted to NVS.

**Base URL:** `/api/v1`
**Authentication:** all endpoints require auth (Authorization header)

---

## API Endpoints

PIR sensor configuration is part of the work mode trigger configuration and is accessed through:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/work-mode/triggers` | get all trigger configurations (including PIR) |
| POST | `/api/v1/work-mode/triggers` | set trigger configurations (including PIR) |

---

## 1. Get PIR Sensor Configuration

Fetch the PIR sensor configuration via the work mode triggers endpoint.

### Request

```
GET /api/v1/work-mode/triggers
Authorization: Bearer <token>
```

### Response

**Success (200 OK)**

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

### PIR Configuration Fields

| Field | Type | Range/options | Description | Default |
|-------|------|---------------|-------------|---------|
| `enable` | boolean | - | whether the PIR trigger is enabled | `false` |
| `trigger_type` | string | `"rising_edge"`, `"falling_edge"`, `"high_level"`, `"low_level"`, `"both_edges"` | PIR wakeup trigger mode | `"rising_edge"` |
| `sensitivity_level` | number | 10-255 | sensitivity level; lower is more sensitive (recommended > 30) | `30` |
| `ignore_time_s` | number | 0-15 | ignore-time register value; actual time = 0.5 + 0.5 × value (seconds) | `7` (4 s) |
| `pulse_count` | number | 1-4 | pulse count; actual pulses = value | `1` (2 pulses) |
| `window_time_s` | number | 0-3 | window-time register value; actual time = 2 + 2 × value (seconds) | `0` (2 s) |

**Parameter details:**

- **trigger_type (trigger mode)**
  - `"rising_edge"`: trigger on rising edge (wakes when motion is detected) - **recommended, most common**
  - `"falling_edge"`: trigger on falling edge (wakes when motion ends)
  - `"high_level"`: trigger on high level (wakes while the PIR signal is high)
  - `"low_level"`: trigger on low level (wakes while the PIR signal is low)
  - `"both_edges"`: trigger on both edges (rising edge is used in practice)
  - default: `"rising_edge"`

- **sensitivity_level (sensitivity)**
  - range: 10-255
  - recommended: > 30
  - lower values are more sensitive but prone to false triggers
  - in interference-free environments the minimum of 10 can be used

- **ignore_time_s (ignore time)**
  - range: 0-15
  - actual ignore time = 0.5 + 0.5 × value (seconds)
  - e.g. value 7 = 4 s, value 15 = 8 s
  - how long motion detection is ignored after the interrupt output returns to 0

- **pulse_count (pulse count)**
  - range: 1-4
  - actual pulse count = value
  - number of pulses that must accumulate within the window time
  - higher values are more noise-resistant but slightly less sensitive

- **window_time_s (window time)**
  - range: 0-3
  - actual window time = 2 + 2 × value (seconds)
  - e.g. value 0 = 2 s, value 3 = 8 s
  - the statistical window for pulse counting

---

## 2. Set PIR Sensor Configuration

Update the PIR sensor configuration via the work mode triggers endpoint.

### Request

```
POST /api/v1/work-mode/triggers
Content-Type: application/json
Authorization: Bearer <token>
```

### Request Body

All fields are optional; only provided fields are updated. You can update only the PIR configuration and leave other triggers unchanged.

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

### Field Validation

| Field | Validation rule |
|-------|-----------------|
| `enable` | must be boolean |
| `trigger_type` | string; must be a valid mode (see options above); invalid values fall back to "rising_edge" |
| `sensitivity_level` | integer within 10-255; out-of-range values are ignored |
| `ignore_time_s` | integer within 0-15; out-of-range values are ignored |
| `pulse_count` | integer within 1-4; out-of-range values are ignored |
| `window_time_s` | integer within 0-3; out-of-range values are ignored |

### Response

**Success (200 OK)**

```json
{
  "code": 0,
  "message": "Image mode triggers configured successfully"
}
```

**Error (400 Bad Request)**

```json
{
  "code": 400,
  "message": "Invalid JSON"
}
```

**Error (500 Internal Server Error)**

```json
{
  "code": 500,
  "message": "Failed to configure image mode triggers"
}
```

---

## 3. Configuration Application

### When the configuration takes effect

1. **At boot**: if the PIR trigger is enabled, the system initializes the PIR sensor with the configured parameters
2. **On update**: after an API update, if the PIR trigger is enabled, the new configuration is applied to the sensor immediately
3. **Before sleep**: before entering sleep mode, the PIR sensor is programmed with the current configuration

### Persistence

- All parameters are saved to NVS (non-volatile storage) automatically
- The configuration survives reboots
- It is also saved to the JSON configuration file

### Defaults

If a parameter is unset or 0, the following defaults apply:

- `sensitivity_level`: 30
- `ignore_time_s`: 7 (4 s actual)
- `pulse_count`: 1 (2 pulses actual)
- `window_time_s`: 0 (2 s actual)

---

## 4. Usage Examples

### JavaScript / TypeScript

```typescript
// get PIR configuration
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
    console.log('PIR config:', pirConfig);
    return pirConfig;
  }
}

// set PIR configuration
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
    console.log('PIR config updated');
    return true;
  } else {
    console.error('Update failed:', data.message);
    return false;
  }
}

// usage
async function configurePIR() {
  // enable PIR and set parameters
  await setPIRConfig({
    enable: true,
    trigger_type: "rising_edge",  // rising edge (wakes on motion detected)
    sensitivity_level: 35,    // medium sensitivity
    ignore_time_s: 5,         // 3 s ignore time
    pulse_count: 2,           // 2 pulses
    window_time_s: 1          // 4 s window time
  });

  // read back the current configuration
  const currentConfig = await getPIRConfig();
  console.log('Current PIR config:', currentConfig);
}
```

### React component

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

  // load configuration
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
      console.error('Failed to load config:', error);
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
        alert('Configuration saved');
      } else {
        alert(`Save failed: ${data.message}`);
      }
    } catch (error) {
      alert('Save failed');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="pir-config">
      <h2>PIR Sensor Configuration</h2>

      <label>
        <input
          type="checkbox"
          checked={config.enable}
          onChange={(e) => setConfig({...config, enable: e.target.checked})}
        />
        Enable PIR trigger
      </label>

      <div>
        <label>
          Trigger mode:
          <select
            value={config.trigger_type}
            onChange={(e) => setConfig({...config, trigger_type: e.target.value})}
          >
            <option value="rising_edge">Rising edge (wake on motion detected)</option>
            <option value="falling_edge">Falling edge (wake when motion ends)</option>
            <option value="high_level">High level</option>
            <option value="low_level">Low level</option>
            <option value="both_edges">Both edges (rising used)</option>
          </select>
        </label>
        <small>Rising edge is recommended</small>
      </div>

      <div>
        <label>
          Sensitivity level (10-255, recommended &gt; 30):
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
        <small>Lower is more sensitive but prone to false triggers</small>
      </div>

      <div>
        <label>
          Ignore time (0-15):
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
          Actual time: {(0.5 + 0.5 * config.ignore_time_s).toFixed(1)} s
        </small>
      </div>

      <div>
        <label>
          Pulse count (1-4):
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
        <small>Pulses required within the window time</small>
      </div>

      <div>
        <label>
          Window time (0-3):
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
          Actual time: {2 + 2 * config.window_time_s} s
        </small>
      </div>

      <button onClick={saveConfig} disabled={loading}>
        {loading ? 'Saving...' : 'Save configuration'}
      </button>
    </div>
  );
}
```

---

## 5. Recommended Values

### Indoor (low interference)

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

### Outdoor (medium interference)

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

### High-interference environment

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

## 6. Notes

1. **Validation**: all parameters are range-checked; out-of-range values are ignored and do not update the configuration
2. **Application**: after an update, if the PIR trigger is enabled, the configuration is applied to the sensor immediately
3. **Sleep mode**: before entering sleep, the PIR sensor is reprogrammed with the current configuration
4. **Power**: to use PIR wakeup in low-power mode, the 3V3 rail must stay on
5. **Trigger modes**: four PIR trigger modes are supported (rising edge, falling edge, high level, low level); rising edge (wake on motion detected) is recommended
6. **Mode selection**: the `trigger_type` field accepts a string (e.g. "rising_edge") or a number (0-4); invalid values fall back to "rising_edge"

---

## 7. Error Handling

### Common error codes

| Code | Description | Fix |
|------|-------------|-----|
| 400 | invalid JSON | check the request body format |
| 400 | invalid Content-Type | set Content-Type to application/json |
| 401 | unauthorized | check the Authorization header |
| 405 | method not allowed | use the correct HTTP method (GET/POST) |
| 500 | internal server error | check server logs |

### Error response example

```json
{
  "code": 400,
  "message": "Invalid JSON"
}
```

---

## 8. Related APIs

- Work mode trigger configuration API - full trigger configuration documentation
- Power mode configuration API - power mode related settings

---

## Version History

- **v1.0** (2026-01-13): initial version with PIR sensor parameter configuration
