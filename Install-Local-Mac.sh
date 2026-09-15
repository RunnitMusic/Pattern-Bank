#!/bin/zsh
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo 'Usage: ./Install-Local-Mac.sh "/Applications/FL Studio 2026.app"'
  exit 2
fi

project_root="${0:A:h}"
source_dir="${project_root}/build-macos/package/Pattern Bank"
fl_app="$1"
destination="${fl_app}/Contents/Resources/FL/Plugins/Fruity/Effects/Pattern Bank"

if [[ ! -f "${source_dir}/Pattern Bank_x64.dylib" ]]; then
  echo "Build output not found. Run ./Build-Mac.sh first."
  exit 1
fi

if [[ ! -d "${fl_app}/Contents/Resources/FL/Plugins/Fruity/Effects" ]]; then
  echo "That does not look like an FL Studio application: ${fl_app}"
  exit 1
fi

sudo mkdir -p "${destination}"
sudo ditto "${source_dir}" "${destination}"
echo "Installed Pattern Bank to: ${destination}"
