#!/bin/zsh
set -euo pipefail

project_root="${0:A:h}"
source_dir="${project_root}/build-macos/package/Pattern Bank"
installer="${project_root}/Packaging/Install-PatternBank-macOS.command"

[[ -f "${source_dir}/Pattern Bank_x64.dylib" ]] || {
  echo 'Build output not found. Run ./Build-Mac.sh first.'
  exit 1
}

PATTERN_BANK_SOURCE_DIR="${source_dir}" "${installer}" "$@"
