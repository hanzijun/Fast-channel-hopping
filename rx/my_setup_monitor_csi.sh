#!/usr/bin/sudo /bin/bash
sudo modprobe -r iwldvm iwlwifi mac80211
modprobe -r iwlwifi mac80211 cfg80211
modprobe iwlwifi connector_log=0x1
if [ "$#" -ne 2 ]; then
    echo "Going to use default settings!"
    chn=136
    bw=HT20
else
    chn=$1
    bw=$2
fi
iwconfig wlan0 mode monitor 2>/dev/null 1>/dev/null
while [ $? -ne 0 ]
do
    iwconfig wlan0 mode monitor 2>/dev/null 1>/dev/null
done
ifconfig wlan0 up 2>/dev/null 1>/dev/null
while [ $? -ne 0 ]
do
  ifconfig wlan0 up 2>/dev/null 1>/dev/null
done

sudo iw dev wlan0 interface add mon0 type monitor
sudo ifconfig mon0 up
while [ $? -ne 0 ]
do
           sudo ifconfig mon0 up
done
sudo iw mon0 set channel $chn $bw

