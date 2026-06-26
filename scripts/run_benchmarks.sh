#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  scripts/run_benchmarks.sh [repetitions] [benchmark args...]
  scripts/run_benchmarks.sh -r|--repetitions N [benchmark args...]

Examples:
  scripts/run_benchmarks.sh
  scripts/run_benchmarks.sh 10 --benchmark_filter=BM_Format
  scripts/run_benchmarks.sh --repetitions 3 --benchmark_min_time=0.2s

Environment:
  CMAKE_BUILD_TYPE          CMake build type, default: Release
  BENCHMARK_REPETITIONS    Default repetitions when no CLI value is provided
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
build_type="${CMAKE_BUILD_TYPE:-Release}"
repetitions="${BENCHMARK_REPETITIONS:-1}"
benchmark_args=()

if [[ $# -gt 0 && "$1" =~ ^[0-9]+$ ]]; then
  repetitions="$1"
  shift
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -r|--repetitions)
      if [[ $# -lt 2 ]]; then
        echo "error: $1 requires a value" >&2
        exit 2
      fi
      repetitions="$2"
      shift 2
      ;;
    --repetitions=*)
      repetitions="${1#*=}"
      shift
      ;;
    --)
      shift
      benchmark_args+=("$@")
      break
      ;;
    *)
      benchmark_args+=("$1")
      shift
      ;;
  esac
done

if [[ ! "$repetitions" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: repetitions must be a positive integer" >&2
  exit 2
fi

cmake -S "$repo_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE="$build_type" \
  -DLOGHTNING_BUILD_BENCHMARKS=ON

cmake --build "$build_dir" --target loghtning_benchmarks

"$build_dir/loghtning_benchmarks" \
  "--benchmark_repetitions=${repetitions}" \
  "${benchmark_args[@]}"
