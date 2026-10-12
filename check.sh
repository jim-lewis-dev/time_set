#!/usr/bin/env bash
# Build locally and run the tests. Does not install packages or change other projects.
set -euo pipefail
cd "$(dirname "$0")"
build=build
python=ON
sanitize=OFF
case ${1:-} in
  '') ;;
  --cpp-only) build=build-cpp; python=OFF ;;
  --sanitize) build=build-sanitize; python=OFF; sanitize=ON ;;
  -h|--help)
    printf '%s\n' 'Usage: ./check.sh [--cpp-only | --sanitize]' \
      'Default: C++, CLI, native Python, examples, and documentation tests.' \
      '--cpp-only: no Python development headers required.' \
      '--sanitize: C++/CLI address, undefined-behavior, and leak checks.'
    exit 0 ;;
  *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
esac
if [[ $# -gt 1 ]]; then printf 'Too many arguments.\n' >&2; exit 2; fi
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DTIME_SET_BUILD_TESTS=ON -DTIME_SET_BUILD_PYTHON="$python" \
    -DTIME_SET_SANITIZERS="$sanitize" -DTIME_SET_WARNINGS_AS_ERRORS=ON
cmake --build "$build" --parallel "${JOBS:-2}"
if [[ $sanitize == ON ]]; then
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} \
    UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1} \
      ctest --test-dir "$build" --output-on-failure --parallel "${JOBS:-2}"
else
    ctest --test-dir "$build" --output-on-failure --parallel "${JOBS:-2}"
fi
