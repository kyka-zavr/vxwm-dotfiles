#!/bin/sh
# vxwm rice installer — Arch Linux (full) + Fedora (experimental)
# Usage: ./install.sh [all|deps|config|build|help]
#
# Distro: auto-detect from /etc/os-release, or force:
#   RICE_DISTRO=arch|fedora ./install.sh
#
# Shell: interactive prompt, or:
#   RICE_SHELL=bash|zsh|both ./install.sh
#   RICE_CHSH=1   also chsh when RICE_SHELL is bash|zsh
set -e

DOTFILES="$(cd "$(dirname "$0")" && pwd)"
RICE_SHELL_CHOICE=""
DISTRO="" # arch | fedora
AUR_HELPER=""

# ── distro ───────────────────────────────────────────────────────────

detect_distro() {
	if [ -n "$RICE_DISTRO" ]; then
		case "$RICE_DISTRO" in
		arch | fedora) DISTRO="$RICE_DISTRO" ;;
		*)
			echo "RICE_DISTRO must be arch or fedora (got: $RICE_DISTRO)" >&2
			exit 1
			;;
		esac
		echo "==> Distro (RICE_DISTRO): $DISTRO"
		return
	fi

	if [ -r /etc/os-release ]; then
		# shellcheck disable=SC1091
		. /etc/os-release
		# Prefer exact ID first (ID_LIKE=arch on some non-Arch can confuse)
		case "${ID:-}" in
		arch | archarm | manjaro | endeavouros | garuda | artix | cachyos)
			DISTRO="arch"
			;;
		fedora | rhel | centos | nobara | rocky | almalinux)
			DISTRO="fedora"
			;;
		esac
		if [ -z "$DISTRO" ]; then
			case " ${ID_LIKE:-} " in
			*" arch "* | *"archlinux"*) DISTRO="arch" ;;
			*" fedora "* | *" rhel "* | *" centos "*) DISTRO="fedora" ;;
			esac
		fi
	fi

	if [ -z "$DISTRO" ]; then
		if command -v pacman >/dev/null 2>&1; then
			DISTRO="arch"
		elif command -v dnf >/dev/null 2>&1; then
			DISTRO="fedora"
		else
			echo "Unsupported distro. Need Arch (pacman) or Fedora (dnf)." >&2
			echo "Force with: RICE_DISTRO=arch|fedora ./install.sh" >&2
			exit 1
		fi
	fi

	echo "==> Distro: $DISTRO"
	if [ "$DISTRO" = "fedora" ]; then
		echo "    (Fedora path is experimental — package names can differ by release)"
	fi
}

# ── package helpers ──────────────────────────────────────────────────

pkg_install() {
	if [ "$#" -eq 0 ]; then
		return 0
	fi
	case "$DISTRO" in
	arch)
		sudo pacman -S --needed --noconfirm "$@"
		;;
	fedora)
		# Prefer skip-unavailable (dnf4/dnf5); else one-by-one
		if sudo dnf install -y --skip-unavailable "$@" 2>/dev/null; then
			return 0
		fi
		echo "    (retrying packages one-by-one)"
		for p in "$@"; do
			if rpm -q "$p" >/dev/null 2>&1; then
				continue
			fi
			sudo dnf install -y "$p" 2>/dev/null ||
				echo "    WARN: could not install '$p' (optional or renamed on this release)"
		done
		;;
	esac
}

arch_has() {
	# true if package or provide is installed
	pacman -Q "$1" >/dev/null 2>&1 || pacman -Qq --whatprovides "$1" >/dev/null 2>&1
}

# ── Arch AUR ─────────────────────────────────────────────────────────

ask_aur_helper() {
	AUR_HELPER=""
	[ "$DISTRO" = "arch" ] || return 0

	if command -v yay >/dev/null 2>&1; then
		AUR_HELPER="yay"
	elif command -v paru >/dev/null 2>&1; then
		AUR_HELPER="paru"
	else
		echo "No AUR helper found (yay/paru)."
		printf "Install yay? [Y/n]: "
		if [ -t 0 ]; then
			read -r ans || ans=""
		else
			ans=n
			echo "n (non-interactive)"
		fi
		case "$ans" in
		n | N | no) ;;
		*)
			sudo pacman -S --needed --noconfirm base-devel git
			git clone https://aur.archlinux.org/yay.git /tmp/yay
			(cd /tmp/yay && makepkg -si --noconfirm)
			rm -rf /tmp/yay
			AUR_HELPER="yay"
			;;
		esac
	fi
	[ -n "$AUR_HELPER" ] && echo "Using AUR helper: $AUR_HELPER"
}

