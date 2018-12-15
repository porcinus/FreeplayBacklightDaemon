#!/bin/bash
cp nns-freeplay-backlight-daemon.service /lib/systemd/system/nns-freeplay-backlight-daemon.service
systemctl enable nns-freeplay-backlight-daemon.service