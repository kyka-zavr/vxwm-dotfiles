#
# ~/.bashrc — vxwm rice
#

# If not running interactively, don't do anything
[[ $- != *i* ]] && return

# Shared aliases + PATH (bash & zsh)
[ -f "$HOME/.config/rice/shell-common" ] && . "$HOME/.config/rice/shell-common"

PS1='[\u@\h \W]\$ '

# Optional smarter line editor (bash only)
[ -f "$HOME/.local/share/blesh/ble.sh" ] && source -- "$HOME/.local/share/blesh/ble.sh"
