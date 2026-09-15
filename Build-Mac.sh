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

echo "Built universal macOS plugin: ${plugin}"