# ── shell choice ─────────────────────────────────────────────────────

ask_shell() {
	if [ -n "$RICE_SHELL" ]; then
		case "$RICE_SHELL" in
		bash | zsh | both) RICE_SHELL_CHOICE="$RICE_SHELL" ;;
		*)
			echo "RICE_SHELL must be bash, zsh, or both (got: $RICE_SHELL)" >&2
			exit 1
			;;
		esac
		echo "==> Shell (RICE_SHELL): $RICE_SHELL_CHOICE"
		return
	fi

	cur="$(basename "${SHELL:-/bin/bash}")"
	def=1
	case "$cur" in
	zsh) def=2 ;;
	bash) def=1 ;;
	esac

	echo ""
	echo "────────────────────────────────────────"
	echo "  Interactive shell for rice configs"
	echo "  (aliases: vxbuild, vxconfig, vxmod, x)"
	echo "────────────────────────────────────────"
	echo "  1) bash   → ~/.bashrc  (+ optional ble.sh)"
	echo "  2) zsh    → ~/.zshrc   (installs zsh if needed)"
	echo "  3) both   → bash + zsh configs"
	printf "Choice [1/2/3] (default %s from \$SHELL=%s): " "$def" "$cur"
	if [ -t 0 ]; then
		read -r ans || ans=""
	else
		ans=$def
		echo "$def (non-interactive)"
	fi
	case "${ans:-$def}" in
	2 | zsh | Zsh | ZSH) RICE_SHELL_CHOICE="zsh" ;;
	3 | both | Both | BOTH) RICE_SHELL_CHOICE="both" ;;
	1 | bash | Bash | BASH | "") RICE_SHELL_CHOICE="bash" ;;
	*)
		echo "    unknown choice, using bash"
		RICE_SHELL_CHOICE="bash"
		;;
	esac
	echo "==> Shell: $RICE_SHELL_CHOICE"
}

shell_wants_bash() {
	case "$RICE_SHELL_CHOICE" in bash | both) return 0 ;; *) return 1 ;; esac
}

shell_wants_zsh() {
	case "$RICE_SHELL_CHOICE" in zsh | both) return 0 ;; *) return 1 ;; esac
}

# ── pywal ────────────────────────────────────────────────────────────

install_pywal_arch() {
	export PATH="$HOME/.local/bin:$PATH"
	if command -v wal >/dev/null 2>&1; then
		echo "==> wal already on PATH ($(command -v wal))"
		return
	fi

	# Official repo first (no AUR needed)
	if ! arch_has python-pywal; then
		echo "==> Installing python-pywal (official repos)"
		sudo pacman -S --needed --noconfirm python-pywal || true
	fi
	if command -v wal >/dev/null 2>&1; then
		echo "==> wal → $(command -v wal)"
		return
	fi

	# Prefer pywal16 from AUR when available
	if [ -n "$AUR_HELPER" ]; then
		echo "==> Installing AUR python-pywal16"
		$AUR_HELPER -S --needed --noconfirm python-pywal16 || true
	fi
	if command -v wal >/dev/null 2>&1; then
		echo "==> wal → $(command -v wal)"
		return
	fi

	echo "==> WARN: wal not found. Install one of:"
	echo "    sudo pacman -S python-pywal"
	echo "    yay -S python-pywal16"
}

