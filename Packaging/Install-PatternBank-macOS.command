#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
source_dir="${PATTERN_BANK_SOURCE_DIR:-${script_dir}/Pattern Bank}"
source_plugin="${source_dir}/Pattern Bank_x64.dylib"

fail() {
  print -u2 -- "ERROR: $*"
  exit 1
}

[[ -f "${source_plugin}" ]] || fail "Missing plug-in payload: ${source_plugin}"
[[ -f "${source_dir}/Plugin.nfo" ]] || fail "Missing Plugin.nfo in ${source_dir}"

if pgrep -if '/FL Studio[^/]*\.app/Contents/MacOS/' >/dev/null 2>&1; then
  fail 'Close every running FL Studio window before installing Pattern Bank.'
fi

archs="$(lipo -archs "${source_plugin}")"
[[ " ${archs} " == *' arm64 '* ]] || fail 'The payload has no Apple Silicon arm64 slice.'
[[ " ${archs} " == *' x86_64 '* ]] || fail 'The payload has no Intel x86_64 slice.'

bad_dependencies="$(otool -L "${source_plugin}" | tail -n +2 | awk '{print $1}' | \
  grep -Ev '^(/System/Library/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' || true)"
[[ -z "${bad_dependencies}" ]] || fail "Unexpected external dependencies:\n${bad_dependencies}"

typeset -a fl_apps
fl_apps=()
if (( $# > 0 )); then
  for app in "$@"; do
    fl_apps+=("${app}")
  done
else
  for search_root in /Applications "${HOME}/Applications"; do
    [[ -d "${search_root}" ]] || continue
    while IFS= read -r app; do
      fl_apps+=("${app}")
    done < <(find "${search_root}" -maxdepth 1 -type d -name 'FL Studio*.app' -print)
  done
fi

(( ${#fl_apps[@]} > 0 )) || fail 'No FL Studio application bundles were found.'

stage_root="$(mktemp -d "${TMPDIR:-/tmp}/pattern-bank-install.XXXXXX")"
trap 'rm -rf "${stage_root}"' EXIT
stage_payload="${stage_root}/Pattern Bank"
ditto "${source_dir}" "${stage_payload}"
xattr -dr com.apple.quarantine "${stage_payload}" 2>/dev/null || true
codesign --force --sign - "${stage_payload}/Pattern Bank_x64.dylib"
codesign --verify --strict --verbose=2 "${stage_payload}/Pattern Bank_x64.dylib"

installed=0
for fl_app in "${fl_apps[@]}"; do
  effects="${fl_app}/Contents/Resources/FL/Plugins/Fruity/Effects"
  [[ -d "${effects}" ]] || fail "Not a valid FL Studio application bundle: ${fl_app}"

  destination="${effects}/Pattern Bank"
  previous="${effects}/Pattern Bank.previous"

  sudo rm -rf "${previous}"
  if [[ -e "${destination}" || -L "${destination}" ]]; then
    sudo mv "${destination}" "${previous}"
  fi

  if ! sudo ditto "${stage_payload}" "${destination}"; then
    sudo rm -rf "${destination}"
    if [[ -e "${previous}" || -L "${previous}" ]]; then
      sudo mv "${previous}" "${destination}"
    fi
    fail "Installation failed and the previous copy was restored: ${fl_app}"
  fi

  sudo xattr -dr com.apple.quarantine "${destination}" 2>/dev/null || true
  sudo codesign --force --sign - "${destination}/Pattern Bank_x64.dylib"

  lipo "${destination}/Pattern Bank_x64.dylib" -verify_arch arm64
  lipo "${destination}/Pattern Bank_x64.dylib" -verify_arch x86_64
  codesign --verify --strict --verbose=2 "${destination}/Pattern Bank_x64.dylib"
  if xattr -p com.apple.quarantine "${destination}/Pattern Bank_x64.dylib" >/dev/null 2>&1; then
    fail "Quarantine is still present after installation: ${destination}"
  fi

  print -- "Installed and verified: ${destination}"
  (( installed += 1 ))
done

print -- "Pattern Bank installation completed for ${installed} FL Studio installation(s)."
print -- 'Restart FL Studio. Native plug-ins do not require a VST scan.'

