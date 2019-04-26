export USE_NDK=1
NDK_BUILD=/workspace/android-ndk/android-ndk-r10e/ndk-build
NDK_PROJECT_PATH=`pwd`
NDK_DEBUG=0
APP_ABI=armeabi,x86,arm64-v8a#,mips,armeabi-v7a,mips64,x86_64
APP_PLATFORM=android-14 #8~2.2 9~2.3 14~4.0 17~4.2 19~4.4 21~5.0 22~5.1 23~6.0 24~6.x
APP_BUILD_SCRIPT=Android.mk
NDK_OUT=../out/gps
NDK_LIBS_OUT=../libgps

gps.default.so: clean
	$(NDK_BUILD) V=0 NDK_OUT=$(NDK_OUT)  NDK_LIBS_OUT=$(NDK_LIBS_OUT) APP_BUILD_SCRIPT=$(APP_BUILD_SCRIPT) NDK_PROJECT_PATH=$(NDK_PROJECT_PATH) NDK_DEBUG=$(NDK_DEBUG) APP_ABI=$(APP_ABI) APP_PLATFORM=$(APP_PLATFORM)

clean:
	rm -rf $(NDK_OUT) $(NDK_LIBS_OUT) *~
