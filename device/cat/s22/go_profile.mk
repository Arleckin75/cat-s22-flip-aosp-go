# Android Go profile for the CAT S22 Flip (1.9 GB RAM, ARM32).
# Mirrors build/make/target/product/go_defaults_common.mk, which puts its
# settings on the vendor partition. This port does not build vendor (it stays
# stock V30), so the settings go to the product partition here: product
# properties load after vendor ones. ro.config.low_ram=true is not repeated:
# the stock V30 vendor build.prop already sets it.

# Leaner ART output: no debug ART, less Java debug info in dex files.
# PRODUCT_SYSTEM_SERVER_COMPILER_FILTER is left unset: Soong already compiles
# system_server jars that ship a profile with `speed-profile`, so Go's setting
# would change nothing here.
PRODUCT_ART_TARGET_INCLUDE_DEBUG_BUILD := false
PRODUCT_MINIMIZE_JAVA_DEBUG_INFO := true

# Product properties load after vendor, so these replace V30's 512m/196m heap
# and V30's ro.lmk.use_psi=false: the 4.9 kernel has PSI (/proc/pressure), so
# lmkd can use pressure-stall events instead of the legacy vmpressure mode.
PRODUCT_PRODUCT_PROPERTIES += \
    dalvik.vm.heapgrowthlimit=128m \
    dalvik.vm.heapsize=256m \
    dalvik.vm.madvise-random=true \
    ro.lmk.use_psi=true \
    ro.lmk.critical_upgrade=true \
    ro.lmk.upgrade_pressure=40 \
    ro.lmk.downgrade_pressure=60 \
    ro.lmk.kill_heaviest_task=false \
    pm.dexopt.downgrade_after_inactive_days=10 \
    pm.dexopt.shared=verify

# Drop preinstalled apps a flip phone does not use (see Android.mk).
PRODUCT_PACKAGES += CatS22RemovePackages

# Go launcher (no taskbar, lighter recents). It overrides Launcher3QuickStep.
PRODUCT_PACKAGES += Launcher3QuickStepGo

# android.hardware.ram.low: what go_handheld_core_hardware.xml declares on Go
# vendors. With ro.config.low_ram, features marked notLowRam (picture in
# picture, secondary-display activities, managed users) are dropped as well.
PRODUCT_COPY_FILES += \
    device/cat/s22/cat-s22-low-ram.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/cat-s22-low-ram.xml
