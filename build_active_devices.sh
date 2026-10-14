#!/bin/bash
# Repurposed (temp): build a static aarch64 kexec binary and drop it as ./Mu-kexec
# so the existing active_devices.yml artifact upload (path ./Mu-*) picks it up.
set -euxo pipefail
sudo apt-get update
sudo apt-get install -y autoconf automake libtool pkg-config gcc-aarch64-linux-gnu libc6-dev-arm64-cross
git clone --depth 1 https://github.com/horms/kexec-tools.git kx
cd kx
./bootstrap
./configure --host=aarch64-linux-gnu --without-zlib --without-lzma --without-xen \
  CC=aarch64-linux-gnu-gcc LDFLAGS="-static"
make -j"$(nproc)"
aarch64-linux-gnu-strip build/sbin/kexec
cp build/sbin/kexec "${GITHUB_WORKSPACE:-..}/Mu-kexec"
cd "${GITHUB_WORKSPACE:-..}"
echo "=== result ==="; ls -la Mu-kexec; file Mu-kexec || true
