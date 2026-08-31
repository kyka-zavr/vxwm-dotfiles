#!/bin/sh
# Right cluster: wifi status as custom/script instead of internal/network —
# same reason as date.sh, internal/network's click-left wasn't firing reliably.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
FG="${foreground:-#c2c0c0}"
DIM="${color8:-#665453}"
ICON=""

while true; do
	ssid=$(nmcli -t -f active,ssid dev wifi 2>/dev/null | awk -F: '$1=="yes"{print $2; exit}')
	if [ -n "$ssid" ]; then
		printf '%%{T2}%%{F%s}%s%%{F-}%%{T-} %s\n' "$FG" "$ICON" "$ssid"
	else
		printf '%%{T2}%%{F%s}%s%%{F-}%%{T-} off\n' "$DIM" "$ICON"
	fi
	sleep 3
done
