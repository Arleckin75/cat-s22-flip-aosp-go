LOCAL_CAT_PATH := device/cat/s22
$(call inherit-product, $(LOCAL_CAT_PATH)/go_profile.mk)
PRODUCT_PACKAGES += cat_s22_keypad cat_s22_keylight cat_s22_cover CatS22Cover CatS22Ims CatS22Camera cat_s22_fcm4
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(LOCAL_CAT_PATH)/sepolicy
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += $(LOCAL_CAT_PATH)/compatibility_matrix.device.xml
PRODUCT_COPY_FILES += \
    $(LOCAL_CAT_PATH)/cat-s22.rc:$(TARGET_COPY_OUT_SYSTEM)/etc/init/cat-s22.rc \
    $(LOCAL_CAT_PATH)/cat-s22-settings.sh:$(TARGET_COPY_OUT_SYSTEM)/bin/cat-s22-settings.sh \
    $(LOCAL_CAT_PATH)/privapp-permissions-cat-ims.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/privapp-permissions-cat-ims.xml \
    $(LOCAL_CAT_PATH)/default-permissions-cat-camera.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/default-permissions/default-permissions-cat-camera.xml \
    $(LOCAL_CAT_PATH)/default-permissions-cat-cover.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/default-permissions/default-permissions-cat-cover.xml

# The cover renderer runs for ten seconds per close; idle is input IRQ only.
PRODUCT_SYSTEM_PROPERTIES += \
    persist.cat.s22.keypad=1 \
    persist.cat.s22.keylight=1 \
    persist.cat.s22.outer=1 \
    persist.dbg.volte_avail_ovr=1 \
    persist.dbg.vt_avail_ovr=1 \
    persist.dbg.wfc_avail_ovr=1 \
    persist.sys.phh.ims.caf=true \
    persist.sys.phh.ims.floss=false

# Hardware Back/Home/Menu/App-switch keys: no on-screen navigation bar, as on
# stock (config_showNavigationBar=false). TrebleDroid forces it on through
# persist.sys.phh.mainkeys=0 -> qemu.hw.mainkeys; product props load last.
PRODUCT_PRODUCT_PROPERTIES += persist.sys.phh.mainkeys=1
