#!/usr/bin/env bash
# Prints the CHANGELOG.md section for a version (without its "## [x.y.z]"
# heading). Fails if there is no such section or it is empty, so a release
# can't go out without its list of changes.
#
# Usage: changelog-section.sh <version> [changelog file]
set -euo pipefail

version=${1#v}
file=${2:-CHANGELOG.md}

section=$(awk -v heading="## [$version]" '
  index($0, heading) == 1 { found = 1; next }
  found && /^## \[/       { exit }
  found                   { print }
' "$file")

if [ -z "$(echo "$section" | tr -d '[:space:]')" ]; then
  echo "No changes listed for $version in $file. Add a \"## [$version]\" section first." >&2
  exit 1
fi

# Trim leading and trailing blank lines.
echo "$section" | sed -e '/./,$!d' | tac | sed -e '/./,$!d' | tac
