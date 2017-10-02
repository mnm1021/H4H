sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

sudo insmod risa_dev_ramdrive_timing.ko
#sudo insmod risa_dev_ramdrive_intr.ko
#sudo insmod risa_dev_h4h.ko
#sudo insmod h4h_drv_page.ko
sudo insmod h4h_drv.ko
#sudo ./h4h_format /dev/h4h
sudo mkfs -t ext4 -b 4096 /dev/h4h
sudo mount \-t ext4 \-o discard /dev/h4h /media/h4h
#sudo mount \-t ext4 /dev/h4h /media/h4h