install_pywal_fedora() {
	export PATH="$HOME/.local/bin:$PATH"
	if command -v wal >/dev/null 2>&1; then
		echo "==> wal already on PATH ($(command -v wal))"
		return
	fi

	echo "==> Installing pywal (Fedora: pipx or pip --user)"

	# pipx avoids PEP 668 externally-managed-environment
	if command -v pipx >/dev/null 2>&1 || pkg_install pipx 2>/dev/null; then
		if command -v pipx >/dev/null 2>&1; then
			pipx ensurepath 2>/dev/null || true
			if pipx install pywal16 2>/dev/null || pipx install pywal 2>/dev/null; then
				export PATH="$HOME/.local/bin:$PATH"
			fi
		fi
	fi

	if ! command -v wal >/dev/null 2>&1; then
		# Fallback: pip --user (PEP 668: break-system-packages on F38+)
		python3 -m pip install --user pywal16 2>/dev/null ||
			python3 -m pip install --user --break-system-packages pywal16 2>/dev/null ||
			python3 -m pip install --user pywal 2>/dev/null ||
			python3 -m pip install --user --break-system-packages pywal 2>/dev/null ||
			echo "    WARN: pip install pywal failed"
	fi

	export PATH="$HOME/.local/bin:$PATH"
	if command -v wal >/dev/null 2>&1; then
		echo "    wal → $(command -v wal)"
		return
	fi

	# Symlink from user base bin if needed
	UBIN="$(python3 -c 'import site; print(site.USER_BASE)' 2>/dev/null)/bin"
	if [ -x "$UBIN/wal" ]; then
		mkdir -p "$HOME/.local/bin"
		ln -sfn "$UBIN/wal" "$HOME/.local/bin/wal"
		echo "    linked $UBIN/wal → ~/.local/bin/wal"
		return
	fi

	echo "==> WARN: wal not found. Try:"
	echo "    pipx install pywal16"
	echo "    # or: python3 -m pip install --user --break-system-packages pywal16"
}

# clipmenu is often missing from Fedora — install scripts from upstream
install_clipmenu_fallback() {
	export PATH="$HOME/.local/bin:$PATH"
	if command -v clipmenu >/dev/null 2>&1 && command -v clipmenud >/dev/null 2>&1; then
		echo "==> clipmenu already present"
		return
	fi
	echo "==> Installing clipmenu from upstream (git)"
	command -v git >/dev/null 2>&1 || {
		echo "    git missing — skip clipmenu"
		return
	}
	mkdir -p "$HOME/.local/bin"

	if ! command -v clipnotify >/dev/null 2>&1; then
		case "$DISTRO" in
		fedora) pkg_install libXfixes-devel libX11-devel make gcc 2>/dev/null || true ;;
		arch) sudo pacman -S --needed --noconfirm libxfixes libx11 base-devel 2>/dev/null || true ;;
		esac
		rm -rf /tmp/clipnotify
		if git clone --depth 1 https://github.com/cdown/clipnotify.git /tmp/clipnotify 2>/dev/null; then
			(cd /tmp/clipnotify && make && cp -f clipnotify "$HOME/.local/bin/" && chmod +x "$HOME/.local/bin/clipnotify") ||
				echo "    WARN: clipnotify build failed"
			rm -rf /tmp/clipnotify
		fi
	fi

	rm -rf /tmp/clipmenu
	if git clone --depth 1 https://github.com/cdown/clipmenu.git /tmp/clipmenu 2>/dev/null; then
		for s in clipmenu clipmenud clipdel clipctl; do
			[ -f "/tmp/clipmenu/$s" ] && cp -f "/tmp/clipmenu/$s" "$HOME/.local/bin/" && chmod +x "$HOME/.local/bin/$s"
		done
		rm -rf /tmp/clipmenu
		echo "    clipmenu → ~/.local/bin"
	else
		echo "    WARN: could not clone clipmenu"
	fi
}

# ── packages ─────────────────────────────────────────────────────────

