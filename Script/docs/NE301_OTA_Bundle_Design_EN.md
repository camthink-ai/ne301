# NE301 OTA Full Bundle (One-Click Upgrade) Design

> English translation of `Docs/NE301_OTA_Bundle_Design.md` (the authoritative
> source; kept in sync manually).

> Status: design review draft
> Date: 2026-08-21
> Scope: bundle format, device-side OTA flow, web front end, build scripts and CI

> ## ⚠️ Design Revisions (2026-08-24, post-implementation): unified forced direct burn
>
> After implementation validation the design was simplified. **This banner
> supersedes every AB/direct dual-mode description below:**
>
> 1. **Bundle burns no longer use AB partitions** — the bundle path is fully
>    decoupled from the AB slot machinery; the AB logic of single-firmware
>    OTA remains unchanged and unaffected.
> 2. **No more "did the partition table change" branching** — the device runs
>    one validity check on the bundle partition table
>    (`ota_bundle_layout_change_valid`: fixed partitions must not move, no
>    overlap, within flash bounds — regardless of whether the table matches
>    the device), then always burns direct at the bundle table's addresses.
>    `layout_changed` degrades to an **informational field** (true when the
>    table differs from the device's compile-time table; only drives the red
>    UI warning).
> 3. **Uniform forced burn** — version comparison and skip decisions were
>    removed; every entry in the precheck plan burns, at the device-resolved
>    direct address.
> 4. **OTA info is erased every time** — the erase moved from "before the
>    direct FSBL write" to `bundle/begin` (independent of whether the bundle
>    contains FSBL): once the session starts, SystemState is declared invalid
>    and the next boot rebuilds it from the new layout (defaulting to slot A
>    = APP1/AI_1, matching the direct-burn landing points). The APP is LRUN
>    (FSBL copies it to PSRAM; not XIP), so burning the APP1 partition
>    while running is safe.
>
> The pack script likewise dropped `--layout-changed` / `--force` (both
> meaningless now); `addr_override` is always written (informational; the
> device resolves addresses itself).
>
> **Second revision (same day): the fixed-partition strict check was narrowed
> to FSBL only.** The original design required all seven fixed partitions to
> match or rejected the bundle; now only FSBL is enforced (the BootROM boot
> address is a physical constraint). NVS/OTA/SWAP/RESERVE1/LITTLEFS/RESERVE2
> may move — the user-visible cost (settings/storage reset) is reported via
> the `layout_diff` red warning (with extra bold data-reset note for
> NVS/LITTLEFS) instead of blocking the user's forced-upgrade intent.
>
> **Third revision (same day): `PARTITION_LAYOUT_VERSION` and the header
> layout_ver fields were removed entirely.** Under forced direct burn every
> decision (`layout_changed`, table validity) is a per-partition byte
> comparison of the actual tables; the version numbers had no consumer:
> the macro was deleted from `mem_map.h`, header offsets 0x1C/0x20 became
> reserved, and precheck JSON / packer / verifier dropped the fields.
>
> **Fourth revision (same day): same-version entries may be skipped at the
> user's choice.** precheck tags every entry `skippable` (device current
> version == package version **and** the partition's base/size in the bundle
> table matches the device's compile-time table — a moved partition must
> re-burn, or the new layout would read the firmware at the new address and
> find nothing). The confirmation page offers a skip checkbox for such
> entries; `bundle/begin` accepts `{"skip":["wifi",...]}` and re-validates
> on the device — only `skippable` entries may be skipped, and never all of
> them (skipping WiFi also withdraws the push flag, so a stale staged .rps
> is not pushed to the 917). This is an **explicit user choice**, not an
> automatic skip: the default is still a full burn. **FSBL may additionally
> be skipped only when the layout is completely unchanged** — the bootloader
> binary embeds the compile-time partition table (it rebuilds SystemState
> and copies the APP per that table), so any layout change makes the old
> FSBL stale; and a **layout-changing bundle must contain an FSBL entry**,
> otherwise precheck rejects it (no layout migration without a new
> bootloader).
>
> **Fifth revision (2026-08-25): device model gate.** The headers gained a
> `device_model` field (single-firmware package 0x1C, bundle outer header
> 0x24; `NE301 = 0x3010`, 0 = unstamped legacy packages, allowed through).
> The device compares it against the compile-time `OTA_DEVICE_MODEL` at both
> precheck and upload — cross-model firmware (e.g. an NE302 package on an
> NE301) is hard-rejected. The single source of truth is the root Makefile's
> `DEVICE_MODEL` variable (changing the model requires `make clean`).

