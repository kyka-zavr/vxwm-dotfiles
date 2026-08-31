#
# ~/.zshrc — vxwm rice
#

# Shared aliases + PATH (bash & zsh)
[ -f "$HOME/.config/rice/shell-common" ] && . "$HOME/.config/rice/shell-common"

# History
HISTFILE="$HOME/.zsh_history"
HISTSIZE=10000
SAVEHIST=10000
setopt HIST_IGNORE_DUPS SHARE_HISTORY

# Completion
autoload -Uz compinit
compinit -u 2>/dev/null || true

# Prompt (simple, matches bash spirit)
PROMPT='[%n@%m %1~]%# '
