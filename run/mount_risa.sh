sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

# compile risa
cd ../../risa-f2fs/tools
make
sudo make install
cd -

#sudo insmod risa_dev_ramdrive_intr.ko
sudo insmod risa_dev_h4h.ko
sudo insmod h4h_drv_risa.ko
sudo insmod risa.ko 
sudo ./h4h_format /dev/h4h
sudo mkfs.f2fs -a 0 -s 8 /dev/h4h
sudo mount \-t f2fs \-o discard /dev/h4h /media/h4h
