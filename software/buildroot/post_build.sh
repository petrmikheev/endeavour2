#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TOOLCHAIN=$SCRIPT_DIR/../../../endeavour2-ext/rv32gc-linux-toolchain
ROOTFS=$(echo $SCRIPT_DIR/../../../endeavour2-ext/buildroot-*/output/target)

mkdir -p $ROOTFS/boot
mkdir -p $ROOTFS/home
mkdir -p $ROOTFS/etc/X11
cp $SCRIPT_DIR/{inittab,fstab} $ROOTFS/etc/
cp $SCRIPT_DIR/../textwm2/S*textwm $ROOTFS/etc/init.d/
cp $SCRIPT_DIR/xorg.conf $ROOTFS/etc/X11/xorg.conf

# disable unnecessary daemons
[ -f $ROOTFS/etc/init.d/S50crond ] && mv $ROOTFS/etc/init.d/S50crond $ROOTFS/etc/init.d/_S50crond
[ -f $ROOTFS/etc/init.d/S90nodm ] && mv $ROOTFS/etc/init.d/S90nodm $ROOTFS/etc/init.d/_S90nodm
[ -f $ROOTFS/etc/init.d/S95mpd ] && mv $ROOTFS/etc/init.d/S95mpd $ROOTFS/etc/init.d/_S95mpd

if [ ! -e $ROOTFS/bin/man ] ; then
    ln -s busybox $ROOTFS/bin/man
fi
echo endeavour2 > $ROOTFS/etc/hostname

cp $SCRIPT_DIR/../textwm2/{textwm2,twmrun,twmcfg} $ROOTFS/usr/bin/
cp $SCRIPT_DIR/../textwm2/textwm2.cfg $ROOTFS/etc/

mkdir -p $ROOTFS/usr/include/endeavour2
cp -r $SCRIPT_DIR/../include/endeavour2/* $ROOTFS/usr/include/endeavour2/

if [ ! -d terminus-font-4.49.1 ] ; then
    wget http://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz
    tar xzf terminus-font-4.49.1.tar.gz
fi

cp terminus-font-4.49.1/ter-u{14,16}*.bdf $ROOTFS/usr/share/fonts

echo 'include "/usr/share/nano/*.nanorc"' > $ROOTFS/etc/nanorc

rsync -a $TOOLCHAIN/sysroot/usr/share/ $ROOTFS/usr/share/
cp -r $TOOLCHAIN/sysroot/lib/* $ROOTFS/lib/
cp -r $TOOLCHAIN/sysroot/sbin/* $ROOTFS/sbin/
cp -r $TOOLCHAIN/sysroot/usr/bin/* $ROOTFS/usr/bin/
cp -r $TOOLCHAIN/sysroot/usr/lib/* $ROOTFS/usr/lib/
cp -r $TOOLCHAIN/sysroot/usr/include/* $ROOTFS/usr/include/
cp -r $TOOLCHAIN/sysroot/usr/libexec/* $ROOTFS/usr/libexec/
cp -r $TOOLCHAIN/native/include/c++ $ROOTFS/usr/include/
cp -r $TOOLCHAIN/native/lib/gcc $ROOTFS/usr/lib/
cp -r $TOOLCHAIN/native/share/gcc* $ROOTFS/usr/share/
cp -r $TOOLCHAIN/native/share/info/* $ROOTFS/usr/share/info/
cp -r $TOOLCHAIN/native/share/man/man1/* $ROOTFS/usr/share/man/man1/
cp -r $TOOLCHAIN/native/share/man/man7/* $ROOTFS/usr/share/man/man7/
rm -rf $ROOTFS/usr/libexec/gcc
cp -r $TOOLCHAIN/native/libexec/gcc $ROOTFS/usr/libexec/
cp -r $TOOLCHAIN/native/bin/{gfortran,elfedit,gcc,strip,gcc-ranlib,gdbserver,g++,gcov,gcc-nm,strings,gcc-ar,gcov-tool,cpp} $ROOTFS/usr/bin/
#{ar,objcopy,gfortran,gprof,elfedit,gcc,strip,readelf,as,gcc-ranlib,c++filt,size,addr2line,objdump,gdbserver,g++,gcov,ranlib,gcc-nm,nm,strings,gcc-ar,gcov-tool,cpp,ld} rootfs/usr/bin/
#$TOOLCHAIN/bin/riscv32-unknown-linux-gnu-strip rootfs/usr/bin/{ar,objcopy,gfortran,gprof,elfedit,gcc,strip,readelf,as,gcc-ranlib,c++filt,size,addr2line,objdump,gdbserver,g++,gcov,ranlib,gcc-nm,nm,strings,gcc-ar,gcov-tool,cpp,ld}
$TOOLCHAIN/bin/riscv32-unknown-linux-gnu-strip -d $(find $ROOTFS/usr/libexec/gcc -type f) 2> /dev/null
#sed -i 's/#!\/bin\/bash/#!\/bin\/sh/g' rootfs/usr/bin/*

cp $ROOTFS/../build/cmake-3.31.5/bin/{cmake,cpack} $ROOTFS/usr/bin/

GROFF=$ROOTFS/../build/groff-1.22.4
cp $GROFF/{groff,grotty,nroff,tbl,troff} $ROOTFS/usr/bin/
$TOOLCHAIN/bin/riscv32-unknown-linux-gnu-strip $ROOTFS/usr/bin/{groff,grotty,tbl,troff}
mkdir -p $ROOTFS/usr/share/groff/1.22.4/font
cp -r $GROFF/font/{devascii,devutf8} $ROOTFS/usr/share/groff/1.22.4/font/
cp -r $GROFF/tmac $ROOTFS/usr/share/groff/1.22.4/tmac
sed -i 's/.mso man.local/.\\"msi man.local/g' $ROOTFS/usr/share/groff/1.22.4/tmac/an-old.tmac