---

## 1. Background and Goals

OTA today covers five independent firmware types (FSBL / APP / WEB / Model /
WiFi), each uploaded separately. Whenever a release **changes the partition
table** (`mem_map.h`), the old firmware cannot burn the new firmware at the
right addresses; users must unpack and handle them one by one, and
development must keep compatibility logic for the previous release.

Goal: produce **one full bundle** (all firmware + partition description +
version/skip/force policy) that the user uploads once via the web advanced
options to upgrade the whole device — including the partition-table-change
scenario.

---

## 2. Current State (key facts this design is built on)

| # | Fact | Source | Meaning for the design |
|---|------|--------|------------------------|
| 1 | **OTA upload is already a streaming direct flash write**: mongoose connection → 1KB `write_buf` → flash; only ~2KB of context lives in PSRAM the whole time | `api_ota_module.c` `ota_upload_stream_processor` | **"a 10+MB bundle won't fit in PSRAM" does not hold for the transport**. The real problem is dispatching multiple firmwares to their partitions, not staging the bundle |
| 2 | Only APP has AB slots (APP1/APP2); WEB / AI_1 / AI_2 / WiFi / FSBL are single-slot (`offset_A == offset_B`); the model's "dual-partition protection" is `FIRMWARE_AI_1/AI_2` + the NVS `ai_1_active` flag | `upgrade_manager.c` `g_partitions[]` | Firmware differ in recoverability; burn order should follow recoverability |
| 3 | For FSBL / WiFi uploads **the OTA header is not written to flash** — the payload goes straight to the partition base; other types write "1KB header + payload" wholesale into the slot | `api_ota_module.c` comments, Makefile `pkg-wifi` comments | Embedding the existing `_pkg.bin` files verbatim reuses the whole validation/slot-write chain |
| 4 | WiFi upgrade is two-phase: stream to `WIFI_FW_BASE` (staging only) → `upgrade-local` sets an NVS flag → pushed to the SiWG917 on reboot | `ota_upgrade_local_handler` → `wifi_mark_update_pending` | In the bundle flow the WiFi flag must be set **after all uploads complete and before reboot** |
| 5 | OTA info (`SystemState`) lives in the 8KB OTA partition; when corrupted/erased, `init_system_state()` **rebuilds it automatically from each partition's slot-A OTA header per the compile-time table** | `upgrade_manager.c` | "Rebuild OTA info after a table change" is an existing mechanism: erase the OTA partition and the new firmware rebuilds it — no migration needed |
| 6 | The WEB partition is single-slot, but the APP embeds a recovery page as a fallback (minimal UI when the flashed WEB is broken) | `web_recovery_reload_assets` | A broken WEB has a fallback, so it can burn early in the order |
| 7 | The front end already has a mature pattern: `sliceFile(2KB)` precheck → streaming upload → upgrade-local → restart → `/upgrade-waiting` polling | `import-firmware.tsx`, `ImportWifi` | The bundle front end can extend this pattern wholesale |
| 8 | `ota_header_t` reserves `target_addr / target_size / compress / encrypt` fields; the sub-package format is stable | `ota_header.h` | The bundle only needs an outer header; the inner packages are untouched |
| 9 | During an upgrade the backend stops MQTT/AI/camera, waits for capture, with a 5-minute watchdog | `api_ota_module.c` | Sequential uploads reuse the same watchdog; nothing to rewrite |
| 10 | `mem_map.h` has no layout version number | — | (superseded: the third revision removed `PARTITION_LAYOUT_VERSION`; layout decisions compare the actual tables) |

---

## 3. Alternatives Considered

Four ways to solve the "dispatch" problem:

