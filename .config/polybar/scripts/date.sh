#!/bin/sh
# Center module: date/time as custom/script instead of internal/date — the
# internal module's click-left wasn't reliably firing, custom/script's does.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
FG="${foreground:-#c2c0c0}"
ICON=""

while true; do
	printf '%%{T2}%s%%{T-} %s\n' "$ICON" "$(date '+%a %d %b  %H:%M:%S')"
	sleep 1
done
