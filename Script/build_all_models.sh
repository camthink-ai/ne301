#!/bin/bash
# Batch-compile every buildable model in Model/weights and wrap each one in an
# OTA package (1K header, fw_type=ai_model) stamped with the model OTA version.
#
# A model is buildable when all of:
#   1. Model/weights/<name>.json exists (input/output spec + postprocess params)
#   2. Model/weights/<name>.tflite or .onnx exists (the network weights)
#   3. its postprocess_type is registered in the firmware (Custom/Common/Lib/pp/*.c)
#
# Per model this runs the exact pipeline of `make model` / `make pkg-model`:
#   generate-reloc-model.sh -> model_packager.py create -> ota_packer.py
# but drives the tools directly instead of looping over `make model`: the fixed
# intermediate dirs (st_ai_c/st_ai_bin) are wiped on every run, and make would
# see a fresh build/network_rel.bin and skip regeneration -> wrong model packaged.
# Consequence: models MUST be built sequentially (this script does that).
#
# OTA header version = $(STEDGEAI_BIT).$(MODEL_VERSION_OVERRIDE) — the same
# value `make pkg-model` stamps (stedgeai.mk). The leading digit is the STEdgeAI
# generation the device gate matches on; keep it in sync with STEDGEAI_VARIANT.
#
# Usage:
#   bash Script/build_all_models.sh [OPTIONS] [model_name ...]
#
# Options:
#   --list        Show every model in weights/ (buildable or not) and exit
#   --dry-run     Show the build plan without compiling
#   --keep-temp   Keep intermediate raw packages (build/tmp_*_pkg.bin)
#   -h, --help    Show this help
#
# Environment overrides:
#   STEDGEAI_VARIANT  toolchain generation (default: stedgeai.mk -> 4.0)
#   MODEL_VERSION     OTA version stamped in the header (default: <bit>.0.0.0)
#   DEVICE_MODEL      device model id at header 0x1C (default: root Makefile 0x3010)
#   DEFAULT_PROFILE   neural-ART profile for models without a family rule
#                     (default: yolov8_od, same as Model/Makefile)
#
# Examples:
#   bash Script/build_all_models.sh --list
#   bash Script/build_all_models.sh --dry-run
#   bash Script/build_all_models.sh                      # build everything
#   bash Script/build_all_models.sh yolov8n_256_quant_pc_ui_od_meter
#   STEDGEAI_VARIANT=2.2 bash Script/build_all_models.sh # whole 2.x generation

# ---------------------------------------------------------------------------
# Paths (resolved from this script so it can be run from anywhere)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODEL_DIR="$REPO_ROOT/Model"
WEIGHTS_DIR="$MODEL_DIR/weights"
PP_DIR="$REPO_ROOT/Custom/Common/Lib/pp"
RELOC_CONFIG_FILE="neural_art_reloc.json"   # resolved relative to Model/

GEN_RELOC="$SCRIPT_DIR/generate-reloc-model.sh"
PACKAGER="$SCRIPT_DIR/model_packager.py"
OTA_PACKER="$SCRIPT_DIR/ota_packer.py"
VERIFIER="$SCRIPT_DIR/verify_ota_package.py"

BUILD_DIR="$MODEL_DIR/build"
OUT_DIR="$BUILD_DIR/models"
RAW_REL_BIN="build/network_rel.bin"          # relative to Model/ (Makefile layout)
STEDGEAI_ARGS="--inputs-ch-position chlast --input-data-type uint8"

# ---------------------------------------------------------------------------
# Colors (same palette as generate-reloc-model.sh)
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error()   { echo -e "${RED}[FAIL]${NC} $1"; }

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
LIST_ONLY=false
DRY_RUN=false
KEEP_TEMP=false
SELECTED=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --list)      LIST_ONLY=true; shift ;;
        --dry-run)   DRY_RUN=true; shift ;;
        --keep-temp) KEEP_TEMP=true; shift ;;
        -h|--help)   sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)          echo "Unknown option: $1" >&2; exit 2 ;;
        *)           SELECTED+=("$1"); shift ;;
    esac
done