| Option | Idea | Pros | Cons | Verdict |
|------|------|------|------|---------|
| **A. Browser orchestration (chosen)** | The browser slices the bundle header to the device, gets back a "burn plan", then uploads every sub-package (`file.slice()` blobs whose content is exactly the `_pkg.bin`) through the **existing** streaming endpoint, ordered by recoverability | Minimal new device code; 100% reuse of the proven upload/validation/service-stop/timeout chain; **per-firmware retry**; largest single upload is the 8MB model — better resilience than one 17MB POST | The browser tab must stay open for the whole sequence (same risk class as today's single-firmware upload); the entry point must ship in the older web first (baseline problem) | ✅ **adopted** |
| B. Device-side single-pass stream | A new bundle streaming endpoint; one upload, the device writes each partition as data arrives | User uploads once; thinnest front end | Duplicates a ~500-line streaming state machine (service stop/timeout/cleanup/progress); a dropped connection re-uploads the whole ~17MB; progress needs a new status endpoint; dual-track maintenance with the existing endpoint | ❌ deferred (**format stays forward-compatible**: entries carry offset/size in burn order, this can be added later) |
| C. Stage to littlefs | Write the bundle to a littlefs file, dispatch on device | Device-autonomous; the browser may leave | littlefs free space is not guaranteed (captures fill it); write-amplification is slow; **bootstrapping paradox**: when the table changes, the littlefs base itself may move | ❌ rejected |
| D. Stage whole bundle in PSRAM | Read the whole bundle into PSRAM, then dispatch | Straightforward | The 28MB PSRAM variant cannot hold ~17MB plus heap fragmentation; 60MB is not guaranteed to have a contiguous block; pointless given fact 1 | ❌ rejected |

> On the concern "parsing on the front end is risky": option A's real risk is
> lower than intuition — every sub-firmware upload is **independently atomic**
> (model writes the inactive partition / WEB has the recovery fallback /
> WiFi without the flag never pushes / APP has AB), so any interruption
> leaves the device runnable or retryable. Keeping the tab open is exactly
> the same requirement as uploading one 8MB model today — not a new risk.

### Option A overall flow

```
┌──────────┐  ①slice(0,4096)   ┌──────────┐
│ Browser  │ ─────────────────▶ │  Device  │  parse bundle header + compare own table/versions
│(bundle   │ ◀───────────────── │          │  return burn plan (order / skip / addresses)
│  file)   │   plan(JSON)       └────┬─────┘
└────┬─────┘                         │
     │  ② POST bundle/begin (enter bundle session, stop services once)
     │                               │
     │  ③ upload each sub-package in plan order (_pkg.bin blobs)
     │     └─ direct: POST /system/ota/upload?firmwareType=xxx&addr=…&direct=1
     │        order: Model → WEB → WiFi → APP → FSBL
     │                               │
     │  ④ POST bundle/finish: set wifi flag / leave session
     │  ⑤ POST system/restart → /upgrade-waiting → firmware-versions check → report
```

---

## 4. Bundle Format

### 4.1 Overall structure

```
┌──────────────────────────────────────────────┐
│ Bundle Header (4096 B)                        │  outer header: all sub-firmwares + new table
├──────────────────────────────────────────────┤
│ sub-package #1 = existing model _pkg.bin      │  each carries its own 1KB ota_header_t + payload
├──────────────────────────────────────────────┤
│ sub-package #2 = web-assets _pkg.bin          │
├──────────────────────────────────────────────┤
│ sub-package #3 = wifi _flash _pkg.bin         │
├──────────────────────────────────────────────┤
│ sub-package #4 = app _signed _pkg.bin         │
├──────────────────────────────────────────────┤
│ sub-package #5 = fsbl _signed _pkg.bin        │
└──────────────────────────────────────────────┘
```

Design principles:
- **Inner packages untouched**: sub-firmwares are the existing `make pkg`
  `_pkg.bin` outputs; the device's magic/CRC/type/size validation and
  slot-write rules apply verbatim;
- **Concatenation order = burn order** (Model → WEB → WiFi → APP → FSBL),
  forward-compatible with a future option B;
- The outer header is a fixed 4096B, packed, self-checking — styled after
  `ota_header_t`.

### 4.2 Bundle header structure (`ota_bundle.h`)

```c
#define BUNDLE_HEADER_SIZE   4096
#define BUNDLE_MAGIC         0x4F544146   /* "OTAF" */
#define BUNDLE_HEADER_VER    0x0100
#define BUNDLE_MAX_ENTRIES   10
#define BUNDLE_MAX_PARTS     16
/* flags0 and the per-entry flags byte are reserved (zero): the forced-direct
 * revision removed the LAYOUT_CHANGED / RESET_OTA_INFO / FORCE_ALL /
 * ENTRY_FORCE signalling — a bundle always burns everything it carries */

typedef struct __attribute__((packed)) {
    uint8_t  fw_type;        /* reuses OTA_FW_TYPE_xxx (0x01 FSBL / 0x02 APP / 0x03 WEB / 0x04 AI / 0x08 WiFi) */
    uint8_t  flags;          /* reserved (0) */
    uint16_t reserved;
    uint8_t  version[8];     /* same format as ota_header_t.fw_ver [maj,min,pat,build_lo,build_hi,0,0,0] */
    uint32_t offset;         /* relative to the payload area start (right after the 4096B header) */
    uint32_t size;           /* sub-package bytes (incl. its 1KB OTA header) */
    uint32_t pkg_crc32;      /* sub-package CRC32 (cross-check against inner fw_crc32) */
    uint32_t addr_override;  /* pack-time burn address (informational; the device
                                re-resolves from the partition table at precheck —
                                only the printed output trusts this field) */
    uint32_t reserved2;
} bundle_entry_t;            /* 32 B */

typedef struct __attribute__((packed)) {
    uint8_t  part_id;        /* partition id enum, see below */
    uint8_t  reserved;
    uint16_t reserved2;
    uint32_t base;           /* absolute address */
    uint32_t size;
} bundle_part_t;             /* 12 B */

typedef struct __attribute__((packed)) {
    /* ===== basic section (64 B) ===== */
    uint32_t magic;            /* 0x00: BUNDLE_MAGIC */
    uint16_t header_version;   /* 0x04 */
    uint16_t header_size;      /* 0x06: 4096 */
    uint32_t header_crc32;     /* 0x08: whole-header CRC32 (own field zeroed) */
    uint8_t  bundle_type;      /* 0x0C: 0x01 = full bundle */
    uint8_t  flags0;           /* 0x0D: reserved (0) */
    uint8_t  reserved1[2];
    uint32_t timestamp;        /* 0x10 */
    uint32_t entry_count;      /* 0x14 */
    uint32_t payload_total;    /* 0x18: sum of all sub-package sizes */
    uint32_t reserved5;        /* 0x1C: reserved (0) */
    uint32_t reserved6;        /* 0x20: reserved (0) — was the layout_ver_old/new pair;
                                  forced-direct burns compare the partition tables
                                  themselves, so the version numbers had no consumer */
    uint32_t device_model;     /* 0x24: target device model (OTA_DEVICE_MODEL; 0 = legacy).
                                  Checked at precheck — a cross-model bundle is
                                  rejected before bundle/begin erases OTA info */
    uint8_t  reserved2b[24];   /* 0x28-0x3F: reserved */

    /* ===== sub-firmware table ===== */
    bundle_entry_t entries[BUNDLE_MAX_ENTRIES];     /* from 0x40, 320 B */

    /* ===== partition table of the new layout (always authoritative) ===== */
    uint16_t part_count;
    uint16_t reserved3;
    bundle_part_t parts[BUNDLE_MAX_PARTS];          /* 192 B */

    /* ===== reserved extensions (signatures / manifest strings / deps …) ===== */
    uint8_t reserved4[ BUNDLE_HEADER_SIZE - 64 - 320 - 4 - 192 ];
} ota_bundle_header_t;

_Static_assert(sizeof(ota_bundle_header_t) == BUNDLE_HEADER_SIZE, "bundle header must be 4096");
```

Partition id enum: `FSBL / NVS / OTA / SWAP / RESERVE1 / APP1 / APP2 / AI_1 /
AI_2 / WEB / WIFI / LITTLEFS / RESERVE2`.

### 4.3 Decision rules (revised: unified direct burn, user-chosen skip)

| Condition | Action | Mode |
|-----------|--------|------|
| Any entry (version and layout not consulted) | always burns | `direct`, at the address the device resolved from the bundle partition table |
| Entry version matches the device **and** its partition is unmoved (base/size equal to the device table), user ticked skip; FSBL additionally requires the layout to be completely unchanged (the bootloader embeds the table) | not burned, not uploaded (`begin` validates `skippable`; skipping everything is not allowed) | — |

Version info (`cur_version` / `new_version`) is still returned in the plan
JSON but is **display-only**. The original version-comparison/skip/force
logic was removed together with the AB dual mode.

### 4.4 Device-side bundle header validation (all at precheck)

1. magic / header_size / header_crc32;
2. `entry_count ≤ 10`; each entry's `offset/size` inside `[0, payload_total)`,
   non-overlapping, ascending by offset; `fw_type` valid and unique;
3. `parts[]` validity (**unconditional**): ids valid and unique, size > 0,
   within flash bounds, non-overlapping; **the only hard check is that FSBL
   exists and matches the device** — the BootROM always fetches the boot
   image from 0x70000000; a moved FSBL never takes effect and its burn would
   clobber another region (a physical constraint). Every other partition
   (incl. NVS/OTA/SWAP/RESERVE/LITTLEFS) **may move**: the user-visible cost
   (settings reset / storage reformat) is surfaced via `layout_changed` +
   `layout_diff` red warnings (bold data-reset note for NVS/LITTLEFS) and
   does **not block the upgrade** (2026-08-24 second revision: requiring all
   seven fixed partitions to match was too strict). Note: if the bundle both
   moves NVS and contains WiFi, the push flag written to the old NVS at
   finish is unreadable after reboot — that WiFi push round is skipped and
   must be re-run; **if `layout_changed`, the bundle must contain an FSBL
   entry** (the bootloader embeds the compile-time table; no migration
   without a new bootloader) or precheck rejects it;
4. every entry must fit its target partition (`entry.size ≤ part.size`);
5. per-entry `pkg_crc32` / `size` consistency with the inner header is
   naturally validated at upload time by the existing endpoint (inner
   `total_package_size` == HTTP Content-Length == runtime `fw_crc32` CRC);
6. **device model gate** (`device_model` @0x24): non-zero and ≠ the device's
   compile-time `OTA_DEVICE_MODEL` → precheck rejects (each embedded
   sub-package's 0x1C model stamp is re-checked at upload — belt and
   braces); 0 = legacy bundle, allowed. The single source of truth is the
   root Makefile's `DEVICE_MODEL` (injected into the firmware via
   `COMMON_DEFS` and into the packers via `-m`).

