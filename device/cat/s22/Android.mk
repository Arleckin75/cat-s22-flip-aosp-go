LOCAL_PATH := $(call my-dir)

# Keeps these preinstalled apps out of the image. None of them stores user
# data; all are unused on a flip phone. Android 15 and later honour overrides only for
# EXECUTABLES, SHARED_LIBRARIES and ETC modules (a phony package is silently
# ignored), so this is a small ETC file that documents the list on the device.
include $(CLEAR_VARS)
LOCAL_MODULE := CatS22RemovePackages
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_STEM := cat-s22-removed-packages.txt
LOCAL_SRC_FILES := cat-s22-removed-packages.txt
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_OVERRIDES_MODULES := \
    EasterEgg \
    BasicDreams \
    PhotoTable \
    LiveWallpapersPicker \
    BuiltInPrintService \
    PrintRecommendationService \
    DeviceAsWebcam \
    AvatarPicker \
    MtkInCallService \
    DeviceDiagnostics \
    ApertureLensLauncher \
    DynamicSystemInstallationService \
    Tag
include $(BUILD_PREBUILT)
