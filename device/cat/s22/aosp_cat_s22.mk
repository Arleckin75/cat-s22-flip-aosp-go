# CAT S22 Flip: AOSP 16 + TrebleDroid, Android Go profile.
# ARM32 userspace with 64-bit binder (TrebleDroid "a64"), stock V30 vendor and
# kernel. Mirrors TrebleDroid's generated treble_a64_bvN product.
TARGET_GAPPS_ARCH := arm
# No bootanimation.zip: AOSP's built-in animation is drawn in code and fits
# the 480x640 panel by itself.
include build/make/target/product/aosp_arm.mk
$(call inherit-product, device/phh/treble/base.mk)

PRODUCT_NAME := aosp_cat_s22
PRODUCT_DEVICE := tdgsi_a64_ab
PRODUCT_BRAND := Cat
PRODUCT_SYSTEM_BRAND := Cat
PRODUCT_MANUFACTURER := Cat
PRODUCT_MODEL := S22 Flip
# Overwrite the inherited "emulator" characteristics.
PRODUCT_CHARACTERISTICS := device

TARGET_SUPPORTS_32_BIT_APPS := true
TARGET_SUPPORTS_64_BIT_APPS := false
TARGET_NO_KERNEL_OVERRIDE := true
TARGET_NO_KERNEL_IMAGE := true
TARGET_USES_PREBUILT_VENDOR_SEPOLICY := true
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
BOARD_EXT4_SHARE_DUP_BLOCKS := true
PRODUCT_SYSTEM_PROPERTIES += ro.sf.lcd_density=200
$(call inherit-product, device/cat/s22/adaptation.mk)
