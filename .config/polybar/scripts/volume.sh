#!/bin/sh
# Far-right vertical-ish volume gauge (single block char, 8 height levels).
# Scroll on it (bound in config.ini) calls `vol up`/`vol down`, click toggles mute.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
ON="${color4:-#88c0d0}"
OFF="${color8:-#666666}"

L0="▁"; L1="▂"; L2="▃"; L3="▄"
L4="▅"; L5="▆"; L6="▇"; L7="█"

muted=0
pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -q yes && muted=1

pct=$(vol get 2>/dev/null)
case "$pct" in '' | *[!0-9]*) pct=0 ;; esac

idx=$((pct * 7 / 100))
[ "$idx" -lt 0 ] && idx=0
[ "$idx" -gt 7 ] && idx=7

case "$idx" in
0) ch="$L0" ;;
1) ch="$L1" ;;
2) ch="$L2" ;;
3) ch="$L3" ;;
4) ch="$L4" ;;
5) ch="$L5" ;;
6) ch="$L6" ;;
7) ch="$L7" ;;
esac

if [ "$muted" -eq 1 ]; then
	printf '%%{F%s}%s%%{F-}\n' "$OFF" "$L0"
else
	printf '%%{F%s}%s%%{F-}\n' "$ON" "$ch"
fi
