#!/bin/bash
# Linux CUDA build (-> build_cuda/spirula). Extra args go to CMake.
#   CUDA_ARCH=6.1   GPU compute capability (default: what nvidia-smi reports)
#   HOST_CC=13      gcc version nvcc compiles host code with
set -euo pipefail

cd "$(dirname "$0")"

# nvcc 12.4 rejects gcc newer than 13.
HOST_CC=${HOST_CC:-13}
if ! command -v "g++-$HOST_CC" >/dev/null; then
    echo "g++-$HOST_CC not found -- sudo apt install g++-$HOST_CC" >&2
    exit 1
fi

arch_args=()
if [ -n "${CUDA_ARCH:-}" ]; then
    arch_args=(-DTORCH_CUDA_ARCH_LIST="$CUDA_ARCH")
fi

# nvcc 12.4's front end has no __builtin_ia32_ldtilecfg, which gcc 13.3+'s
# amxtileintrin.h calls; claiming its include guard skips it (no AMX is used).
bash build_develop.bash \
    -DSS_BACKEND=cuda \
    "${arch_args[@]}" \
    -DCMAKE_C_COMPILER="gcc-$HOST_CC" \
    -DCMAKE_CXX_COMPILER="g++-$HOST_CC" \
    -DCMAKE_CUDA_HOST_COMPILER="g++-$HOST_CC" \
    -DCMAKE_CUDA_FLAGS=-D_AMXTILEINTRIN_H_INCLUDED \
    "$@"
