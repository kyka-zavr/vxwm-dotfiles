#pragma once

/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 0;        /* border pixel of windows */
static const unsigned int snap      = 0;       /* snap pixel */
static const int showbar            = 0;        /* 0 means no bar */
static const int topbar             = 0;        /* 0 means bottom bar */
/*
 * dim13 Sun Gallant (https://github.com/dim13/gallant) — scalable TTF.
 * Works bar + kitty + dmenu. No Cyrillic → DejaVu fallback for missing glyphs.
 */
static const char *fonts[]          = { "Adwaita Sans:size=12", "DejaVu Sans Mono:size=12" };
static const char dmenufont[]       = "Adwaita Sans:size=12";
#define COORDINATES_STYLE "[x%d y%d]" /* The style of coordinates displayed in bar, do not remove %d. */

static MAYBE_CONST char normbgcolor[]           = "#222222";
static MAYBE_CONST char normbordercolor[]       = "#444444";
static MAYBE_CONST char normfgcolor[]           = "#bbbbbb";
static MAYBE_CONST char selfgcolor[]            = "#eeeeee";
static MAYBE_CONST char selbordercolor[]        = "#005577";
static MAYBE_CONST char selbgcolor[]            = "#005577";
static MAYBE_CONST char *colors[][3] = {
       /*               fg           bg           border   */
       [SchemeNorm] = { normfgcolor, normbgcolor, normbordercolor },
       [SchemeSel]  = { selfgcolor,  selbgcolor,  selbordercolor  },
};

#define CENTER_NEW_FLOATING_WINDOWS 1 // so, basically, it does what it says. (make 0 to turn off)
#define NEW_FLOATING_WINDOWS_APPEAR_UNDER_CURSOR 0 // so, basically, it does what it says. (make 0 to turn off) 

#if GAPS
static const unsigned int gappx = 5;
#endif

#if BAR_HEIGHT
static const int user_bh = 30; /* fits gallant12x22 at size 12 */
#endif

#if BAR_PADDING
static const int top_vertpad = 0;          /* top vertical padding of bar */ 
static const int bottom_vertpad = 8;       /* bottom vertical padding of bar */
static const int left_sidepad = 550;       /* left horizontal padding of bar */
static const int right_sidepad = 550;      /* right horizontal padding of bar */
#endif

#define BAR_ALWAYS_ON_TOP 1 /* Makes internal bar on top of other windows. */

#if EXTERNAL_BARS
#define EXTERNAL_BARS_ALWAYS_ON_TOP 1 /* Makes external bars on top of other windows. */
#endif

#if INFINITE_TAGS
#define PINNED_WINDOWS_ALWAYS_ON_TOP 1 /* Makes pinned windows on top of other windows */
#endif

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

#if OCCUPIED_TAGS_DECORATION
static const char *occupiedtags[] = { "1+", "2+", "3+", "4+", "5+", "6+", "7+", "8+", "9+" };
#endif

#if INFINITE_TAGS
#define MOVE_CANVAS_STEP 120 /* Defines how many pixel will be jumped when using movecanvas function */

/* touchpad two-finger scroll on the bare desktop: normally pans the canvas
   (one MOVE_CANVAS_STEP per scroll notch, see buttons[] below). A fast burst
   of scroll-up notches is treated as a "swipe up" and opens the launcher
   instead — see the SWIPE_BURST_COUNT check in buttonpress() (vxwm.c). This
   is a heuristic: this touchpad (ALPS PS/2) has no real multi-finger gesture
   support, only 2-finger scroll, so there's no true swipe event to bind. */
#define SWIPE_BURST_MS 140    /* max ms between notches to still count as the same burst */
#define SWIPE_BURST_COUNT 4   /* consecutive fast notches that count as a swipe-up */
#endif

#if INFINITE_TAGS && IT_SHOW_COORDINATES_IN_BAR
#define COORDINATES_DIVISOR 10 /* Defines by what number coordinates on the bar will be divided, can be used for making numbers smaller which makes navigation easier */
#endif

