#!/bin/sh
# Right cluster: bluetooth power status. Click (bound in config.ini) opens blueman-manager.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
ON="${color4:-#88c0d0}"
OFF="${color8:-#666666}"
ICON=""

command -v bluetoothctl >/dev/null 2>&1 || exit 0

if bluetoothctl show 2>/dev/null | grep -q "Powered: yes"; then
	printf '%%{T2}%%{F%s}%s%%{F-}%%{T-} on\n' "$ON" "$ICON"
else
	printf '%%{T2}%%{F%s}%s%%{F-}%%{T-} off\n' "$OFF" "$ICON"
fi
