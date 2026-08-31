#!/bin/sh
# Left bar module: vxwm tags 1-5. Click switches tag (simulates the MOD+N keybind
# via xdotool, so it works regardless of whether vxwm reacts to EWMH pager requests).
# Re-renders whenever _NET_CURRENT_DESKTOP changes (xprop -spy), so it's event-driven,
# not polled.

COLORS="$HOME/.cache/wal/colors.sh"
[ -f "$COLORS" ] && . "$COLORS"
ACTIVE="${color4:-#88c0d0}"
INACTIVE="${color8:-#666666}"

render() {
	cur=$(xprop -root -notype _NET_CURRENT_DESKTOP 2>/dev/null | awk -F'= ' '{print $2}')
	cur=$((cur + 1)) # EWMH desktops are 0-indexed, vxwm tags are 1-indexed
	out=""
	i=1
	while [ "$i" -le 5 ]; do
		if [ "$i" = "$cur" ]; then
			out="$out%{F$ACTIVE}%{A1:xdotool key --clearmodifiers super+$i:} $i %{A}%{F-}"
		else
			out="$out%{F$INACTIVE}%{A1:xdotool key --clearmodifiers super+$i:} $i %{A}%{F-}"
		fi
		i=$((i + 1))
	done
	printf '%s\n' "$out"
}

render
xprop -root -spy _NET_CURRENT_DESKTOP 2>/dev/null | while IFS= read -r _; do
	render
done
