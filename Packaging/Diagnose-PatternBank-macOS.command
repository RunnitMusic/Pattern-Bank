#!/bin/zsh
set -u

failures=0
print -- "macOS: $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
print -- "Machine: $(uname -m)"

typeset -a fl_apps
fl_apps=()
if (( $# > 0 )); then
  fl_apps=("$@")
else
  for search_root in /Applications "${HOME}/Applications"; do
    [[ -d "${search_root}" ]] || continue
    while IFS= read -r app; do
      fl_apps+=("${app}")
    done < <(find "${search_root}" -maxdepth 1 -type d -name 'FL Studio*.app' -print)
  done
fi

if (( ${#fl_apps[@]} == 0 )); then
  print -u2 -- '[FAIL] No FL Studio application bundles were found.'
  exit 1
fi

for fl_app in "${fl_apps[@]}"; do
  plugin="${fl_app}/Contents/Resources/FL/Plugins/Fruity/Effects/Pattern Bank/Pattern Bank_x64.dylib"
  print -- "\nChecking: ${fl_app}"
  if [[ ! -f "${plugin}" ]]; then
    print -u2 -- '[FAIL] Pattern Bank_x64.dylib is missing.'
    (( failures += 1 ))
    continue
  fi

  archs="$(lipo -archs "${plugin}" 2>/dev/null || true)"
  if [[ " ${archs} " == *' arm64 '* && " ${archs} " == *' x86_64 '* ]]; then
    print -- "[OK] Universal architectures: ${archs}"
  else
    print -u2 -- "[FAIL] Expected arm64 and x86_64; found: ${archs}"
    (( failures += 1 ))
  fi

  if codesign --verify --strict --verbose=2 "${plugin}" >/dev/null 2>&1; then
    print -- '[OK] Code signature verifies.'
  else
    print -u2 -- '[FAIL] Code signature verification failed.'
    (( failures += 1 ))
  fi

  if xattr -p com.apple.quarantine "${plugin}" >/dev/null 2>&1; then
    print -u2 -- '[FAIL] com.apple.quarantine is present.'
    (( failures += 1 ))
  else
    print -- '[OK] No quarantine attribute.'
  fi

  bad_dependencies="$(otool -L "${plugin}" | tail -n +2 | awk '{print $1}' | \
    grep -Ev '^(/System/Library/|/usr/lib/|@rpath/|@loader_path/|@executable_path/)' || true)"
  if [[ -z "${bad_dependencies}" ]]; then
    print -- '[OK] No unexpected external library dependencies.'
  else
    print -u2 -- "[FAIL] Unexpected dependencies:\n${bad_dependencies}"
    (( failures += 1 ))
  fi
done

if (( failures > 0 )); then
  print -u2 -- "\nDiagnostics completed with ${failures} failure(s)."
  exit 1
fi

print -- '\nDiagnostics passed.'