---

## 5. Device-Side Changes

### 5.1 New APIs (`api_ota_module.c`)

| API | Method | Description |
|-----|--------|-------------|
| `/api/v1/system/ota/bundle/precheck` | POST | body = the 4096B bundle header. Validates + builds the burn plan, returns the plan JSON (see 5.2). **Writes no flash** |
| `/api/v1/system/ota/bundle/begin` | POST | optional body `{"skip":["wifi",...]}` (skippable entries only, never all; device re-validates). Enters the bundle session: sets `g_bundle_session.active` and **erases the OTA info partition immediately** (every time, regardless of FSBL presence) — the next boot rebuilds SystemState from the new layout (slot A default). While active, all normal (non-direct) OTA uploads are rejected — mutual exclusion both ways |
| `/system/ota/upload?firmwareType=X&direct=1&addr=0x…` | POST | **existing endpoint extension**: with `direct=1` (requires an active bundle session + fw_type in the plan + addr equal to the device-resolved precheck value + size ≤ target partition — four checks against arbitrary-address writes) it skips the AB/slot/SystemState logic and erases/writes at the absolute address; FSBL/WiFi still write no OTA header, other types still write "header + payload" wholesale. `upgrade_finish` writes no system state in direct mode |
| `/api/v1/system/ota/bundle/finish` | POST | body `{result: "ok" \| "abort"}`. ok: if the plan includes WiFi → `wifi_mark_update_pending()`; session cleared. abort: session cleared only; burned firmware stays as-is (re-running the bundle re-burns — idempotent) |
| `/api/v1/system/ota/bundle/status` | GET | (optional, v1.1) session step/progress for page-refresh recovery |

