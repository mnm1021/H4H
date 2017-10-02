#!/bin/bash

# umount the file-system
sudo umount /media/h4h

# kill libftl
pid=`ps -ef | grep "sudo [.]/libftl" | awk '{print $2}'`
if [ $pid > 0 ]
then
	 echo 'sudo kill -2 libftl ('$pid')'
	sudo kill -2 $pid
	sudo pkill -9 libftl
	sleep 1
fi

# rm the device
sudo rmmod h4h_drv
sudo rmmod risa_dev_*
sudo rmmod f2fs
sudo rm 0 1
