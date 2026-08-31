#!/bin/sh
export PATH="$HOME/.local/bin:$PATH"

# Keyboard layout: RU/EN, toggled with MOD+Space (matches vxwm's MODKEY = Super)
setxkbmap -layout us,ru -option grp:win_space_toggle

xset r rate 200 40

# Some drivers report the real panel DPI (~144 on 15" 1080p) → huge UI. Lock to classic 96.
xrandr --dpi 96
echo 'Xft.dpi: 96' | xrdb -merge

export CM_DIR="$HOME/.cache/clipmenu"
mkdir -p "$CM_DIR"
clipmenud &

while true; do
	xsetroot -name " vxwm | $(date '+%H:%M') "
	sleep 10
done &

# wallpaper + colors before compositor/wm
LASTWAL="$HOME/.cache/wal/wal"
if [ -f "$LASTWAL" ]; then
	WAL="$(cat "$LASTWAL")"
	wal -i "$WAL" -n -q 2>/dev/null
	feh --bg-fill "$WAL"
	xrdb -merge "$HOME/.cache/wal/colors.Xresources"
	xrdb -merge "$HOME/.cache/wal/dwm.Xresources"
fi

dunst &
picomlaunch &

# top bar (polybar) shows wifi/bluetooth/volume directly — no separate tray applets
barlaunch &

vxwm &
VXWM_PID=$!
sleep 1
xdotool key --clearmodifiers Super_L+F5 2>/dev/null
wait $VXWM_PID
kill $(jobs -p) 2>/dev/null
wait 2>/dev/null
