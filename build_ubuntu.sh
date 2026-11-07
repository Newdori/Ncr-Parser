#!/bin/sh
set -eu

if [ "$(uname -s)" != 'Linux' ]; then
    printf '%s\n' 'This build script requires Ubuntu/Linux.' >&2
    exit 2
fi

TaskRoot=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
cd "$TaskRoot"

if [ "$#" -gt 1 ]; then
    printf '%s\n' 'Usage: sh ./build_ubuntu.sh [build|test|example|asan|ubsan|tsan|clean|help]' >&2
    exit 2
fi

TaskMode=${1:-test}
case "$TaskMode" in
    build) set -- all ;;
    test) set -- test example ;;
    example|asan|ubsan|tsan|clean|help) set -- "$TaskMode" ;;
    *)
        printf '%s\n' "Unknown build mode: $TaskMode" >&2
        exit 2
        ;;
esac

if ! command -v make >/dev/null 2>&1; then
    printf '%s\n' 'GNU make is required. Install build-essential on Ubuntu.' >&2
    exit 2
fi

exec make --no-print-directory "$@"
