# FreeplayBacklightDaemon

This programs is design to work aside of Freeplay setPCA9633 on Raspberry Pi 3 on Freeplay Zero/CM3 platform (with installed backlight control).
It also require to have juj fbcp-ili9341 driver running with backlight control and backlight pin set to put backlight in 'sleep mode', wake is manage by backlight pin and evdev checking.

# History

0.1a : Initial release.
0.1b : Change way gpio cheching work.
0.1c : Add evdev support, refresh device list each 15sec, all 'event' checked during 0.2sec.


# Provided scripts :
- compile.sh : Compile cpp file (run this first).
- install.sh : Install service (need restart).
- remove.sh : Remove service.

Don't miss to edit nns-freeplay-backlight-daemon.service set path and pin correctly.


# Compiling juj/fbcp-ili9341 for Freeplay CM3

Follow instructions provided (https://github.com/juj/fbcp-ili9341/).

Replace 'cmake [options] ..' by 'cmake -DARMV8A=ON -DFREEPLAYTECH_WAVESHARE32B=ON -DSPI_BUS_CLOCK_DIVISOR=6 -DDISPLAY_BREAK_ASPECT_RATIO_WHEN_SCALING=ON -DUSE_DMA_TRANSFERS=ON -DBACKLIGHT_CONTROL=ON -DGPIO_TFT_BACKLIGHT=31 -DSTATISTICS=0 ..'
