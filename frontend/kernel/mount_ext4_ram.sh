sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

sudo insmod ../../devices/ramdrive_timing/risa_dev_ramdrive_timing.ko
sudo insmod h4h_drv.ko
sudo mkfs -t ext4 -b 4096 /dev/H4H
sudo mount -t ext4 -o discard /dev/H4H /media/H4H
