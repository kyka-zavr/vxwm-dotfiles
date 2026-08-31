#!/bin/sh
# Small mono spectrum between the center (date) and the right cluster.
# cava streams one line per frame: "3;5;2;7;...\n" (0-7 per bar, ';'-separated).

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
FG="${color4:-#88c0d0}"

L0="▁"; L1="▂"; L2="▃"; L3="▄"
L4="▅"; L5="▆"; L6="▇"; L7="█"

level_char() {
	case "$1" in
	0) printf '%s' "$L0" ;;
	1) printf '%s' "$L1" ;;
	2) printf '%s' "$L2" ;;
	3) printf '%s' "$L3" ;;
	4) printf '%s' "$L4" ;;
	5) printf '%s' "$L5" ;;
	6) printf '%s' "$L6" ;;
	*) printf '%s' "$L7" ;;
	esac
}

command -v cava >/dev/null 2>&1 || exit 0

cava -p "$HOME/.config/cava/config" 2>/dev/null | while IFS= read -r line; do
	out=""
	old_ifs=$IFS
	IFS=';'
	for v in $line; do
		[ -n "$v" ] || continue
		out="$out$(level_char "$v")"
	done
	IFS=$old_ifs
	printf '%%{F%s}%s%%{F-}\n' "$FG" "$out"
done