# ---------------------------------------------------------------------------
# Version / variant derivation (mirrors stedgeai.mk + version.mk + root Makefile)
# ---------------------------------------------------------------------------
STEDGEAI_VARIANT="${STEDGEAI_VARIANT:-$(sed -n 's/^STEDGEAI_VARIANT ?= *//p' "$REPO_ROOT/stedgeai.mk" | tail -1 | tr -d '[:space:]')}"
STEDGEAI_VARIANT="${STEDGEAI_VARIANT:-4.0}"

case "$STEDGEAI_VARIANT" in
    2.2) STEDGEAI_BIT=2 ;;
    3.0) STEDGEAI_BIT=3 ;;
    4.0) STEDGEAI_BIT=4 ;;
    *)   log_error "Unsupported STEDGEAI_VARIANT=$STEDGEAI_VARIANT (2.2 / 3.0 / 4.0)"
         exit 2 ;;
esac

VERSION_OVERRIDE="$(sed -n 's/^MODEL_VERSION_OVERRIDE[[:space:]]*:=\s*//p' "$REPO_ROOT/version.mk" | head -1 | tr -d '[:space:]')"
MODEL_VERSION="${MODEL_VERSION:-${STEDGEAI_BIT}.${VERSION_OVERRIDE:-0.0.0}}"

DEVICE_MODEL="${DEVICE_MODEL:-$(sed -n 's/^DEVICE_MODEL ?= *//p' "$REPO_ROOT/Makefile" | head -1 | tr -d '[:space:]')}"
DEVICE_MODEL="${DEVICE_MODEL:-0x3010}"

DEFAULT_PROFILE="${DEFAULT_PROFILE:-yolov8_od}"

# ---------------------------------------------------------------------------
# Reloc profile per model family (neural_art_reloc.json profile names).
# Extend the basename case for one-off overrides; the fallback maps by
# postprocess family. mpools/: yolox_od / yolov8_mpe / yolov8_od exist for these.
# ---------------------------------------------------------------------------
profile_for_model() {  # profile_for_model <basename> <postprocess_type>
    case "$1" in
        *) ;;
    esac
    case "$2" in
        pp_od_st_yolox_*) echo "yolox_od" ;;
        pp_mpe_*|pp_spe_*) echo "yolov8_mpe" ;;
        *) echo "$DEFAULT_PROFILE" ;;
    esac
}

