#!/usr/bin/env bash

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP_REPO=${APP_REPO:-/Users/harryhua/Documents/GitHub/ne30x-app}
APP_SHA=85e3d4a46b8b4c53a63fac3405b91efb084a3f8a
APP_STAGE=$(mktemp -d "${TMPDIR:-/tmp}/ne301-113-app30.XXXXXX")
APP_TREE_SHA=$(git -C "$APP_REPO" rev-parse "$APP_SHA^{tree}")
MATRIX_CC=${CC:-cc}

trap 'rm -rf "$APP_STAGE"' EXIT
git -C "$APP_REPO" archive "$APP_SHA" apps/line-crossing tests/line-crossing-enable tests/line-crossing-app | tar -x -C "$APP_STAGE"
make -C "$ROOT/tests/app_native_vnext" CC=cc test

HOST_BUILD="$ROOT/tests/app_native_vnext/build"
APP_DIR="$APP_STAGE/apps/line-crossing"
APP_TESTS="$APP_STAGE/tests/line-crossing-enable"
APP_STUBS="$APP_STAGE/tests/line-crossing-app"
MATRIX_CFLAGS=(
    -std=c11
    -O1
    -g
    -Wall
    -Wextra
    -Werror
    -pthread
    "-I$ROOT/tests/sign_spike/shim"
    "-I$ROOT/tests/storage_gate/shim"
    "-I$ROOT/tests/app_installer"
    "-I$ROOT/tests/app_admission"
    "-I$ROOT/tests/sign_spike"
    "-I$ROOT/Custom/Services/AppHost"
    "-I$ROOT/Custom/Common/Inc"
    "-I$ROOT/Custom/Common/Utils"
    "-I$ROOT/Custom/Common/Lib"
    "-I$ROOT/Custom/Common/Lib/MbedTLS/include"
    "-I$ROOT/Custom/Common/Lib/MbedTLS/configs"
    "-I$HOST_BUILD"
    "-I$APP_DIR/include"
    "-I$APP_DIR/app"
    "-I$APP_TESTS"
    "-I$APP_STUBS"
    '-DMBEDTLS_CONFIG_FILE="host_user_config.h"'
    -DMBEDTLS_ALLOW_PRIVATE_ACCESS
)

shopt -s nullglob
MBOBJS=("${HOST_BUILD}/"*.o)
"$MATRIX_CC" "${MATRIX_CFLAGS[@]}" \
    "$ROOT/tests/app_host_enable/test_joint_app30.c" \
    "$APP_STUBS/host_stub.c" \
    "$APP_DIR/app/lc_app_entry_v4.c" \
    "$APP_DIR/app/lc_bus.c" \
    "$APP_DIR/app/lc_bus_config.c" \
    "$APP_DIR/app/lc_stateblob.c" \
    "$APP_DIR/app/lc_ctrl.c" \
    "$APP_DIR/app/lc_cfgcodec.c" \
    "$APP_DIR/app/lc_json.c" \
    "$APP_DIR/app/lc_arena.c" \
    "$APP_DIR/src/lc_tracker.c" \
    "$APP_DIR/src/lc_line_cross.c" \
    "$ROOT/Custom/Services/AppHost/app_host_control.c" \
    "$ROOT/tests/app_native_vnext/app_installer.o" \
    "$ROOT/tests/app_native_vnext/app_state_store.o" \
    "$ROOT/tests/app_native_vnext/app_admission.o" \
    "$ROOT/tests/app_native_vnext/app_trust_anchor.o" \
    "$ROOT/tests/app_native_vnext/generic_math.o" \
    "${MBOBJS[@]}" \
    -o "$APP_STAGE/test_joint_app30"

printf 'HOST_WORKTREE_SHA=%s\n' "$(git -C "$ROOT" rev-parse HEAD)"
printf 'APP_SHA=%s\n' "$APP_SHA"
printf 'APP_TREE_SHA=%s\n' "$APP_TREE_SHA"
CC="$MATRIX_CC" bash "$APP_TESTS/run_tests.sh"
"$APP_STAGE/test_joint_app30"
