#!/usr/bin/env bash
# =============================================================================
# stedgeai-use.sh - Switch STEdgeAI variant for NE301 builds (Git Bash / Linux)
# =============================================================================
# Usage:
#   source Script/stedgeai-use.sh    # load functions into current shell
#   stedgeai-use 4.0                 # set STEDGEAI_VARIANT + STEDGEAI_CORE_DIR
#   stedgeai-use show                # print current selection
#   stedgeai-use paths               # print configured install directories
#
#   ./Script/stedgeai-use.sh 4.0     # one-shot (exports for child processes only)
#
# Configure install paths (pick one):
#   1. Create Script/stedgeai-use.local.sh (gitignored), e.g.:
#        STEDGEAI_22_DIR="/c/ST/X-CUBE-AI/2.2.0"
#        STEDGEAI_30_DIR="/c/ST/X-CUBE-AI/3.0.0"
#        STEDGEAI_40_DIR="/c/ST/X-CUBE-AI/4.0.1"
#   2. Or export STEDGEAI_22_DIR / STEDGEAI_30_DIR / STEDGEAI_40_DIR in ~/.bashrc
# =============================================================================


if [[ -z "${_STEDGEAI_USE_LOADED:-}" ]]; then
    _STEDGEAI_USE_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    _STEDGEAI_USE_PROJECT_ROOT="$(cd "${_STEDGEAI_USE_SCRIPT_DIR}/.." && pwd)"
    _STEDGEAI_USE_LOCAL="${_STEDGEAI_USE_SCRIPT_DIR}/stedgeai-use.local.sh"

    if [[ -f "${_STEDGEAI_USE_LOCAL}" ]]; then
        # shellcheck source=/dev/null
        source "${_STEDGEAI_USE_LOCAL}"
    fi

    _STEDGEAI_USE_LOADED=1
fi

_stedgeai_use_color() {
    if [[ -t 1 ]]; then
        case "$1" in
            green)  printf '\033[0;32m' ;;
            yellow) printf '\033[1;33m' ;;
            red)    printf '\033[0;31m' ;;
            blue)   printf '\033[0;34m' ;;
            reset)  printf '\033[0m' ;;
        esac
    fi
}

_stedgeai_use_log() {
    local level="$1"
    shift
    case "$level" in
        info)    _stedgeai_use_color blue;  printf '[INFO] ' ;;
        ok)      _stedgeai_use_color green; printf '[OK] ' ;;
        warn)    _stedgeai_use_color yellow; printf '[WARN] ' ;;
        err)     _stedgeai_use_color red;   printf '[ERROR] ' ;;
    esac
    _stedgeai_use_color reset
    printf '%s\n' "$*"
}

_stedgeai_use_normalize_variant() {
    case "$1" in
        2.2|22|2)  echo "2.2" ;;
        3.0|30|3)  echo "3.0" ;;
        4.0|40|4)  echo "4.0" ;;
        *)         return 1 ;;
    esac
}

_stedgeai_use_dir_for_variant() {
    local variant="$1"
    case "$variant" in
        2.2) echo "${STEDGEAI_22_DIR:-}" ;;
        3.0) echo "${STEDGEAI_30_DIR:-}" ;;
        4.0) echo "${STEDGEAI_40_DIR:-}" ;;
    esac
}

_stedgeai_use_expected_tool_version() {
    case "$1" in
        2.2) echo "v2.2" ;;
        3.0) echo "v3.0.0" ;;
        4.0) echo "v4.0.1" ;;
    esac
}