#if MOVE_RESIZE_WITH_KEYBOARD
#define MOVE_WITH_KEYBOARD_STEP 50 /* Defines by how many pixels windows will be resized with keyboard */
#define RESIZE_WITH_KEYBOARD_STEP 50 /* Defines by how many pixels windows will be resized with keyboard */
#endif

#if AUTOSTART
/* vxwm will execute this on startup (can be skipped with -ignoreautostart vxwm flag). */

static const char *const autostart[] = {
	"st",
	NULL /* must end with NULL */
};
#endif

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1 },
	/* tray: ~0 = all tags; also auto-pinned in applyrules (infinite canvas) */
	{ "stalonetray", NULL,    NULL,       ~0,           1,           -1 },
	{ "Stalonetray", NULL,    NULL,       ~0,           1,           -1 },
	{ NULL,       NULL,       "Calendar", 0,            1,           -1 },
	{ NULL,       NULL,       "Powermenu", 0,           1,           -1 },
	{ NULL,       NULL,       "wpick",    0,            1,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
#if LOCK_MOVE_RESIZE_REFRESH_RATE
/* Cap move/resize event handling. Panel is 165Hz → ~2× = 330 keeps motion smooth without flooding X. */
static const int refreshrate = 330;
#endif //LOCK_MOVE_RESIZE_REFRESH_RATE
static const Layout layouts[] = {
	/* symbol     arrange function */
  { "><>",      NULL },    /* no layout function means floating behavior */
	{ "[]=",      tile },    /* first entry is default */
	{ "[M]",      monocle },
};

/* key definitions */
#define MODKEY Mod4Mask
#define ALTERNATE_MODKEY Mod1Mask

#define SCROLL_UP Button4
#define SCROLL_DOWN Button5

/* Super+N       → show tag N
 * Super+Ctrl+N  → toggle tag N on the view (multi-tag view)
 * Super+Shift+N → move focused window to tag N
 */
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmlaunch", NULL }; /* kept for spawn()'s dmenumon plumbing; not bound to a key */
static const char *clipcmd[] = { "cmlaunch", NULL };
/* rofi theme path resolved via $HOME at runtime (spawn() uses execvp, no
   shell, so this must go through /bin/sh -c to expand — see rofcmd below) */

static const char *termcmd[]  = { "kitty", NULL };
static const char *screenshotcmd[] = { "screenshot", NULL };
static const char *screenshotdelaycmd[] = { "screenshot", "delay", "3", NULL };
static const char *wpickcmd[] = { "wpick", NULL };
static const char *powermenucmd[] = { "powermenu", NULL };
static const char *lockcmd[] = { "betterlockscreen", "-l", "blur", NULL };
static const char *filemanagercmd[] = { "thunar", NULL };
static const char *chromecmd[] = { "google-chrome-stable", NULL };
static const char *codiumcmd[] = { "codium", NULL };
static const char *spotifycmd[] = { "spotify", NULL };

#if ZOOM
static const char *zoomin[] = { "vcompmgr", "-Z", "+0.15", NULL }; // zoom in
static const char *zoomout[] = { "vcompmgr", "-Z", "-0.15", NULL }; // zoom out
static const char *zoomreset[] = { "vcompmgr", "-Z", "1", NULL }; // set zoom to 1
#endif

static const char *volup[] = { "vol", "up", NULL };
static const char *voldown[] = { "vol", "down", NULL };
static const char *volmute[] = { "vol", "mute", NULL };
static const char *brightup[] = { "bright", "up", NULL };
static const char *brightdown[] = { "bright", "down", NULL };
static const char *setfontcmd[] = { "setfont", "pick", NULL };
static const char *cheatsheetcmd[] = { "cheatsheet", NULL };

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ ALTERNATE_MODKEY,              XK_space,  spawn,          SHCMD("rofi -show drun -theme \"$HOME/.config/rofi/config.rasi\"") }, /* rofi launcher */
	{ MODKEY,                       XK_v,      spawn,          {.v = clipcmd } },
  { MODKEY,                       XK_Return, spawn,          {.v = termcmd } },
  { MODKEY|ShiftMask,             XK_s,      spawn,          {.v = screenshotcmd } },
  /* delay: press first, then open menus — dmenu grabs keys so Print alone can't fire while open */
  { MODKEY|ShiftMask,             XK_Print,  spawn,          {.v = screenshotdelaycmd } },
  { MODKEY|ShiftMask,             XK_w,      spawn,          {.v = wpickcmd } }, /* wallpaper picker — GTK wheel carousel, cached thumbnails */
  { MODKEY|ShiftMask,             XK_t,      spawn,          {.v = setfontcmd } }, /* setfont pick */
  { MODKEY,                       XK_grave,  spawn,          {.v = cheatsheetcmd } }, /* keybind cheatsheet */
  { MODKEY,                       XK_Escape, spawn,          {.v = powermenucmd } },
  { MODKEY,                       XK_e,      spawn,          {.v = filemanagercmd } },
  { MODKEY,                       XK_w,      spawn,          {.v = chromecmd } },
  { MODKEY,                       XK_c,      spawn,          {.v = codiumcmd } },
  { MODKEY,                       XK_s,      spawn,          {.v = spotifycmd } },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_Tab,    focusstack,     {.i = +1 } }, /* same as j — focusstack already re-centers the canvas on the newly focused client (see INFINITE_TAGS block) */
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_minus,  setmfact,       {.f = -0.05} }, /* moved off h/l — freed minus/equal when gaps binds were dropped */
	{ MODKEY,                       XK_equal,  setmfact,       {.f = +0.05} },
	{ MODKEY,                       XK_l,      spawn,          {.v = lockcmd } }, /* Super+L lock, matches the usual desktop convention */
	{ MODKEY,                       XK_q,      killclient,     {0} },
	{ MODKEY|ShiftMask|ALTERNATE_MODKEY, XK_t, setlayout,      {.v = &layouts[0]} }, /* was MOD+t; MOD+Shift+t stays setfont */
	{ MODKEY|ShiftMask|ALTERNATE_MODKEY, XK_f, setlayout,      {.v = &layouts[1]} }, /* was MOD+f; MOD+Shift+f stays fullscreen */
	{ MODKEY|ShiftMask,             XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,             XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask|ALTERNATE_MODKEY, XK_space, togglefloating, {0} }, //default toggle floating bind.
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ControlMask|ShiftMask, XK_q,      quit,           {0} },
#if XRDB
  { MODKEY,                       XK_F5,     xrdb,           {.v = NULL } },
