# BPI-M64 Android 6.1 Source code
-------
Notice: Please download this file to the correct path  https://drive.google.com/open?id=0B_YnvHgh2rwjaVRrc2VoR0xLTlE

1.  gcc-linaro-aarch64.tar.xz   ==> /lichee/buildroot/dl

2.  gcc-linaro-aarch64.tar.xz   ==> /lichee/brandy/toolchain

3.  gcc-linaro-arm.tar.xz       ==> /lichee/brandy/toolchain

---------
###1 Build Android BSP

   $ cd lichee
 
   $ ./build.sh config    

Welcome to mkscript setup progress
All available chips:
   1. sun50iw1p1

Choice: 1

All available platforms:
   1. android
   2. dragonboard
   3. linux
   4. camdroid
   5. secureandroid
   
Choice: 1

All available kernel:
   1. linux-3.10
   2. linux-3.4
   
Choice: 1

All available boards:
   1. bpi-m64-hdmi
   2. bpi-m64-lcd
   
Choice: 1 or 2

   $ ./build.sh 

***********

###2 Buidl Android 

   $cd ../android

   $source build/envsetup.sh
   
   $lunch    //(bpi_m64_hdmi-eng or bpi_m64_lcd-eng)
   
   $extract-bsp
   
   $make -j8
   
   $pack