# ---------------------------------------------------------------------------
# Firmware-registered postprocess names (Custom/Common/Lib/pp/*.c ".name = ...")
# ---------------------------------------------------------------------------
PP_REGISTERED="$(grep -h -o '\.name *= *"[^"]*"' "$PP_DIR"/*.c 2>/dev/null | sed 's/.*"\(.*\)".*/\1/' | sort -u)"
pp_is_registered() { grep -qxF "$1" <<< "$PP_REGISTERED"; }

# ---------------------------------------------------------------------------
# Model discovery
# ---------------------------------------------------------------------------
# Emits one tab-separated record per weights/*.json:
#   basename<TAB>weights_file<TAB>postprocess_type<TAB>model_desc<TAB>status
# status: OK | no-weights | no-postprocess
discover_models() {
    local json base weights_file pp_type desc ext
    for json in "$WEIGHTS_DIR"/*.json; do
        [[ -e "$json" ]] || continue
        base="$(basename "$json" .json)"

        weights_file=""
        for ext in tflite onnx; do
            if [[ -f "$WEIGHTS_DIR/$base.$ext" ]]; then
                weights_file="weights/$base.$ext"
                break
            fi
        done

        IFS=$'\t' read -r pp_type desc < <(python - "$json" <<-'EOF'
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
    print("{}\t{}".format(d.get('postprocess_type', '?'),
                          d.get('model_info', {}).get('name', '')))
except Exception as e:
    print("?\t(unreadable config: {})".format(e))
EOF
        )
        desc="${desc%$'\r'}"   # Windows python emits CRLF; keep it out of the OTA desc

        if [[ -z "$weights_file" ]]; then
            printf '%s\t%s\t%s\t%s\tno-weights\n' "$base" "-" "$pp_type" "$desc"
        elif ! pp_is_registered "$pp_type"; then
            printf '%s\t%s\t%s\t%s\tno-postprocess\n' "$base" "$weights_file" "$pp_type" "$desc"
        else
            printf '%s\t%s\t%s\t%s\tOK\n' "$base" "$weights_file" "$pp_type" "$desc"
        fi
    done
}

# ---------------------------------------------------------------------------
# List mode
# ---------------------------------------------------------------------------
if $LIST_ONLY; then
    echo "Models in Model/weights (STEDGEAI_VARIANT=$STEDGEAI_VARIANT, OTA version $MODEL_VERSION):"
    printf '  %-52s %-16s %-24s %s\n' "MODEL" "STATUS" "POSTPROCESS" "PROFILE"
    while IFS=$'\t' read -r base wf pp desc status; do
        profile="-"
        if [[ "$status" == "OK" ]]; then
            state="${GREEN}OK${NC}"
            profile="$(profile_for_model "$base" "$pp")"
        elif [[ "$status" == "no-weights" ]]; then
            state="${YELLOW}no-weights${NC}"
        else
            state="${YELLOW}no-postprocess${NC}"
        fi
        printf '  %-52s %-16b %-24s %s\n' "$base" "$state" "$pp" "$profile"
    done < <(discover_models)
    echo ""
    echo "no-weights      config only, no .tflite/.onnx alongside — nothing to compile"
    echo "no-postprocess  postprocess_type not registered in Custom/Common/Lib/pp — device would reject it"
    exit 0
fi

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------
for tool in "$GEN_RELOC" "$PACKAGER" "$OTA_PACKER"; do
    if [[ ! -f "$tool" ]]; then
        log_error "Required script not found: $tool"
        exit 2
    fi
done
if [[ ! -d "$WEIGHTS_DIR" ]]; then
    log_error "weights directory not found: $WEIGHTS_DIR"
    exit 2
fi
command -v python &>/dev/null || { log_error "python not found"; exit 2; }

log_info "STEDGEAI_VARIANT = $STEDGEAI_VARIANT (bit $STEDGEAI_BIT)"
log_info "OTA version      = $MODEL_VERSION (fw_type=ai_model)"
log_info "Device model     = $DEVICE_MODEL"
log_info "Default profile  = $DEFAULT_PROFILE@$RELOC_CONFIG_FILE"
echo ""

# ---------------------------------------------------------------------------
# Build queue
# ---------------------------------------------------------------------------
QUEUE=()
SKIPPED=()
while IFS=$'\t' read -r base wf pp desc status; do
    if [[ ${#SELECTED[@]} -gt 0 ]]; then
        want=false
        for sel in "${SELECTED[@]}"; do
            [[ "$sel" == "$base" ]] && want=true
        done
        $want || continue
        if [[ "$status" != "OK" ]]; then
            log_error "Requested model '$base' is not buildable ($status)"
            exit 2
        fi
    else
        if [[ "$status" != "OK" ]]; then
            SKIPPED+=("$base ($status)")
            continue
        fi
    fi
    QUEUE+=("$base"$'\t'"$wf"$'\t'"$pp"$'\t'"$desc")
done < <(discover_models)

# catch typos in an explicit model selection
if [[ ${#SELECTED[@]} -gt 0 ]]; then
    for sel in "${SELECTED[@]}"; do
        found=false
        for entry in "${QUEUE[@]}"; do
            [[ "${entry%%$'\t'*}" == "$sel" ]] && found=true
        done
        $found || { log_error "Unknown model: $sel (see --list)"; exit 2; }
    done
fi

if [[ ${#SKIPPED[@]} -gt 0 ]]; then
    log_warning "Skipping ${#SKIPPED[@]} config-only / unsupported model(s):"
    for s in "${SKIPPED[@]}"; do echo "    $s"; done
    echo ""
fi

if [[ ${#QUEUE[@]} -eq 0 ]]; then
    log_error "No buildable models found"
    exit 1
fi

log_info "Build queue: ${#QUEUE[@]} model(s)"
for entry in "${QUEUE[@]}"; do
    IFS=$'\t' read -r base wf pp desc <<< "$entry"
    printf '  %-52s profile=%s\n' "$base" "$(profile_for_model "$base" "$pp")"
done
echo ""

if $DRY_RUN; then
    log_info "Dry run — nothing compiled"
    exit 0
fi

# ---------------------------------------------------------------------------
# Build loop (sequential: st_ai_c/st_ai_bin are wiped and rebuilt per model)
# ---------------------------------------------------------------------------
mkdir -p "$OUT_DIR"

FAILED=()
BUILT=()
IDX=0
for entry in "${QUEUE[@]}"; do
    IFS=$'\t' read -r base wf pp desc <<< "$entry"
    IDX=$((IDX + 1))
    PROFILE="$(profile_for_model "$base" "$pp")"
    RAW_PKG="$BUILD_DIR/tmp_${base}_pkg.bin"
    OTA_PKG="$OUT_DIR/${base}_v${MODEL_VERSION}_pkg.bin"

    echo "----------------------------------------------------------------"
    log_info "[$IDX/${#QUEUE[@]}] $base"
    log_info "  weights : $wf"
    log_info "  profile : $PROFILE@$RELOC_CONFIG_FILE"
    log_info "  output  : ${OTA_PKG#$REPO_ROOT/}"

    cd "$MODEL_DIR" || { FAILED+=("$base"); continue; }

    # 1) compile: tflite/onnx -> relocated NPU binary (build/network_rel.bin)
    if ! bash "$GEN_RELOC" -m "$wf" -f "${PROFILE}@${RELOC_CONFIG_FILE}" \
              -a "$STEDGEAI_ARGS" -o "$RAW_REL_BIN" > "$BUILD_DIR/${base}_reloc.log" 2>&1; then
        log_error "reloc generation failed (see Model/build/${base}_reloc.log)"
        FAILED+=("$base")
        continue
    fi

    # 2) package: reloc bin + json config -> raw model package
    if ! python "$PACKAGER" create \
            --model "$RAW_REL_BIN" --config "weights/$base.json" \
            --output "$RAW_PKG" > "$BUILD_DIR/${base}_pkg.log" 2>&1; then
        log_error "packaging failed (see Model/build/${base}_pkg.log)"
        FAILED+=("$base")
        continue
    fi
    if ! python "$PACKAGER" validate --package "$RAW_PKG" \
            > "$BUILD_DIR/${base}_pkg.log" 2>&1; then
        log_error "raw package failed validation (see Model/build/${base}_pkg.log)"
        FAILED+=("$base")
        continue
    fi

    # 3) OTA header: 1K header, fw_type=ai_model, version/device-model stamped
    if ! python "$OTA_PACKER" "$RAW_PKG" \
            -o "$OTA_PKG" \
            -n "$base" \
            -d "${desc:-NE301 AI Model}" \
            -t ai_model \
            -v "$MODEL_VERSION" \
            -m "$DEVICE_MODEL" > "$BUILD_DIR/${base}_ota.log" 2>&1; then
        log_error "OTA packing failed (see Model/build/${base}_ota.log)"
        FAILED+=("$base")
        continue
    fi

    # 4) verify the final OTA package (header CRC / sizes / magic)
    if ! python "$VERIFIER" "$OTA_PKG" > "$BUILD_DIR/${base}_ota.log" 2>&1; then
        log_error "OTA package failed verification (see Model/build/${base}_ota.log)"
        FAILED+=("$base")
        continue
    fi

    $KEEP_TEMP || rm -f "$RAW_PKG"

    SIZE=$(stat -c%s "$OTA_PKG" 2>/dev/null || stat -f%z "$OTA_PKG" 2>/dev/null || echo 0)
    log_success "$base -> $(basename "$OTA_PKG") ($((SIZE / 1024)) KB)"
    BUILT+=("$base")
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "================================================================"
echo "Model batch build summary"
echo "================================================================"
echo "  OTA version : $MODEL_VERSION (ai_model, device 0x$(printf '%04X' "$((DEVICE_MODEL))"))"
echo "  Succeeded   : ${#BUILT[@]}/${#QUEUE[@]}"
if [[ ${#BUILT[@]} -gt 0 ]]; then
    (cd "$OUT_DIR" && ls -lh *_v"${MODEL_VERSION}"_pkg.bin 2>/dev/null) | awk '{printf "    %8s  %s\n", $5, $NF}'
fi
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo "  Failed      : ${#FAILED[@]}"
    for f in "${FAILED[@]}"; do echo "    $f"; done
    exit 1
fi
log_success "All models built into ${OUT_DIR#$REPO_ROOT/}"
exit 0
