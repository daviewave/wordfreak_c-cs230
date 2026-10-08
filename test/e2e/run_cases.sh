#!/usr/bin/env bash
# Runs every test/e2e/cases/<name>.cmd in a scratch copy of the cases
# directory and compares the resulting output.txt with <name>.expected.
# Optional <name>.stderr holds a grep -E pattern the error output must
# contain; optional <name>.status holds the expected exit status (default 0);
# a case without <name>.expected asserts that no output.txt was written.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"
CASES_DIR="$SCRIPT_DIR/cases"
WORDFREAK="$REPO/build/wordfreak"
CASE_TIMEOUT_SECONDS=10
SCRATCH_ROOT="${TMPDIR:-/tmp}/wordfreak_e2e_$$"
export WORDFREAK

failures=0

remove_scratch_on_exit() {
    trap 'rm -rf "$SCRATCH_ROOT"' EXIT
}

require_built_binary() {
    [ -x "$WORDFREAK" ] || { echo "run_cases: $WORDFREAK is not built" >&2; exit 1; }
}

fresh_scratch_copy_of_cases() {
    local name="$1" scratch="$SCRATCH_ROOT/$name"
    rm -rf "$scratch"
    mkdir -p "$scratch"
    cp "$CASES_DIR"/*.txt "$scratch/"
    echo "$scratch"
}

run_command_in() {
    local scratch="$1" command_file="$2"
    (cd "$scratch" && timeout "$CASE_TIMEOUT_SECONDS" bash -c "$(cat "$command_file")" \
        2> "$scratch/stderr.log") || return $?
}

expected_status_of() {
    local name="$1"
    if [ -f "$CASES_DIR/$name.status" ]; then
        cat "$CASES_DIR/$name.status"
    else
        echo 0
    fi
}

output_differs() {
    local name="$1" scratch="$2"
    if [ ! -f "$CASES_DIR/$name.expected" ]; then
        [ -e "$scratch/output.txt" ] && echo "output.txt exists" > "$scratch/diff.log"
        return
    fi
    ! diff -u "$CASES_DIR/$name.expected" "$scratch/output.txt" > "$scratch/diff.log" 2>&1
}

stderr_lacks_required_pattern() {
    local name="$1" scratch="$2"
    [ -f "$CASES_DIR/$name.stderr" ] || return 1
    ! grep -E -q -f "$CASES_DIR/$name.stderr" "$scratch/stderr.log"
}

fail_case() {
    local name="$1" reason="$2" scratch="$3"
    echo "FAIL case $name: $reason"
    sed 's/^/    /' "$scratch/diff.log" 2>/dev/null | head -20 || true
    sed 's/^/    stderr: /' "$scratch/stderr.log" 2>/dev/null | head -5 || true
    failures=$((failures + 1))
}

run_case() {
    local command_file="$1"
    local name scratch status expected_status
    name="$(basename "$command_file" .cmd)"
    scratch="$(fresh_scratch_copy_of_cases "$name")"
    status=0
    run_command_in "$scratch" "$command_file" || status=$?
    expected_status="$(expected_status_of "$name")"
    if [ "$status" -ne "$expected_status" ]; then
        fail_case "$name" "exit status $status, expected $expected_status" "$scratch"
    elif output_differs "$name" "$scratch"; then
        fail_case "$name" "output.txt differs from expected" "$scratch"
    elif stderr_lacks_required_pattern "$name" "$scratch"; then
        fail_case "$name" "stderr lacks $(cat "$CASES_DIR/$name.stderr")" "$scratch"
    else
        echo "PASS case $name"
    fi
}

run_every_case() {
    local command_file
    for command_file in "$CASES_DIR"/*.cmd; do
        run_case "$command_file"
    done
}

fail_if_any_case_failed() {
    [ "$failures" -eq 0 ]
}

main() {
    remove_scratch_on_exit
    require_built_binary
    run_every_case
    fail_if_any_case_failed
}

main "$@"