> The existing `upgrade-local` WiFi trigger stays untouched; the
> single-firmware flow is unaffected.

### 5.2 Plan JSON returned by precheck

```json
{
  "layout_changed": false,
  "total_bytes": 17350400,
  "entries": [
    { "type": "ai",  "fw_type": 4, "skippable": false, "cur_version": "1.0.0.5",
      "new_version": "1.0.0.6", "size": 8389600, "offset": 0,
      "addr": 1890091008 },
    { "type": "web", "...": "..." },
    { "type": "wifi", "...": "..." },
    { "type": "app", "...": "..." },
    { "type": "fsbl", "cur_version": "1.0.0.3", "new_version": "1.0.0.4",
      "addr": 1879048192 }
  ]
}
```

(Forced direct burn: no `mode` or per-entry `action` fields — there is one
mode and every entry burns; `total_bytes` is the sum of all entries;
`cur_version`/`new_version` are display-only. When `layout_changed` is true
an extra `layout_diff[]` lists, per partition, the base/size differences
between the device compile-time table and the bundle table (-1 = absent from
the bundle table) for the UI's migration details.)

The order is fixed: Model → WEB → WiFi → APP → FSBL (the device returns
entries in order; the front end does not sort). The device is the single
source of truth: the front end **never parses** the bundle binary — it only
picks the file, slices uploads per the plan, and shows progress. `offset` is
the sub-package's start inside the bundle payload area (after the 4096B
header); the front end slices `file.slice(4096 + offset, 4096 + offset +
size)` — it does not parse the entry table itself, the offsets arrive
pre-computed.

### 5.3 Safer FSBL writes (fixes an existing hazard along the way)

