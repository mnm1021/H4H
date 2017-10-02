sudo mkdir -p /usr/share/h4h_drv
sudo touch /usr/share/h4h_drv/ftl.dat
sudo touch /usr/share/h4h_drv/dm.dat

sudo rmmod nvme
sudo insmod dumbssd.ko
sleep 1
sudo insmod nvme.ko
sleep 1
sudo insmod h4h_drv.ko
sleep 1
sudo mkfs -t ext4 -b 4096 /dev/H4H
sudo mount -t ext4 -o discard /dev/H4H /media/H4H
