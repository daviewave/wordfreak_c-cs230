#!/usr/bin/env bash
# Builds every unit test in test/unit/ against the sources (minus main.c),
# runs each, then runs every e2e script in test/e2e/. Prints one PASS/FAIL
# line per test and exits non-zero if anything failed. `make test` calls
# this and nothing else; CC and TEST_CFLAGS come from the Makefile.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$SCRIPT_DIR")"
export REPO

CC="${CC:-gcc}"
TEST_CFLAGS="${TEST_CFLAGS:--std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O0 -g3}"
TEST_BUILD_DIR="$REPO/build/test"
UNIT_DIR="$REPO/test/unit"
E2E_DIR="$REPO/test/e2e"

failures=0
total=0

prepare_build_directory() {
    mkdir -p "$TEST_BUILD_DIR"
}

# Sources every unit test links against: all of src/ except the entry point.
module_sources() {
    find "$REPO/src" -name '*.c' ! -name 'main.c' | sort
}

compile_unit_test() {
    local source="$1" binary="$2"
    # shellcheck disable=SC2046,SC2086
    $CC $TEST_CFLAGS -o "$binary" "$source" $(module_sources)
}

record_result() {
    local name="$1" passed="$2"
    total=$((total + 1))
    if [ "$passed" -eq 0 ]; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        failures=$((failures + 1))
    fi
}

run_unit_test() {
    local source="$1"
    local name binary passed=0
    name="$(basename "$source" .c)"
    binary="$TEST_BUILD_DIR/$name"
    compile_unit_test "$source" "$binary" && TMPDIR="${TMPDIR:-/tmp}" "$binary" || passed=1
    record_result "$name" "$passed"
}

run_e2e_script() {
    local script="$1"
    local name passed=0
    name="$(basename "$script" .sh)"
    bash "$script" || passed=1
    record_result "e2e/$name" "$passed"
}

run_every_unit_test() {
    local source
    for source in "$UNIT_DIR"/test_*.c; do
        [ -e "$source" ] || continue
        run_unit_test "$source"
    done
}

run_every_e2e_script() {
    local script
    for script in "$E2E_DIR"/*.sh; do
        [ -e "$script" ] || continue
        run_e2e_script "$script"
    done
}

report_totals_and_fail_if_anything_failed() {
    echo "run_tests: $total tests, $failures failures"
    [ "$failures" -eq 0 ]
}

main() {
    prepare_build_directory
    run_every_unit_test
    run_every_e2e_script
    report_totals_and_fail_if_anything_failed
}

main "$@"
