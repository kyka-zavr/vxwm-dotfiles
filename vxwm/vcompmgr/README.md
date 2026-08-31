> [!WARNING]
> Very experimental software, only use with amd or newer intel gpus.

# vcompmgr

A lightweight x11 compositor (forked from xcompmgr). Requires a server with XFIXES, DAMAGE, RENDER, and COMPOSITE extensions.
Currently broken for window managers that have window decorations and many nvidia gpus.

By lightweight i mean, very lightweight, it has less lines of code than xcompmgr it self while featuring these features alongside the default xcompmgr ones:

    True zoom
    Colored shadows

And a few improvements were also added:

    Shadows on argb windows
    Shadows caching (hello perfomance :D)

## Building

```sh
sudo make clean install
```

Dependencies: `xcomposite`, `xfixes`, `xdamage`, `xrender`, `xext`, `x11`, `libdrm`

## Configuration

Default options are in `config.h` and are set at compile time. Any flag passed at runtime overrides the default one.
config.h has enough information about configuration.

## Usage

```sh
vcompmgr [options]
```

### Shadows

```
-c          # enable shadows
-r value    # blur radius
-l value    # horizontal offset
-t value    # vertical offset
-k color    # shadow color, e.g. -k '000000'
```

Shadow color can also be changed at runtime without restarting:

```sh
vcompmgr -k 'ff0000'
```

### Fading

```
-f          # enable fade in/out on map/unmap
-D value    # time between fade steps in ms
```

### Zoom

```
-Z value    # set zoom to value (e.g. -Z 1.5)
-Z +value   # zoom in by value
-Z -value   # zoom out by value
-G          # print current zoom level
-m value    # minimum zoom
-M value    # maximum zoom
-u value    # animation duration in ms
-b value    # animation cubic-bezier, e.g. -b 0.22,1.0,0.36,1.0
```

## Repository

https://codeberg.org/wh1tepearl/vcompmgr

Bug reports and patches welcome.
