#!/bin/bash
sudo mkdir /media/h4h/1
sudo postmark < postmark-config-1.cfg &
sudo mkdir /media/h4h/2
sudo postmark < postmark-config-2.cfg &
sudo mkdir /media/h4h/3
sudo postmark < postmark-config-3.cfg &

for job in `jobs -p`
do
	echo $job
  	wait $job
done
