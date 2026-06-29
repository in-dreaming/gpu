#!/usr/bin/env bash
# Mirror .github/workflows/ci.yml for local Linux / WSL2 runs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-Release}"
BUILD_DIR="${2:-$ROOT/build/ci-linux-$CONFIG}"

export SLANG_RHI_ENABLE_WGPU=OFF
export SLANG_RHI_ENABLE_CUDA=OFF
export SLANG_RHI_ENABLE_D3D11=OFF
export SLANG_RHI_ENABLE_OPTIX=OFF

APT_PACKAGES=(
  cmake ninja-build
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
  libxss-dev libxxf86vm-dev libxtst-dev libxfixes-dev libdrm-dev libgbm-dev
  libwayland-dev libxkbcommon-dev libegl-dev libdbus-1-dev libibus-1.0-dev
  mesa-vulkan-drivers vulkan-tools libvulkan1 xvfb
)

bootstrap_local_toolchain() {
  local tools="${HOME}/.local/ci-tools"
  mkdir -p "${tools}"

  if ! command -v ninja >/dev/null 2>&1; then
    if [[ ! -x "${tools}/ninja" ]]; then
      echo "Bootstrapping ninja to ${tools} ..."
      curl -fsSL -o /tmp/ninja-linux.zip \
        https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-linux.zip
      python3 -c "import zipfile; zipfile.ZipFile('/tmp/ninja-linux.zip').extractall('${tools}')"
      chmod +x "${tools}/ninja"
    fi
    export PATH="${tools}:${PATH}"
  fi

  local need_cmake_bootstrap=0
  if ! command -v cmake >/dev/null 2>&1; then
    need_cmake_bootstrap=1
  else
    local cmake_major cmake_minor
    IFS=. read -r cmake_major cmake_minor _ < <(cmake --version | awk '/version/{print $3; exit}')
    if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 24) )); then
      need_cmake_bootstrap=1
    fi
  fi

  if (( need_cmake_bootstrap )); then
    if [[ ! -x "${tools}/cmake/bin/cmake" ]]; then
      local cmake_ver=3.31.6
      echo "Bootstrapping CMake ${cmake_ver} to ${tools}/cmake ..."
      curl -fsSL -o /tmp/cmake-linux.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${cmake_ver}/cmake-${cmake_ver}-linux-x86_64.tar.gz"
      mkdir -p "${tools}/cmake"
      tar -xzf /tmp/cmake-linux.tar.gz -C "${tools}/cmake" --strip-components=1
    fi
    export PATH="${tools}/cmake/bin:${PATH}"
  fi
}

ensure_linux_packages() {
  command -v apt-get >/dev/null 2>&1 || return 0

  local missing=()
  for pkg in "${APT_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
      missing+=("$pkg")
    fi
  done

  if ((${#missing[@]} == 0)); then
    return 0
  fi

  echo "Missing apt packages: ${missing[*]}"
  if sudo -n true 2>/dev/null; then
    echo "Installing via passwordless sudo ..."
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends "${missing[@]}"
    return 0
  fi

  cat <<EOF
WSL/Linux CI needs system packages that require sudo (one-time setup).

Open a WSL terminal and run:

  sudo apt-get update
  sudo apt-get install -y --no-install-recommends \\
    cmake ninja-build \\
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \\
    libwayland-dev libxkbcommon-dev libegl-dev libdbus-1-dev libibus-1.0-dev \\
    mesa-vulkan-drivers vulkan-tools libvulkan1 xvfb

Then re-run: $0 $CONFIG
EOF
  exit 1
}

setup_vulkan_sdk() {
  if [[ -n "${VULKAN_SDK:-}" ]]; then
    return 0
  fi
  if [[ -d "${HOME}/VulkanSDK" ]]; then
    local latest_sdk
    latest_sdk="$(ls -1d "${HOME}/VulkanSDK"/*/ 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -n "${latest_sdk}" ]]; then
      export VULKAN_SDK="${latest_sdk%/}"
      export PATH="${VULKAN_SDK}/bin:${PATH}"
      export LD_LIBRARY_PATH="${VULKAN_SDK}/lib:${LD_LIBRARY_PATH:-}"
    fi
  fi
}

echo "=== CI local (Linux/WSL): $CONFIG ==="
echo "Build dir: $BUILD_DIR"

bootstrap_local_toolchain
ensure_linux_packages
setup_vulkan_sdk

if [[ -d "$BUILD_DIR" ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DGPU_BUILD_TESTS=ON \
  -DGPU_BUILD_EXAMPLES=OFF \
  -DGPU_INSTALL=OFF \
  -DSLANG_RHI_ENABLE_WGPU=OFF \
  -DSLANG_RHI_ENABLE_CUDA=OFF \
  -DSLANG_RHI_ENABLE_D3D11=OFF \
  -DSLANG_RHI_ENABLE_OPTIX=OFF

cmake --build "$BUILD_DIR" --parallel

if command -v xvfb-run >/dev/null 2>&1; then
  xvfb-run -a ctest --test-dir "$BUILD_DIR" --output-on-failure --timeout 300 --parallel 2
else
  echo "xvfb-run not found; running ctest with SDL_VIDEODRIVER=dummy"
  SDL_VIDEODRIVER=dummy ctest --test-dir "$BUILD_DIR" --output-on-failure --timeout 300 --parallel 2
fi

echo "=== CI local (Linux/WSL): $CONFIG PASSED ==="
