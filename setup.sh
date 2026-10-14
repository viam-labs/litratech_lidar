#!/usr/bin/env bash
# Install build dependencies for a local build or a Viam cloud build.
# Linux cloud builds default to an older distro, so CMake and gRPC are upgraded
# when the packaged versions are below what the Viam C++ SDK requires.

set -euo pipefail

cd "$(dirname "$0")"

if [[ ! -f third_party/ldcp_sdk/src/device.cpp ]]; then
  git submodule update --init --recursive
fi

if [[ "$(uname)" == "Darwin" ]]; then
  brew install cmake ninja pkg-config boost grpc protobuf abseil
  exit 0
fi

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  curl \
  git \
  ninja-build \
  pkg-config \
  libboost-all-dev \
  libc-ares-dev \
  libre2-dev \
  libssl-dev \
  zlib1g-dev

version_ge() {
  [[ "$(printf '%s\n' "$2" "$1" | sort -V | head -1)" == "$2" ]]
}

apt_candidate() {
  apt-cache policy "$1" | awk '/Candidate:/ { print $2; exit }'
}

cmake_ver="$(cmake --version 2>/dev/null | awk 'NR==1 { print $3 }' || true)"
if [[ -z "${cmake_ver}" ]] || ! version_ge "${cmake_ver}" "3.25"; then
  case "$(uname -m)" in
    x86_64) cmake_arch=x86_64 ;;
    aarch64) cmake_arch=aarch64 ;;
    *)
      echo "unsupported architecture $(uname -m)" >&2
      exit 1
      ;;
  esac
  curl -fsSL -o /tmp/cmake.tgz \
    "https://github.com/Kitware/CMake/releases/download/v3.30.8/cmake-3.30.8-linux-${cmake_arch}.tar.gz"
  tar -C /usr/local --strip-components=1 -xzf /tmp/cmake.tgz
fi

grpc_ver="$(apt_candidate libgrpc++-dev)"
grpc_ver="${grpc_ver%%-*}"
if [[ -n "${grpc_ver}" && "${grpc_ver}" != "(none)" ]] && version_ge "${grpc_ver}" "1.51.1"; then
  apt-get install -y --no-install-recommends \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc
  exit 0
fi

src_dir="${HOME}/opt/src"
mkdir -p "${src_dir}"
rm -rf "${src_dir}/grpc"
git clone --recurse-submodules -b v1.51.1 --depth 1 --shallow-submodules \
  https://github.com/grpc/grpc "${src_dir}/grpc"
cmake -S "${src_dir}/grpc" -B "${src_dir}/grpc/build" -G Ninja \
  -DgRPC_ZLIB_PROVIDER=package \
  -DgRPC_CARES_PROVIDER=package \
  -DgRPC_RE2_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DCMAKE_INSTALL_RPATH=/usr/local/lib
cmake --build "${src_dir}/grpc/build" --target install
ldconfig
rm -rf "${src_dir}/grpc"
