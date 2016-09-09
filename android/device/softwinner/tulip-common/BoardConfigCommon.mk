include device/softwinner/common/BoardConfigCommon.mk

TARGET_BOARD_PLATFORM := tulip
TARGET_USE_NEON_OPTIMIZATION := true

TARGET_CPU_SMP := true

TARGET_NO_BOOTLOADER := true

TARGET_BOOTLOADER_BOARD_NAME := exdroid
TARGET_BOOTLOADER_NAME := exdroid

BOARD_EGL_CFG := device/softwinner/tulip-common/egl/egl.cfg
BOARD_KERNEL_BASE := 0x41000000
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x80000

#SurfaceFlinger's configs
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true

BOARD_CHARGER_ENABLE_SUSPEND := true

BOARD_SEPOLICY_DIRS := \
    device/softwinner/tulip-common/sepolicy

#Justin Porting for BPI-M64 Root Start
BOARD_SEPOLICY_UNION := \
	bluetooth.te \
	device.te \
	file_contexts \
	genfs_contexts \
	gpsd.te \
	init.te \
	kernel.te \
	mediaserver.te \
	netd.te \
	preinstall.te \
	pvrsrvctl.te \
	recovery.te \
        rild.te \
	sayeye.te \
	sensors.te \
	service_contexts \
	shell.te \
	surfaceflinger.te \
	system_app.te \
	system_server.te \
	unconfined.te \
	vold.te \
	wpa.te \
    file.te \
    logger.te \
	platform_app.te
#Justin Porting for BPI-M64 Root End

USE_OPENGL_RENDERER := true


