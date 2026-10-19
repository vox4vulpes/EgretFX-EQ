#!/usr/bin/env bash
# Builds a Windows installer for every effect staged in <stage dir>.
# Runs in Git Bash on a Windows CI runner.
#
# Usage: build-installer.sh <version> <stage dir> <output dir> [<app name> <app key> <output name>]
#   <stage dir> holds one folder per effect, each with VST3/ and optionally
#   Standalone/, as laid out by the release workflow. Without the last three
#   arguments it builds the bundle installer (see the defaults in the .iss).
set -euo pipefail

version=$1
stage=$2
out=$3
product_defines=()
if [ $# -ge 6 ]; then
  product_defines=("-DAppName=$4" "-DAppKey=$5" "-DOutputName=$6")
fi
here=$(cd "$(dirname "$0")" && pwd)

work=$(mktemp -d)
mkdir -p "$work/VST3" "$work/Standalone" "$work/Licenses" "$out"
icons="$work/icons.iss"
: > "$icons"

for fx_dir in "$stage"/*/; do
  cp -R "${fx_dir}VST3/." "$work/VST3/"
  if [ -f "${fx_dir}LICENSE.txt" ]; then
    cp "${fx_dir}LICENSE.txt" "$work/Licenses/$(basename "$fx_dir")-LICENSE.txt"
  fi
  for exe in "${fx_dir}"Standalone/*.exe; do
    [ -e "$exe" ] || continue
    cp "$exe" "$work/Standalone/"
    name=$(basename "$exe" .exe)
    echo "Name: \"{group}\\$name\"; Filename: \"{app}\\$name.exe\"" >> "$icons"
  done
done

iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
if [ ! -f "$iscc" ]; then
  choco install innosetup -y --no-progress > /dev/null
fi

# ISCC accepts '-' as well as '/' for switches; '-' avoids Git Bash path mangling.
"$iscc" -Qp \
  "-DAppVersion=$version" \
  "${product_defines[@]}" \
  "-DStageDir=$(cygpath -w "$work")" \
  "-DIconsFile=$(cygpath -w "$icons")" \
  "-O$(cygpath -w "$out")" \
  "$(cygpath -w "$here/EgretFxBundle.iss")"