Status quo: FSBL streams to flash and only checks CRC at the end — a network
corruption has already written a bad FSBL partition (recoverable by
re-upload if not rebooted; a power cut there bricks the board).

Change: FSBL (and direct-mode FSBL) payload ≤ 512KB is **fully staged in
PSRAM → CRC verified → erased/written to flash in one pass**. The
"corrupted data already hit flash" window is gone. This also hardens the
existing single-firmware FSBL upload path.

(WiFi ~2MB keeps the current behavior: the 917-side push has its own
validation, and `WIFI_FW_BASE` is staging only — nothing is pushed without
the flag.)

### 5.4 Direct-burn landing points (revised: all bundle burns follow this table)

| Type | Written | Target address | Notes |
|------|---------|----------------|-------|
| Model | header + payload | **bundle table `AI_1` base**, and NVS `ai_1_active` reset to FALSE | OTA info was erased at begin; `init_system_state()` rebuilds with slot A default (= AI_1) — landing point and rebuild default align |
| WEB | header + payload | bundle table `WEB_BASE` | no `reload_assets` after write (the running APP may not know the new address); effective after reboot |
| WiFi | payload (flash_header_t + .rps, no OTA header) | bundle table `WIFI_FW_BASE` | staging only; flag set at finish, pushed on reboot |
| APP | header + payload | bundle table `APP1_BASE` (slot-A semantics) | rebuild default is slot A, matching the landing point. The APP is LRUN (FSBL copies it to PSRAM) — writing APP1 while running is safe |
| FSBL | payload (no OTA header; PSRAM-staged + CRC first) | bundle table `FSBL_BASE` (enforced unmoving by the parts check) | **last** in the burn sequence |

> Why the FSBL partition address stays fixed for now: FSBL is the only
> component that addresses itself; relocating it needs bootloader relocation
> support, out of scope here. Table changes are limited to
> APP/AI/WEB/WiFi/Littlefs boundaries.

### 5.5 Burn order and interruption-recovery matrix (revised)

| # | Firmware | Direct (the only mode) |
|---|----------|------------------------|
| 1 | Model | burns at the bundle-table AI_1 base; an old model in the other slot stays at its old address; an interruption is harmless — re-running the bundle re-burns |
| 2 | WEB | single slot; a bad write falls back to the APP-embedded recovery page until reboot |
| 3 | WiFi | only stages the .rps; without the flag nothing is pushed; an interruption has zero effect |
| 4 | APP | direct-overwrites APP1 (the running APP lives in PSRAM, unaffected); if APP1 ends up half-written, the reboot's `init_system_state` fails to read the APP1 header and the recovery page / re-burn recovers |
| 5 | FSBL | written after PSRAM staging + up-front CRC; power loss during the write = brick (needs SWD) — the inherent residual risk |

**OTA info erase timing (revised: erased at `bundle/begin`)**: begin is the
start of both the session and the "forced full reflash" semantics — from that
moment the old SystemState is invalid (everything afterwards writes direct,
slot state is neither read nor written), and it happens regardless of whether
the bundle contains FSBL. A blank OTA partition is the safe default for both
firmware generations (`init_system_state` rebuilds from slot-A headers on a
corrupt/blank state):

- power loss at any point after begin → the reboot rebuilds with the current
  (old) layout's slot-A default → the untouched old APP1 boots normally; if
  the APP direct-write was interrupted, APP1 is half-new and the recovery
  page / a bundle re-run recovers;
- power loss/reboot after everything burned + finish → new layout + blank OTA
  info → the new firmware rebuilds slot A per the new table, reads the new
  APP1 header written by the direct burn, boots normally.

**On reusing the existing endpoint's reboot semantics**: neither device-side
burn path (the web streaming `ota_upload_stream_processor` and the
file-based `ota_service.c`) **reboots by itself** — `upgrade_finish` only
updates slot info and saves SystemState (direct mode skips even that);
`upgrade-local` is a no-op for non-WiFi types. The reboot is driven by the
bundle front end only after `finish` (WiFi flag set, session cleared).

---

## 6. Web Front-End Design

### 6.1 Entry point

Same pattern as the FSBL / WiFi upgrades: **import-firmware dialog →
advanced-options popover → third entry "Full bundle upgrade"** →
`navigate('/import-bundle')` (a new route beside `/import-fsbl` and
`/import-wifi`).

### 6.2 Page state machine (`/import-bundle`)

```
select file ──▶ precheck ──▶ plan confirm ──▶ sequential burn ──▶ reboot wait ──▶ report
   (a)           (b)            (c)              (d)                (e)            (f)
```

