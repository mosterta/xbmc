#!/bin/sh

#USEHWR="A10HWR=1"

if [ -d /lib/arm-linux-gnueabihf ]
then
	#assume linaro rootfs
	USER=linaro
	XBMC=/allwinner/xbmc-pvr-binhf/lib/xbmc/xbmc.bin
else
	#assume miniand rootfs
	USER=miniand
	XBMC=/allwinner/xbmc-pvr-bin/lib/xbmc/xbmc.bin
fi

#
#some q&d to avoid editing system config files.
#
depmod -a `uname -r`
modprobe lcd
modprobe hdmi
modprobe ump
modprobe disp
modprobe mali
modprobe mali_drm
chmod 666 /dev/mali /dev/ump /dev/cedar_dev /dev/disp
chmod -R 666 /dev/input/*
chmod -R 666 /dev/snd/*

stop lightdm

#thanks, Sam Nazarko 

while true
do
    su - $USER -c "$USEHWR $XBMC --standalone -fs --lircdev /var/run/lirc/lircd 2>&1 | logger -t xbmc"
    case "$?" in
         0) # user quit. 
	    	sleep 2 ;;
        64) # shutdown system.
            poweroff;;
        65) # warm Restart xbmc
            sleep 2 ;;
        66) # Reboot System
            reboot;;
         *) # this should not happen
            sleep 30 ;;
    esac
done
