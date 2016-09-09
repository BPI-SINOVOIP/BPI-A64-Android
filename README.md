# BPI-M64 Android 6.1 Source code
---------
###1 Build Android BSP
 $cd lichee
 
   $ ./build.sh config    

Welcome to mkscript setup progress
All available chips:
   1. sun50iw1p1
   2. sun8iw10p1
   3. sun8iw1p1
   4. sun8iw3p1
   5. sun8iw5p1
   6. sun8iw6p1
   7. sun8iw7p1
   8. sun8iw8p1
   9. sun8iw9p1
   10. sun9iw1p
   
Choice: 1

All available platforms:
   1. android
   2. dragonboard
   3. linux
   4. camdroid
   5. secureandroi
   
Choice: 1


All available kernel:
   1. linux-3.10
   2. linux-3.
  
Choice: 1

All available boards:
   1. fpga
   2. p1
   3. perf1_v1_0
   4. perf2_v1_0
   5. perf3_v1_0
   6. t1
   7. t1_v
   
Choice:2


   $ ./build.sh 

***********

###2 Buidl Android 
   $cd ../android

   $source build/envsetup.sh
   
   $lunch    //(tulip_bpi_m64-eng)
   
   $extract-bsp
   
   $make -j8
   
   $pack