_stedgeai_use_find_stedgeai_bin() {
    local core_dir="$1"
    local candidate
    for candidate in \
        "${core_dir}/Utilities/windows/stedgeai.exe" \
        "${core_dir}/Utilities/windows/stedgeai" \
        "${core_dir}/Utilities/linux/stedgeai"
    do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

stedgeai-use-paths() {
    _stedgeai_use_log info "ST Edge AI install directories:"
    printf '  2.2 -> %s\n' "${STEDGEAI_22_DIR:-<not set>}"
    printf '  3.0 -> %s\n' "${STEDGEAI_30_DIR:-<not set>}"
    printf '  4.0 -> %s\n' "${STEDGEAI_40_DIR:-<not set>}"
    if [[ ! -f "${_STEDGEAI_USE_LOCAL}" ]]; then
        _stedgeai_use_log warn "Optional local config not found: ${_STEDGEAI_USE_LOCAL}"
        _stedgeai_use_log info "Copy paths into that file or export STEDGEAI_*_DIR in your shell."
    fi
}

stedgeai-use-show() {
    local variant="${STEDGEAI_VARIANT:-4.0}"
    local expected
    expected="$(_stedgeai_use_expected_tool_version "$variant")"

    _stedgeai_use_log info "Current STEdgeAI selection:"
    printf '  STEDGEAI_VARIANT=%s (toolchain %s)\n' "$variant" "$expected"
    printf '  STEDGEAI_CORE_DIR=%s\n' "${STEDGEAI_CORE_DIR:-<not set>}"

    if [[ -n "${STEDGEAI_CORE_DIR:-}" ]]; then
        local bin
        if bin="$(_stedgeai_use_find_stedgeai_bin "$STEDGEAI_CORE_DIR")"; then
            printf '  stedgeai=%s\n' "$bin"
        else
            _stedgeai_use_log warn "stedgeai binary not found under STEDGEAI_CORE_DIR"
        fi
    fi
}

stedgeai-use() {
    local arg="${1:-show}"

    case "$arg" in
        show|status)
            stedgeai-use-show
            return 0
            ;;
        paths|list)
            stedgeai-use-paths
            return 0
            ;;
        help|-h|--help)
            sed -n '2,22p' "${_STEDGEAI_USE_SCRIPT_DIR}/stedgeai-use.sh" | sed 's/^# \{0,1\}//'
            return 0
            ;;
    esac

    local variant
    variant="$(_stedgeai_use_normalize_variant "$arg")" || {
        _stedgeai_use_log err "Unsupported variant: $arg (use 2.2, 3.0, or 4.0)"
        return 1
    }

    local core_dir expected
    core_dir="$(_stedgeai_use_dir_for_variant "$variant")"
    expected="$(_stedgeai_use_expected_tool_version "$variant")"

    export STEDGEAI_VARIANT="$variant"

    if [[ -n "$core_dir" ]]; then
        if [[ ! -d "$core_dir" ]]; then
            _stedgeai_use_log err "Directory does not exist for STEdgeAI ${variant}: ${core_dir}"
            return 1
        fi
        export STEDGEAI_CORE_DIR="$core_dir"
    else
        unset STEDGEAI_CORE_DIR
        _stedgeai_use_log warn "STEDGEAI_${variant//./}_DIR not set; cleared STEDGEAI_CORE_DIR"
        _stedgeai_use_log info "Add path in Script/stedgeai-use.local.sh (see .example), then re-run stedgeai-use ${variant}"
        _stedgeai_use_log info "Firmware-only builds (make app / make flash) still work without stedgeai"
    fi

    _stedgeai_use_log ok "STEDGEAI_VARIANT=${STEDGEAI_VARIANT} (expects stedgeai ${expected})"
    if [[ -n "${STEDGEAI_CORE_DIR:-}" ]]; then
        _stedgeai_use_log ok "STEDGEAI_CORE_DIR=${STEDGEAI_CORE_DIR}"
    fi

    if [[ -n "${STEDGEAI_CORE_DIR:-}" && -f "${_STEDGEAI_USE_PROJECT_ROOT}/Script/check_stedgeai_toolchain.py" ]]; then
        if ! python "${_STEDGEAI_USE_PROJECT_ROOT}/Script/check_stedgeai_toolchain.py"; then
            _stedgeai_use_log warn "Toolchain check failed (OK for firmware-only builds without stedgeai)"
            return 0
        fi
    fi

    _stedgeai_use_log info "Example: make all   or   make flash   (variant is exported for this shell)"
    return 0
}

# Allow: ./Script/stedgeai-use.sh 4.0
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    stedgeai-use "$@"
    exit $?
fi