install_packages_arch() {
	# shellcheck disable=SC2086
	SYSTEM_PKGS="
		base-devel git pkgconf python-pip rsync
		xorg xorg-xinit xorg-xrdb xorg-xsetroot xorg-xset xorg-xprop
		xorg-setxkbmap xorg-xrandr xdotool
		libx11 libxinerama libxft libxcomposite libxdamage libxfixes libxrandr libdrm
		kitty feh dmenu clipmenu stalonetray polybar rofi cava picom dunst libnotify
		thunar gvfs exo tumbler sxiv imagemagick xclip xsel swappy micro jq code
		brightnessctl
		networkmanager network-manager-applet
		bluez bluez-utils blueman
		libpulse pipewire pipewire-pulse pipewire-alsa pipewire-jack wireplumber
		pavucontrol alsa-utils
		python-gobject gtk3
		udisks2 ntfs-3g xdg-user-dirs xdg-utils
		ttf-dejavu ttf-liberation noto-fonts
		otf-atkinsonhyperlegiblemono-nerd
		ttf-jetbrains-mono-nerd ttf-firacode-nerd ttf-hack-nerd
	"

	if shell_wants_zsh; then
		SYSTEM_PKGS="$SYSTEM_PKGS zsh"
	fi

	MISSING=""
	for pkg in $SYSTEM_PKGS; do
		arch_has "$pkg" || MISSING="$MISSING $pkg"
	done

	if [ -n "$MISSING" ]; then
		echo "==> Installing system packages (Arch):"
		for pkg in $MISSING; do echo "  - $pkg"; done
		# shellcheck disable=SC2086
		sudo pacman -S --needed --noconfirm $MISSING
	else
		echo "==> All system packages already installed"
	fi

	install_pywal_arch
}

install_packages_fedora() {
	echo "==> Installing packages (Fedora / dnf)…"

	# Build tools + X11 headers for vxwm (needs fontconfig + freetype for -lXft)
	BUILD_PKGS="
		gcc make pkgconf git rsync
		python3-pip python3-devel
		libX11-devel libXft-devel libXinerama-devel
		libXcomposite-devel libXdamage-devel libXfixes-devel
		libXrandr-devel libdrm-devel libXext-devel
		fontconfig-devel freetype-devel
		xorg-x11-server-Xorg xorg-x11-xinit xorg-x11-server-utils
		xorg-x11-xkb-utils xorg-x11-apps xdotool
	"

	# Runtime desktop stack
	# pulseaudio-utils → pactl (vol)
	# gtk3 + python3-gobject → calendar-popup / powermenu (GTK popups)
	DESKTOP_PKGS="
		kitty feh dmenu polybar rofi cava picom dunst libnotify
		thunar gvfs tumbler ImageMagick xclip xsel micro jq code
		brightnessctl stalonetray swappy
		NetworkManager network-manager-applet
		bluez blueman
		pipewire pipewire-pulseaudio wireplumber pulseaudio-utils
		pavucontrol alsa-utils
		python3-gobject gtk3
		udisks2 ntfs-3g xdg-user-dirs xdg-utils
		dejavu-sans-mono-fonts liberation-fonts google-noto-sans-fonts
		jetbrains-mono-fonts fira-code-fonts
		pipx
	"

	# May be missing on some releases (skip-unavailable / one-by-one)
	OPTIONAL_PKGS="sxiv nsxiv clipmenu clipnotify hack-fonts"

	if shell_wants_zsh; then
		DESKTOP_PKGS="$DESKTOP_PKGS zsh"
	fi

	# shellcheck disable=SC2086
	pkg_install $BUILD_PKGS
	# shellcheck disable=SC2086
	pkg_install $DESKTOP_PKGS
	# shellcheck disable=SC2086
	pkg_install $OPTIONAL_PKGS || true

	install_pywal_fedora
	install_clipmenu_fallback
}

install_packages() {
	case "$DISTRO" in
	arch) install_packages_arch ;;
	fedora) install_packages_fedora ;;
	*)
		echo "Unknown DISTRO=$DISTRO" >&2
		exit 1
		;;
	esac
}

# ── services ─────────────────────────────────────────────────────────

enable_services() {
	if command -v systemctl >/dev/null 2>&1; then
		echo "==> Enabling NetworkManager"
		sudo systemctl enable --now NetworkManager.service 2>/dev/null || true
		echo "==> Enabling Bluetooth"
		sudo systemctl enable --now bluetooth.service 2>/dev/null || true
	fi
	echo "==> Enabling PipeWire user services"
	# Arch unit: pipewire-pulse · Fedora unit often: pipewire-pulseaudio
	systemctl --user enable --now pipewire wireplumber 2>/dev/null || true
	systemctl --user enable --now pipewire-pulse 2>/dev/null ||
		systemctl --user enable --now pipewire-pulseaudio 2>/dev/null || true
}

