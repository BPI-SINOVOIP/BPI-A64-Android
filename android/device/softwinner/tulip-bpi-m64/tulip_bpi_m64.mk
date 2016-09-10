$(call inherit-product, device/softwinner/tulip-common/tulip_64_bit.mk)
$(call inherit-product, build/target/product/full_base.mk)
$(call inherit-product, device/softwinner/tulip-common/tulip-common.mk)
$(call inherit-product-if-exists, device/softwinner/tulip-bpi-m64/modules/modules.mk)

DEVICE_PACKAGE_OVERLAYS := device/softwinner/tulip-bpi-m64/overlay \
                           $(DEVICE_PACKAGE_OVERLAYS)

PRODUCT_PACKAGES += Launcher3

PRODUCT_PACKAGES += \
    ESFileExplorer \
    VideoPlayer \
    Bluetooth
#   PartnerChromeCustomizationsProvider

#------------------ BPI-M64 3rd-party apk --------------------------
PRODUCT_PACKAGES += \
	Camerafi    \
	Kodi        \
        Pinyin      \
        Zhuyin        


PRODUCT_COPY_FILES += \
    device/softwinner/tulip-bpi-m64/kernel:kernel \
    device/softwinner/tulip-bpi-m64/fstab.sun50iw1p1:root/fstab.sun50iw1p1 \
    device/softwinner/tulip-bpi-m64/init.sun50iw1p1.rc:root/init.sun50iw1p1.rc \
    device/softwinner/tulip-bpi-m64/init.recovery.sun50iw1p1.rc:root/init.recovery.sun50iw1p1.rc \
    device/softwinner/tulip-bpi-m64/ueventd.sun50iw1p1.rc:root/ueventd.sun50iw1p1.rc \
    device/softwinner/common/verity/rsa_key/verity_key:root/verity_key \
    device/softwinner/tulip-bpi-m64/modules/modules/nand.ko:root/nand.ko \
    device/softwinner/tulip-bpi-m64/modules/modules/sunxi_tr.ko:root/sunxi_tr.ko \
    device/softwinner/tulip-bpi-m64/modules/modules/disp.ko:root/disp.ko \
    device/softwinner/tulip-bpi-m64/modules/modules/sunxi-keyboard.ko:recovery/root/sunxi-keyboard.ko \
    device/softwinner/tulip-bpi-m64/modules/modules/sw-device.ko:recovery/root/sw-device.ko \
    device/softwinner/tulip-bpi-m64/modules/modules/gslX680new.ko:recovery/root/gslX680new.ko \

#Justin Porting Start
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.xml:system/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:system/etc/permissions/android.software.verified_boot.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:system/etc/permissions/android.hardware.ethernet.xml


PRODUCT_COPY_FILES += \
    device/softwinner/tulip-bpi-m64/bluetooth/bt_vendor.conf:system/etc/bluetooth/bt_vendor.conf

#Justin Porting End


PRODUCT_COPY_FILES += \
    device/softwinner/tulip-bpi-m64/configs/camera.cfg:system/etc/camera.cfg \
    device/softwinner/tulip-bpi-m64/configs/gsensor.cfg:system/usr/gsensor.cfg \
    device/softwinner/tulip-bpi-m64/configs/media_profiles.xml:system/etc/media_profiles.xml \
    device/softwinner/tulip-bpi-m64/configs/sunxi-keyboard.kl:system/usr/keylayout/sunxi-keyboard.kl \
    device/softwinner/tulip-bpi-m64/configs/tp.idc:system/usr/idc/tp.idc


#Justin Porting Start
PRODUCT_COPY_FILES += \
    device/softwinner/tulip-bpi-m64/hawkview/sensor_list_cfg.ini:system/etc/hawkview/sensor_list_cfg.ini

#Justin Porting End

# bootanimation
PRODUCT_COPY_FILES += \
    device/softwinner/tulip-bpi-m64/media/bootanimation.zip:system/media/bootanimation.zip

# Radio Packages and Configuration Flie
$(call inherit-product, device/softwinner/common/rild/radio_common.mk)
#$(call inherit-product, device/softwinner/common/ril_modem/huawei/mu509/huawei_mu509.mk)
#$(call inherit-product, device/softwinner/common/ril_modem/Oviphone/em55/oviphone_em55.mk)

#Justin  Porting Start
# Realtek wifi efuse map
#PRODUCT_COPY_FILES += \
#    device/softwinner/tulip-bpi-m64/wifi_efuse_8723bs-vq0.map:system/etc/wifi/wifi_efuse_8723bs-vq0.map
#Justin  Porting Start


PRODUCT_PROPERTY_OVERRIDES += \
	ro.frp.pst=/dev/block/by-name/frp

#Justin  Porting Start
PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.usb.config=mtp,adb\
    ro.adb.secure=0 \
    rw.logger=0

#Justin  Porting End

PRODUCT_PROPERTY_OVERRIDES += \
    ro.zygote.disable_gl_preload=false

#Justin  Porting Start
#BPI_M64 Set Density
# HDMI Display = ro.sf.lcd_density=160
# LCD  Display = ro.sf.lcd_density=120
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=160 \
#Justin  Porting End

PRODUCT_PROPERTY_OVERRIDES += \
    ro.spk_dul.used=false \



#Justin  Porting Start
PRODUCT_PROPERTY_OVERRIDES += \
    ro.property.tabletUI=false 
#Justin  Porting End



#Justin Porting Start
PRODUCT_PROPERTY_OVERRIDES += \
   	persist.sys.timezone=Asia/Taipei \
	persist.sys.language=EN \
	persist.sys.country=US

#Justin Porting End

# stoarge
PRODUCT_PROPERTY_OVERRIDES += \
    persist.fw.force_adoptable=true

PRODUCT_CHARACTERISTICS := tablet

PRODUCT_AAPT_CONFIG := tvdpi xlarge hdpi xhdpi large
PRODUCT_AAPT_PREF_CONFIG := tvdpi

$(call inherit-product-if-exists, vendor/google/products/gms_base.mk)

PRODUCT_BRAND := Allwinner
PRODUCT_NAME := tulip_bpi_m64
PRODUCT_DEVICE := tulip-bpi-m64
PRODUCT_MODEL := BPI-M64
PRODUCT_MANUFACTURER := Allwinner
