#!/bin/zsh
set -euo pipefail

project_root="${0:A:h}"
build_dir="${project_root}/build-macos"

cmake -S "${project_root}" -B "${build_dir}" -G Xcode \
  -DSTEPSHAPER_BUILD_FL_PLUGIN=ON \
  -DSTEPSHAPER_BUILD_PREVIEW=ON \
  -DSTEPSHAPER_BUILD_TESTS=ON \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

cmake --build "${build_dir}" --config Release
ctest --test-dir "${build_dir}" -C Release --output-on-failure

plugin="${build_dir}/package/Pattern Bank/Pattern Bank_x64.dylib"
codesign --force --sign - "${plugin}"
codesign --verify --strict --verbose=2 "${plugin}"
lipo -verify_arch arm64 x86_64 "${plugin}"

bad_dependencies="$(otool -L "${plugin}" | tail -n +2 | awk '{print $1}' | \
  grep -Ev '^(/System/Library/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' || true)"
if [[ -n "${bad_dependencies}" ]]; then
  echo "Unexpected external dependencies:"
  echo "${bad_dependencies}"
  exit 1
fi

xattr -dr com.apple.quarantine "${build_dir}/package" 2>/dev/null || true
chmod +x "${build_dir}/package/Install-PatternBank-macOS.command"
chmod +x "${build_dir}/package/Diagnose-PatternBank-macOS.command"

echo "Built and verified universal macOS plugin: ${plugin}"
