# Photo booth

### build

```shell
g++ -Wall -std=c++11 gst_jpg_on_demand.cpp -o test $(pkg-config --cflags --libs gstreamer-app-1.0)
```

### xinitrc

`.xprofile`

```shell
# hdmi display is rotated
xrandr --output HDMI-0 --rotate inverted

xbindkeys --display :0
```

### xbindkeys

```
"start-snap"
  b:9

"start-booth"
  b:8
```

### xorg configuration

hdmi display is rotated, so we need to rotate touchscreen also

`/etc/X11/xorg.conf.d/40-libinput.conf`

```
Section "InputClass"
        Identifier "libinput touchscreen catchall"
        MatchIsTouchscreen "on"
        MatchDevicePath "/dev/input/event*"
        Option "CalibrationMatrix" "1 0 0 -1 0 0 1"
        Driver "libinput"
EndSection
```
