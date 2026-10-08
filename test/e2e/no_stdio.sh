#!/usr/bin/env bash
# Proves the rubric's I/O restriction: src/ calls none of the stdio I/O
# functions (snprintf/sprintf format into memory and are allowed) and never
# names the stdio streams.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$(dirname "$SCRIPT_DIR")")"
SRC_DIR="$REPO/src"

FORBIDDEN_CALLS='printf|fprintf|vprintf|vfprintf|dprintf|puts|fputs|putchar|putc|fputc'
FORBIDDEN_CALLS="$FORBIDDEN_CALLS|fopen|fdopen|freopen|fclose|fgets|gets|getc|fgetc|getchar"
FORBIDDEN_CALLS="$FORBIDDEN_CALLS|fread|fwrite|fflush|fseek|ftell|rewind|perror|scanf|fscanf|sscanf"
FORBIDDEN_STREAMS='stdin|stdout|stderr'

src_calls_a_forbidden_function() {
    grep -Ewn "($FORBIDDEN_CALLS)[[:space:]]*\(" "$SRC_DIR"/*.c "$SRC_DIR"/*.h
}

src_names_a_stdio_stream() {
    grep -Ewn "$FORBIDDEN_STREAMS" "$SRC_DIR"/*.c "$SRC_DIR"/*.h
}

main() {
    if src_calls_a_forbidden_function; then
        echo "FAIL no_stdio: forbidden I/O call in src/" >&2
        exit 1
    fi
    if src_names_a_stdio_stream; then
        echo "FAIL no_stdio: stdio stream named in src/" >&2
        exit 1
    fi
    echo "PASS no_stdio: src/ uses only open, close, read, write and lseek for I/O"
}

main "$@"