# ── shell configs ────────────────────────────────────────────────────

install_blesh() {
	if [ -f "$HOME/.local/share/blesh/ble.sh" ]; then
		echo "==> ble.sh already installed"
		return
	fi
	echo "==> Installing ble.sh (bash line editor)"
	command -v git >/dev/null 2>&1 || {
		echo "    git missing — skip ble.sh"
		return
	}
	git clone --recursive https://github.com/akinomyoga/ble.sh.git /tmp/ble.sh
	make -C /tmp/ble.sh install PREFIX="$HOME/.local"
	rm -rf /tmp/ble.sh
}

ensure_path_line() {
	# $1 = file
	f=$1
	[ -n "$f" ] || return 0
	touch "$f"
	if ! grep -q 'local/bin' "$f" 2>/dev/null; then
		echo 'export PATH="$HOME/.local/bin:$PATH"' >>"$f"
	fi
}

install_shell_configs() {
	if [ -z "$RICE_SHELL_CHOICE" ]; then
		ask_shell
	fi

	mkdir -p "$HOME/.config/rice"
	if [ -f "$DOTFILES/.config/rice/shell-common" ]; then
		echo "==> Shared shell aliases → ~/.config/rice/shell-common"
		cp "$DOTFILES/.config/rice/shell-common" "$HOME/.config/rice/shell-common"
	fi

	if shell_wants_bash; then
		echo "==> bash → ~/.bashrc"
		cp "$DOTFILES/.bashrc" "$HOME/.bashrc"
		install_blesh
		# Fedora login bash often only reads .bash_profile
		ensure_path_line "$HOME/.bash_profile"
	fi

	if shell_wants_zsh; then
		if ! command -v zsh >/dev/null 2>&1; then
			echo "==> Installing zsh"
			case "$DISTRO" in
			arch) sudo pacman -S --needed --noconfirm zsh ;;
			fedora) sudo dnf install -y zsh ;;
			esac
		fi
		echo "==> zsh → ~/.zshrc"
		cp "$DOTFILES/.zshrc" "$HOME/.zshrc"
		ensure_path_line "$HOME/.zprofile"
	fi

	case "$RICE_SHELL_CHOICE" in
	bash | zsh)
		target_path="$(command -v "$RICE_SHELL_CHOICE" 2>/dev/null || true)"
		if [ -n "$target_path" ] && [ -x "$target_path" ]; then
			cur_login="$(getent passwd "${USER:-$(id -un)}" 2>/dev/null | cut -d: -f7)"
			if [ "$cur_login" != "$target_path" ]; then
				if [ -n "$RICE_SHELL" ]; then
					if [ "${RICE_CHSH:-0}" = "1" ]; then
						echo "==> chsh → $target_path (RICE_CHSH=1)"
						chsh -s "$target_path" || echo "    chsh failed — run: chsh -s $target_path"
					else
						echo "==> Login shell is $cur_login (not changing; set RICE_CHSH=1 or run chsh -s $target_path)"
					fi
				elif [ -t 0 ]; then
					printf "Set login shell to %s? [y/N]: " "$target_path"
					read -r ans || ans=n
					case "$ans" in
					y | Y | yes | Yes)
						chsh -s "$target_path" || echo "    chsh failed — run: chsh -s $target_path"
						echo "    (log out/in for login shell change)"
						;;
					*)
						echo "    keeping login shell: $cur_login"
						;;
					esac
				else
					echo "==> keeping login shell: $cur_login (non-interactive)"
				fi
			else
				echo "==> Login shell already $target_path"
			fi
		fi
		;;
	both)
		echo "==> both shells configured — login shell unchanged (use chsh if needed)"
		echo "    chsh -s $(command -v bash)   or   chsh -s $(command -v zsh)"
		;;
	esac
}

# ── configs / build ──────────────────────────────────────────────────

