#define PACKAGE_VERSION "1.0"
#define HAVE_REALLOCARRAY 0

#define FADE_WINDOWS 0
#define FADE_TRANS 0
#define FADE_IN_STEP 0.028
#define FADE_OUT_STEP 0.1
#define FADE_DELTA 2

#define SHADOWS 1 /* Turns shadows on/off, options: 
    0 = Disable Shadows
    1 = Enable Shadows
    2 = Enables server side shadows instead of client side
*/

#define SHADOW_RADIUS 10 // blur radius for shadows
#define SHADOW_OPACITY 0.7
#define SHADOW_OFFSET_X 0
#define SHADOW_OFFSET_Y 0
#define SHADOW_EXCLUDE_TYPES {"_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_UTILITY" }
#define SHADOW_EXCLUDE_CLASSES {}

#define ZOOM_EXCLUDE_TYPES {"_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_STATE_FULLSCREEN"}
#define ZOOM_EXCLUDE_CLASSES {}

#define ZOOM_BEZIER 0.22,1.3,0.36,1.0 // zoom bezier
#define ZOOM_DURATION 470.0 // zoom duration in ms
#define ZOOM_MIN 0.1 // minimum zoom value
#define ZOOM_MAX 5 // maximum zoom value

// configure shadow colors via -k flag when launching vcompmgr or when it is already running.