- **(a) select file**: accepts `.bin`; `maxSize` raised to 64MB (file object
  only; the browser slices on demand, never reads it all into memory).
- **(b) precheck**: `file.slice(0, 4096)` → `POST bundle/precheck`. Failure
  (bad header / layout rejected / invalid type) → red banner with the reason.
- **(c) plan confirm** (the densest screen — the interaction-design focus):
  - per-row: firmware name / current → new version / size / burn address /
    skip checkbox (skippable entries only);
  - when `layout_changed`: a red warning block on top ("this upgrade changes
    the partition table and rebuilds OTA info, **no AB rollback** — do not
    power off"), plus the per-partition `layout_diff` details;
  - bottom shows the estimated transfer total (`Σ selected sizes`).
- **(d) sequential burn**:
  - stepper: `Model → WEB → WiFi → APP → FSBL → reboot`, current item
    highlighted;
  - two-level progress: **overall** = Σ uploaded bytes / total; **current
    item** = XHR `onprogress`; if progress stalls for >4s the device is
    erasing the partition — a pulsing hint says so;
  - a full-viewport click shield + route blocker + `beforeunload` keep the
    user on the page while streaming;
  - failure stops the run: the item turns red with the reason; a retry
    re-runs precheck (the session is re-armed automatically) and abort
    leaves burned firmware as-is;
  - all done → `POST bundle/finish` → `restartDevice`;
- **(e) reboot wait**: reuses the `/upgrade-waiting` pattern (`retryFetch`
  polling for the device to come back); if it never does, the same guidance
  page is shown (reconnect the device AP / watch the LED).
- **(f) report**: on return, fetch `firmware-versions` and compare item by
  item against the bundle versions.

### 6.3 Interruption recovery

After a refresh / network drop / switching computers: re-enter the page,
pick the same file → precheck → re-burn (forced full reflash is idempotent —
no per-item completion state is kept). This is option A's core resilience:
**every re-entry is an idempotent re-run**.

### 6.4 i18n

~25 new strings under `sys.system_management.bundle_*` (Chinese/English).

---

## 7. Build and CI Integration

### 7.1 `mem_map.h`

No new requirements (the third revision removed `PARTITION_LAYOUT_VERSION` —
see the revision banner). The pack/verify scripts parse only the
`*_BASE`/`*_SIZE` macros.

### 7.2 Pack script `Script/ota_bundle_packer.py`

- inputs: `build/*_pkg.bin` (the five existing outputs), `mem_map.h`
  (macro-parsed for the partition table), version strings from the Makefile;
- arguments: `--mem-map`, `--board-flash {64,128}`, `--exclude fsbl,wifi…`
  (subset bundles without FSBL etc., e.g. SensorExt variants),
  `-m/--model` (device model stamp, default from the root Makefile's
  `DEVICE_MODEL`); ~~`--layout-changed` / `--reset-ota` / `--force-fsbl`~~
  (removed with the unified forced-direct revision);
- output: `build/ne301_Full_v<APP_VERSION>_bundle.bin`;
- the script fixes the entry order Model → WEB → WiFi → APP → FSBL,
  computes the CRCs and stamps the header;
- `Script/verify_ota_package.py` gained a bundle mode for offline checks
  (header CRC, non-overlapping entries, inner-header cross-checks, device
  model).

### 7.3 Makefile

```makefile
.PHONY: pkg-bundle
pkg-bundle: pkg
	@python $(PKG_SCRIPT_DIR)/ota_bundle_packer.py $(BUILD_DIR) \
	    -o $(BUILD_DIR)/ne301_Full_v$(APP_VERSION_STR)_bundle.bin -m $(DEVICE_MODEL)
```

A standalone target (not folded into `pkg`): run `make pkg-bundle` when a
bundle is needed, keeping the existing flow and size accounting untouched.

### 7.4 GitHub Actions (`main.yml`)

- build step appends `make pkg-bundle STEDGEAI_VARIANT=…` (for SensorExt
  variants either `--exclude app` with the SensorExt APP, or two bundles);
- `upload-artifact` paths gain `./build/*_bundle.bin`;
- release files gain `./build/*_bundle.bin`; the release note gains a bundle
  usage section.

---

## 8. Edge Cases and Risk Register

| # | Risk / edge | Handling |
|---|-------------|----------|
| 1 | Power loss during the FSBL write = brick | Inherent. PSRAM staging + up-front CRC removes "bad data hits flash"; the page warns loudly about power; FSBL burns last |
| 2 | Power loss between APP and FSBL | They run back-to-back; re-running the bundle re-burns (no skip — naturally idempotent) |
| 3 | Old web without the bundle entry | The feature requires a minimum web version (baseline moves forward); noted in the first-release instructions |
| 4 | User refreshes mid-upgrade | `beforeunload` warning; idempotent re-run after refresh (see 6.3) |
| 5 | direct mode abused as an arbitrary-address write | Four checks: active bundle session + fw_type in plan + addr == device-registered precheck value + size ≤ target partition; the route still goes through `require_auth` |
| 6 | Bundle session concurrent with normal OTA | After `bundle/begin` normal uploads are rejected (mutual exclusion both ways); **10-minute** idle timeout clears the session (`OTA_BUNDLE_TIMEOUT_MS`, hooked into the `ota_check_timeout` watchdog poll; longer than one slow 8MB sub-firmware upload) |
| 7 | Data partitions (NVS / LITTLEFS …) moved by the bundle | Only FSBL is immovable (physical); other moves are **allowed**, with the cost carried by the front-end warning (NVS = settings reset, LITTLEFS = storage reformat, bold for both; moving NVS + containing WiFi voids that round's push flag — re-run), see 4.4 |
| 8 | ~~version suffix vs skip~~ | Removed with the skip logic (unified direct burn) |
| 9 | 100MB Content-Length cap | Largest sub-firmware is the 8MB model — far below the cap |
| 10 | Bundle size | Typically ~17MB (0.5+4+1+3+8); gzip later if worth it (`ota_header_t` reserves a compress field); v1 ships uncompressed |
| 11 | Web assets switching mid-sequence | The page already runs from memory; no reload during the sequence; one reboot after finish |
| 12 | Model dual-partition direct-burn landing | Always writes the bundle-table `AI_1` and resets the NVS flag (see 5.4), aligned with the post-erase rebuild default |
| 13 | Session timeout / abort after begin erased OTA info | The device keeps running (the running APP does not read SystemState); the next reboot rebuilds slot A; the front end always reboots right after finish, so the normal path never sees this window |
| 14 | Low-power mode: AP sleep timer firing mid-burn | Upload traffic refreshes the AP sleep timer (1Hz-throttled `web_server_ap_sleep_timer_reset()` in the OTA stream processor) — a >90s burn no longer looks like "no web operation" |

---

## 9. Implementation Slicing (suggested five merges)

| Phase | Content | Exit criteria |
|-------|---------|---------------|
| P1 build side | `ota_bundle.h` format, `ota_bundle_packer.py`, `verify_ota_package.py --bundle`, Makefile `pkg-bundle`, CI artifacts | CI produces a bundle that passes offline verification |
| P2 device (session & plan) | `bundle/precheck` / `begin` / `finish`, begin erases OTA info, FSBL PSRAM-staged writes | precheck returns a full direct-burn plan; after begin the OTA partition is blank and normal uploads are rejected |
| P3 front end | `/import-bundle` page + advanced-options entry + i18n | end-to-end one-click upgrade + idempotent re-run |
| P4 device (direct execution) | `upload` endpoint `direct/addr` extension (four checks), `upgrade_finish` direct early-exit | one real cross-layout bundle burn (incl. injected interruptions: power after begin / after APP) |
| P5 wrap-up | OTA guide updates, release-note template, interruption-matrix test report | docs + test records archived |

P1/P2 can merge first independently (touching no existing path); P3/P4 are
each independently revertible.

---

## Appendix A: Relationship to the existing single-firmware upload

The single-firmware entry points and flows are **fully preserved** (incl.
the FSBL/WiFi advanced pages); the bundle is a new parallel entry, both share
the `/system/ota/upload` foundation. `upgrade-local` keeps serving only the
WiFi single-firmware case; the bundle's WiFi flag setting moved into
`bundle/finish`.

## Appendix B: Open questions (for review)

1. Should the **SensorExt variant** get its own bundle (`--exclude` subset)
   or one bundle with two APPs (a second APP entry)? Leaning toward the
   former;
2. Should bundles be **signed** (`ota_header_t` reserves a 416B security
   section; no signing anywhere today)? Suggest a dedicated project together
   with single-firmware signing;
3. Should the `bundle/status` endpoint (progress recovery after a page
   refresh) make v1?
4. Is gzip worth it: 17MB → ~8MB estimated, but the device would need a
   zlib streaming decompressor writing to flash — decide after measuring
   real transfer times.