#endif
#if FULLSCREEN
  { MODKEY|ShiftMask,             XK_f,      togglefullscr,  {0} },
#endif
#if ENHANCED_TOGGLE_FLOATING
  { MODKEY|ShiftMask,              XK_q,      enhancedtogglefloating, {0} }, //enhanced toggle floating bind.
#endif
#if MOVE_RESIZE_WITH_KEYBOARD
  { MODKEY,					              XK_Down,	moveresize,		{.v = (int []){ 0, MOVE_WITH_KEYBOARD_STEP, 0, 0 }}}, // Move window to down
  { MODKEY,					              XK_Up,		moveresize,		{.v = (int []){ 0, -MOVE_WITH_KEYBOARD_STEP, 0, 0 }}}, // Move window to up
  { MODKEY,					              XK_Right,	moveresize,		{.v = (int []){ MOVE_WITH_KEYBOARD_STEP, 0, 0, 0 }}}, // Move window to right
  { MODKEY,					              XK_Left,	moveresize,		{.v = (int []){ -MOVE_WITH_KEYBOARD_STEP, 0, 0, 0 }}}, // Move window to left
  { MODKEY|ControlMask,			      XK_Down,	moveresize,		{.v = (int []){ 0, 0, 0, RESIZE_WITH_KEYBOARD_STEP }}}, // Resize window
  { MODKEY|ControlMask,			      XK_Up,		moveresize,		{.v = (int []){ 0, 0, 0, -RESIZE_WITH_KEYBOARD_STEP }}}, // Resize window
  { MODKEY|ControlMask,			      XK_Right,	moveresize,		{.v = (int []){ 0, 0, RESIZE_WITH_KEYBOARD_STEP, 0 }}}, // Resize window
  { MODKEY|ControlMask,			      XK_Left,	moveresize,		{.v = (int []){ 0, 0, -RESIZE_WITH_KEYBOARD_STEP, 0 }}}, // Resize window