install_configs() {
	echo "==> Scripts → ~/.local/bin/"
	mkdir -p "$HOME/.local/bin" "$HOME/.cache/clipmenu"
	for f in "$DOTFILES"/.local/bin/*; do
		[ -f "$f" ] || continue
		case "$(basename "$f")" in
		*.md | *.txt) continue ;;
		esac
		cp "$f" "$HOME/.local/bin/"
		chmod +x "$HOME/.local/bin/$(basename "$f")"
	done

	echo "==> App configs"
	mkdir -p \
		"$HOME/.config/kitty" \
		"$HOME/.config/picom" \
		"$HOME/.config/dunst" \
		"$HOME/.config/gtk-3.0" \
		"$HOME/.config/polybar/scripts" \
		"$HOME/.config/rofi" \
		"$HOME/.config/cava" \
		"$HOME/.config/wal/templates" \
		"$HOME/.config/rice" \
		"$HOME/Pictures/Wallpapers" \
		"$HOME/Pictures/Screenshots"

	cp "$DOTFILES/.config/kitty/kitty.conf" "$HOME/.config/kitty/"
	cp "$DOTFILES/.config/picom/picom.conf" "$HOME/.config/picom/"
	cp "$DOTFILES/.config/polybar/config.ini" "$HOME/.config/polybar/"
	cp "$DOTFILES/.config/polybar/scripts/"*.sh "$HOME/.config/polybar/scripts/"
	chmod +x "$HOME/.config/polybar/scripts/"*.sh
	cp "$DOTFILES/.config/rofi/config.rasi" "$HOME/.config/rofi/"
	cp "$DOTFILES/.config/cava/config" "$HOME/.config/cava/"
	cp "$DOTFILES/.config/dunst/dunstrc" "$HOME/.config/dunst/dunstrc.base"
	cp "$DOTFILES/.config/dunst/dunstrc" "$HOME/.config/dunst/dunstrc"
	cp "$DOTFILES/.config/wal/templates/dwm.Xresources" "$HOME/.config/wal/templates/"
	cp "$DOTFILES/.config/wal/templates/dunstrc" "$HOME/.config/wal/templates/"
	cp "$DOTFILES/.config/wal/templates/gtk.css" "$HOME/.config/wal/templates/"
	cp "$DOTFILES/.config/wal/templates/colors-rofi.rasi" "$HOME/.config/wal/templates/"
	cp "$DOTFILES/.config/wal/templates/vscode-colors.json" "$HOME/.config/wal/templates/"
	# GTK3 / Thunar (colors filled on first setwal)
	[ -f "$DOTFILES/.config/gtk-3.0/settings.ini" ] &&
		cp "$DOTFILES/.config/gtk-3.0/settings.ini" "$HOME/.config/gtk-3.0/"
	[ -f "$DOTFILES/.config/gtk-3.0/gtk.css" ] &&
		cp "$DOTFILES/.config/gtk-3.0/gtk.css" "$HOME/.config/gtk-3.0/"
	# default file manager
	if command -v thunar >/dev/null 2>&1 && command -v xdg-mime >/dev/null 2>&1; then
		xdg-mime default thunar.desktop inode/directory 2>/dev/null || true
	fi

	[ -f "$DOTFILES/.config/rice/font" ] &&
		cp "$DOTFILES/.config/rice/font" "$HOME/.config/rice/font"
	for ic in volume-white.png volume-muted-white.png; do
		[ -f "$DOTFILES/.config/rice/$ic" ] &&
			cp "$DOTFILES/.config/rice/$ic" "$HOME/.config/rice/$ic"
	done
	[ -f "$DOTFILES/.config/rice/shell-common" ] &&
		cp "$DOTFILES/.config/rice/shell-common" "$HOME/.config/rice/shell-common"

	echo "==> Home configs"
	cp "$DOTFILES/.xinitrc" "$HOME/.xinitrc"
	chmod +x "$HOME/.xinitrc"
	cp "$DOTFILES/.stalonetrayrc" "$HOME/.stalonetrayrc"

	install_shell_configs

	echo "==> Wallpapers"
	if [ -d "$DOTFILES/Pictures/Wallpapers" ]; then
		cp -a "$DOTFILES/Pictures/Wallpapers/." "$HOME/Pictures/Wallpapers/" 2>/dev/null || true
	fi

	echo "==> vxwm sources → ~/vxwm"
	mkdir -p "$HOME/vxwm"
	if command -v rsync >/dev/null 2>&1; then
		rsync -a --delete \
			--exclude='*.o' --exclude='vxwm' --exclude='.git' \
			"$DOTFILES/vxwm/" "$HOME/vxwm/"
	else
		cp -a "$DOTFILES/vxwm/." "$HOME/vxwm/"
	fi

	ensure_path_line "$HOME/.profile"
	ensure_path_line "$HOME/.bash_profile"

	if [ ! -e "$HOME/dotfiles" ] && [ "$DOTFILES" != "$HOME/dotfiles" ]; then
		ln -sfn "$DOTFILES" "$HOME/dotfiles" 2>/dev/null || true
	fi

	command -v xdg-user-dirs-update >/dev/null 2>&1 && xdg-user-dirs-update 2>/dev/null || true
}

build_vxwm() {
	echo "==> Building vxwm (picom only, ZOOM off)"
	cd "$DOTFILES/vxwm"
	make clean 2>/dev/null || true
	if ! make; then
		echo "==> ERROR: vxwm build failed."
		echo "    Arch:   sudo pacman -S --needed base-devel libx11 libxft libxinerama"
		echo "    Fedora: sudo dnf install gcc make libX11-devel libXft-devel libXinerama-devel fontconfig-devel freetype-devel"
		exit 1
	fi
	sudo make install
	mkdir -p "$HOME/.local/bin"
	cp -f "$DOTFILES/vxwm/vxwm" "$HOME/.local/bin/vxwm"
	chmod +x "$HOME/.local/bin/vxwm"
	[ -d "$HOME/vxwm" ] && cp -f "$DOTFILES/vxwm/vxwm" "$HOME/vxwm/vxwm" 2>/dev/null || true
	echo "    vxwm → $HOME/.local/bin/vxwm  (+ sudo make install → /usr/local/bin)"
}

apply_default_wallpaper() {
	first=""
	for f in "$HOME/Pictures/Wallpapers/"*; do
		[ -f "$f" ] || continue
		first=$f
		break
	done
	export PATH="$HOME/.local/bin:$PATH"
	if [ -n "$first" ] && command -v wal >/dev/null 2>&1; then
		echo "==> Default wallpaper: $first"
		if [ -x "$HOME/.local/bin/setwal" ]; then
			"$HOME/.local/bin/setwal" "$first" 2>/dev/null || true
		else
			wal -i "$first" -n -q 2>/dev/null || true
		fi
	else
		echo "==> Skip default wallpaper (no image or no wal yet)"
	fi
}

# Soft check — does not fail install, only reports
verify_install() {
	export PATH="$HOME/.local/bin:$PATH"
	echo ""
	echo "==> Verify (commands the rice needs)"
	ok=0
	bad=0
	# critical for session
	for c in vxwm wal feh dmenu dunst picom polybar xdotool xrdb xsetroot xrandr; do
		if command -v "$c" >/dev/null 2>&1; then
			echo "  OK  $c"
			ok=$((ok + 1))
		else
			echo "  MISSING  $c"
			bad=$((bad + 1))
		fi
	done
	# tray / audio / extras
	for c in stalonetray clipmenud clipmenu nm-applet blueman-applet pactl brightnessctl kitty; do
		if command -v "$c" >/dev/null 2>&1; then
			echo "  OK  $c"
			ok=$((ok + 1))
		else
			echo "  WARN  $c (feature may be incomplete)"
			bad=$((bad + 1))
		fi
	done
	# AUR-only, not installed by this script — check separately, don't count as missing
	if command -v betterlockscreen >/dev/null 2>&1; then
		echo "  OK  betterlockscreen"
		ok=$((ok + 1))
	else
		echo "  INFO  betterlockscreen not installed (AUR — paru -S --needed betterlockscreen); Super+Escape → Lock needs it"
	fi
	# scripts
	for c in setwal dmenu-above powermenu calendar-popup; do
		if [ -x "$HOME/.local/bin/$c" ]; then
			echo "  OK  ~/.local/bin/$c"
			ok=$((ok + 1))
		else
			echo "  MISSING  ~/.local/bin/$c"
			bad=$((bad + 1))
		fi
	done
	if [ -f "$HOME/.config/rice/shell-common" ]; then
		echo "  OK  shell-common"
	else
		echo "  WARN  shell-common"
	fi
	echo "  summary: $ok ok, $bad missing/warn"
	echo ""
}

print_help() {
	cat <<'EOF'

==============================================
  Install finished — vxwm rice
==============================================

  START
    startx          (alias: x)
    Fedora: use an Xorg session or startx (not pure Wayland)

  SHELL
    Aliases live in  ~/.config/rice/shell-common
    bash → ~/.bashrc    zsh → ~/.zshrc
    Re-run:  RICE_SHELL=zsh ./install.sh config
    Login shell:  chsh -s $(command -v zsh)   # or bash

  ESSENTIAL KEYS
    Super+Return         terminal
    Alt+Space            launcher (rofi)
    Super+`              keybind cheatsheet
    Super+v              clipboard
    Super+Shift+w        wallpaper picker (wpick wheel)
    Super+Shift+t        fonts
    Super+Tab / j / k    cycle focus (recenters canvas on it)
    Super+l              lock screen
    Super+Escape         power menu
                         → Lock | Exit | Suspend | Reboot | Poweroff
    Super+F5             reload pywal colors everywhere

  POWER MENU
    Lock      betterlockscreen (AUR — paru -S --needed betterlockscreen)
    Exit      leave X / vxwm
    Suspend   sleep
    Reboot / Poweroff

  WALLPAPERS & THEME
    ~/Pictures/Wallpapers
    wpick   |   setwal /path/to/img
    (recolors dunst, picom shadows, bar via xrdb)

  FONTS
    Super+Shift+t   or   setfont pick
    setfont list | setfont "Name" 10 12
    applies live (bar/menus/kitty/dunst) — no rebuild or restart needed

  EDIT / REBUILD
    vxconfig   →  ~/dotfiles/vxwm/config.h
    vxmod      →  ~/dotfiles/vxwm/modules.h
    vxbuild    →  make + install vxwm

  NVIDIA
    Arch:    install-nvidia   (linux-zen + nvidia-open-dkms)
    Fedora:  RPM Fusion akmod-nvidia — see README § NVIDIA

  NOTES
    • Launcher: rofi (Alt+Space); menus otherwise dmenu above/on the bar
    • Compositor: picom (no vcompmgr)
    • Audio: PipeWire + pactl (pavucontrol)
    • Bar: polybar — wifi/bluetooth/volume/brightness live in it directly,
      click wifi/bluetooth to open nm-connection-editor/blueman-manager,
      click the date for a calendar popup. Tray is for apps that minimize
      to it (Telegram/Discord/etc), not for network/bluetooth applets.
    • RU/EN layout: Super+Space (grp:win_space_toggle, set in .xinitrc)

EOF
	echo "  Repo:   $DOTFILES"
	echo "  Distro: ${DISTRO:-unknown}"
	echo "  Shell:  ${RICE_SHELL_CHOICE:-not set this run}"
	echo ""
}

# ── main ─────────────────────────────────────────────────────────────

detect_distro

case "${1:-all}" in
deps)
	[ "$DISTRO" = "arch" ] && ask_aur_helper
	ask_shell
	install_packages
	enable_services
	verify_install
	;;
config)
	ask_shell
	install_configs
	;;
build)
	build_vxwm
	;;
all)
	[ "$DISTRO" = "arch" ] && ask_aur_helper
	ask_shell
	install_packages
	enable_services
	install_configs
	build_vxwm
	apply_default_wallpaper
	verify_install
	print_help
	;;
help | --help | -h)
	print_help
	echo "Usage: $0 [all|deps|config|build|help]"
	echo "  RICE_DISTRO=arch|fedora     force distro"
	echo "  RICE_SHELL=bash|zsh|both    non-interactive shell choice"
	echo "  RICE_CHSH=1                 also chsh when RICE_SHELL is bash|zsh"
	echo ""
	;;
*)
	echo "Usage: $0 [all|deps|config|build|help]" >&2
	echo "  RICE_DISTRO=arch|fedora RICE_SHELL=bash|zsh|both ./install.sh" >&2
	exit 1
	;;
esac
