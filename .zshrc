#
# ~/.zshrc — vxwm rice
#

# Oh My Zsh
export ZSH="$HOME/.oh-my-zsh"
ZSH_THEME="robbyrussell"
plugins=(git)
source "$ZSH/oh-my-zsh.sh"

# Shared aliases + PATH (bash & zsh) — vxbuild, vxconfig, vxmod, x, etc.
[ -f "$HOME/.config/rice/shell-common" ] && . "$HOME/.config/rice/shell-common"

# History
HISTFILE="$HOME/.zsh_history"
HISTSIZE=10000
SAVEHIST=10000
setopt HIST_IGNORE_DUPS SHARE_HISTORY

# Fish-like conveniences (ghost-text suggestions + live syntax highlighting)
[ -f /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ] && \
	source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh

# must be sourced last — it wraps zle widgets set up by everything above
[ -f /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ] && \
	source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh
