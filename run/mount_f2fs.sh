sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

# compile f2fs
cd ../../f2fs_org/tools/
make
sudo make install
cd -

#sudo insmod risa_dev_ramdrive_intr.ko
sudo insmod risa_dev_h4h.ko
sudo insmod h4h_drv_page.ko
sudo insmod f2fs.ko
sudo ./h4h_format /dev/h4h
sudo mkfs.f2fs -a 0 -s 2 /dev/h4h
sudo mount \-t f2fs \-o discard /dev/h4h /media/h4h
