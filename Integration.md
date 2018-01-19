FFMPEG-DASH Integration

Download the source code from https://github.com/ric-sv/kickflip-dash-ffwrap or `git clone 
https://github.com/ric-sv/kickflip-dash-ffwrap.git`

Goto kickflip-dash-ffwrap directory then run below commands for ffmpeg submodule

1. `git submodule init`

2. `git submodule update`

Download Android NDK from https://developer.android.com/ndk/downloads/index.html

X264 and FFMPEG Compilation:

1. Copy build_x264.sh.example to build_x264.sh, and set HOSTARCH and NDK to match your installation.
Currently, tested with NDK-R15C 

2. Run `./build_x264.sh` to compile x264 libraries for Android using the NDK toolchain

3. Copy build.sh.example to build.sh, and set HOSTARCH and NDK to match your installation 

4. Run `./build.sh` to compile FFmpeg 3.3.4 library for Android using the NDK toolchain

5. Output files are located under `android/arm` (for x86 it will be under `android/x86/`)

6. Output files contain libraries under `lib` folder and header files under `include` folder


FFmpegWrapper Generation:

1. Goto `FFmpegWrapper-Android/FFmpegWrapper/jni` folder

2. Create a symbolic link with name `include` pointing to the include files from the FFMPEG 
compilation (`android/arm/include`)

3. Goto `FFmpegWrapper-Android/FFmpegWrapper/libs` folder 

4. Create a symbolic link named `arm` pointing to the library files from the FFMPEG 
compilation (`android/arm/lib`) (these are *.a and *.so files)

5. Export your NDK path in `~/.bash_profile` (or `~/.profile`) then run `source` cmd on it (for Mac or Ubuntu) 

6. Go to `FFmpegWrapper/jni` folder then run `./ndk-build.sh` 

7. After compilation the output files will be located in the `FFmpegWrapper/libs/armeabi` folder, containing
gdb.setup, gdbserver, libavcodec-57.so, libavdevice-57.so, libavfilter-6.so, libavformat-57.so, libavutil-55.so, 
libFFmpegWrapper.so, libswresample-2.so and libswscale-4.so)	


FFMPEG Integration for KickPlay:

1. Add the following to `kickflip-android-sdk/sdk/build.gradle` to automatically copy 
the `FFmpegWrapper/libs/armeabi` files into `kickflip-android-sdk/sdk/src/main/jniLibs/armeab` 
during a project rebuild (or clean) in Android Studio: 
```
   task copyFFmpegWrapperLibs(type: Copy) {
       description = 'Copying the FFmpegWrapper lib files...'
       from '../../FFmpegWrapper-Android/FFmpegWrapper/libs/armeabi'
       into 'src/main/jniLibs/armeabi'
       include '*'
       doFirst {
           println "[INFO] Copying the FFmpegWrapper lib files"
       }
   }

   task rmFFmpegWrapperLibs(type: Delete) {
       description = 'Removing the FFmpegWrapper lib files...'
       delete 'src/main/jniLibs/armeabi'
       doFirst {
           println "[INFO] Removing previous FFmpegWrapper lib files"
       }
   }

   copyFFmpegWrapperLibs.dependsOn rmFFmpegWrapperLibs

   project.afterEvaluate {
     preBuild.dependsOn copyFFmpegWrapperLibs
   }

   clean.dependsOn copyFFmpegWrapperLibs
   clean.mustRunAfter copyFFmpegWrapperLibs
```

2. Open kickflip-android-example app through Android studio then Run the App and it will download into Asus Phone.




 

