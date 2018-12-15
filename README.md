# FreeplayBacklightDaemon

This programs is design to work aside of Freeplay setPCA9633 on Raspberry Pi 3 on Freeplay Zero/CM3 platform (with installed backlight control).
It also require to have juj fbcp-ili9341 driver running with backlight control and backlight pin set.


# Provided scripts :
- compile.sh : Compile cpp file (run this first).
- install.sh : Install service (need restart)
- remove.sh : Remove service 

Don't miss to edit nns-freeplay-backlight-daemon.service set path and pin correctly.
