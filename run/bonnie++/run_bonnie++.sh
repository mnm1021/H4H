#!/bin/bash

# for 2 GB
sudo mkdir /media/h4h/1
sudo bonnie++ -s 320:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/1 &
sudo mkdir /media/h4h/2
sudo bonnie++ -s 320:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/2 &
sudo mkdir /media/h4h/3
sudo bonnie++ -s 320:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/3 &

# for 16 GB
#sudo mkdir /media/h4h/1
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/1 &
#sudo mkdir /media/h4h/2
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/2 &
#sudo mkdir /media/h4h/3
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/3 &
#sudo mkdir /media/h4h/4
#sudo bonnie++ -s 2500:1024 -n 40 -x 1 -r 8 -z 1 -u root -d /media/h4h/4 &

for job in `jobs -p`
do
	echo $job
   	wait $job
done
