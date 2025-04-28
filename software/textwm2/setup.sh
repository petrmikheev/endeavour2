#!/bin/sh

stty raw
/mnt/textwm < /dev/ttyS0 &
sleep 2
cat /mnt/textwm.cfg > /dev/textwmcfg
export TERM=ansi
export HOME=/root
export PS1='\u:\[\e[34m\]\w\[\e[m\]\$ '
echo -e "Kernel messages redirected to /dev/ttyS0. Starting root shell.\n" > /dev/ttyp0
setsid sh -c 'exec sh </dev/ttyp1 >/dev/ttyp1 2>&1' &
setsid sh -c 'exec sh </dev/ttyp0 >/dev/ttyp0 2>&1'
