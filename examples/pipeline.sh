#!/usr/bin/env bash
set -euo pipefail
cli=${1:-time_set}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$cli" make --length 100 '[10,20)' > "$tmp/a.json"
"$cli" make --length 100 '[15,25]' > "$tmp/b.json"

both=$("$cli" intersect "$tmp/a.json" "$tmp/b.json" --text)
[[ "$both" == '[15,20)' ]]
printf 'Intersection: %s\n' "$both"

starts=$("$cli" shift "$tmp/a.json" --by 5t | "$cli" starts --duration 4t --text)
[[ "$starts" == '[15,22)' ]]
printf 'Shift five ticks, then find four-tick starts: %s\n' "$starts"

"$cli" move "$tmp/a.json" '[10,20)' --to 40 --into "$tmp/b.json" > "$tmp/move.json"
source=$("$cli" select "$tmp/move.json" source --text)
destination=$("$cli" select "$tmp/move.json" destination --text)
[[ "$source" == 'empty' ]]
[[ "$destination" == '[15,26) | [40,50)' ]]
printf 'Cross-set move: source = %s; destination = %s\n' "$source" "$destination"
