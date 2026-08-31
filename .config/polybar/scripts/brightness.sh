#!/bin/sh
# Far-left vertical-ish brightness gauge (single block char, 8 height levels).
# Scroll on it (bound in config.ini) calls `bright up`/`bright down`.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
FG="${color4:-#88c0d0}"

L0="▁"; L1="▂"; L2="▃"; L3="▄"
L4="▅"; L5="▆"; L6="▇"; L7="█"

pct=$(bright get 2>/dev/null)
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

printf '%%{F%s}%s%%{F-}\n' "$FG" "$ch"
