#!/bin/bash

sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

sudo insmod h4h_drv.ko
sleep 1
sudo ./libftl &
sleep 4
sudo mkfs -t ext4 -b 4096 /dev/h4h
sudo mount \-t ext4 \-o discard /dev/h4h /media/h4h
