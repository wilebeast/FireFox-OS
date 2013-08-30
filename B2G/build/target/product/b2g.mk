PRODUCT_NAME := b2g
PRODUCT_BRAND := generic
PRODUCT_DEVICE := generic

TARGET_PROVIDES_INIT_RC := true
CONFIG_ESD := no
HTTP := android

PRODUCT_PACKAGES := \
	b2g.sh \
    b2g-ps \
	fakeperm \
	gaia \
	gecko \
	init.rc \
	init.b2g.rc \
    killer \
	rilproxy \
	sources.xml \
	MozTT-Light.ttf \
	MozTT-Regular.ttf \
	MozTT-Medium.ttf \
	MozTT-Bold.ttf \
	$(NULL)

ifeq ($(ENABLE_LIBRECOVERY),true)
PRODUCT_PACKAGES += \
  librecovery
endif