#endif
#if INFINITE_TAGS
  { MODKEY,                       XK_r,      homecanvas,       {0} }, // Return to x:0, y:0 position
  { MODKEY|ShiftMask,             XK_Left,   movecanvas,       {.i = 0} }, // Move your position to left
  { MODKEY|ShiftMask,             XK_Right,  movecanvas,       {.i = 1} }, // Move your position to right
  { MODKEY|ShiftMask,             XK_Up,     movecanvas,       {.i = 2} }, // Move your position up
  { MODKEY|ShiftMask,             XK_Down,   movecanvas,       {.i = 3} }, // Move your position down
  { MODKEY|ShiftMask,             XK_d,      centerwindow,     {0} },
  { MODKEY|ControlMask,           XK_z,      pinwindow,        {0} },
#endif
#if ZOOM
 { ALTERNATE_MODKEY,              XK_r,      spawn,          {.v = zoomreset } },
 { MODKEY,                        XK_equal,  spawn,          {.v = zoomin } },
 { MODKEY,                        XK_minus,  spawn,          {.v = zoomout } },
#endif
 { 0,                XF86XK_AudioLowerVolume,  spawn,          {.v = voldown } },
 { 0,                XF86XK_AudioRaiseVolume,  spawn,          {.v = volup } },
 { 0,                XF86XK_AudioMute,         spawn,          {.v = volmute } },
 { 0,              XF86XK_MonBrightnessDown,  spawn,          {.v = brightdown } },
 { 0,                XF86XK_MonBrightnessUp,  spawn,          {.v = brightup } },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
#if INFINITE_TAGS
  { ClkRootWin,           0,                        Button1,        movecanvasmouse,     {.f = 1.5 } },
  { ClkRootWin,           MODKEY|ShiftMask,         Button1,        movecanvasmouse,     {.f = 1.5 } },
  { ClkClientWin,         MODKEY|ShiftMask,         Button1,        movecanvasmouse,     {.f = 1.5 } },
  /* plain Button1 on the real desktop drags the canvas. This used to swallow clicks
     meant for polybar because buttonpress() defaulted EVERY unmanaged window (bar
     included) to ClkRootWin — fixed at the source in buttonpress() (vxwm.c) so only
     the actual root window classifies as ClkRootWin now; external windows like
     polybar/rofi/calendar-popup no longer match any root bind. */
  /* .f = 1 is moving multiplier, for example if set to 0.5, canvas will move 2 times slower, if set to 2, canvas will move 2 times faster.
     If you want inverted canvas move then set the value to a negative value. */

  /* touchpad two-finger scroll on the bare desktop pans the canvas (same
     step as Super+Shift+arrows). A fast scroll-up burst opens the launcher
     instead — see buttonpress() in vxwm.c and SWIPE_BURST_* above. */
  { ClkRootWin,           0,              SCROLL_UP,      movecanvas,     {.i = 2} },
  { ClkRootWin,           0,              SCROLL_DOWN,    movecanvas,     {.i = 3} },
  { ClkRootWin,           0,              6,              movecanvas,     {.i = 0} }, /* Button6 (horiz scroll left) — not a named X11 macro */
  { ClkRootWin,           0,              7,              movecanvas,     {.i = 1} }, /* Button7 (horiz scroll right) */
#endif
#if ZOOM
  { ClkRootWin,           MODKEY,         SCROLL_UP,      spawn,          {.v = zoomin } },
  { ClkRootWin,           MODKEY,         SCROLL_DOWN,    spawn,          {.v = zoomout } },

  { ClkClientWin,         MODKEY,         SCROLL_UP,      spawn,          {.v = zoomin } },
  { ClkClientWin,         MODKEY,         SCROLL_DOWN,    spawn,          {.v = zoomout } },
#endif
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        swapmaster,     {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};

