BB_PATH := $(call my-dir)

include $(CLEAR_VARS)

BB_TC_DIR := $(realpath $(shell dirname $(TARGET_TOOLS_PREFIX)))
BB_TC_PREFIX := $(shell basename $(TARGET_TOOLS_PREFIX))
BB_LDFLAGS := -nostdlib -Bdynamic -Wl,-z,muldefs$(shell if test $(PLATFORM_SDK_VERSION) -lt 16; then echo -ne ',-T$(CURDIR)/$(BUILD_SYSTEM)/armelf.x'; fi),-dynamic-linker,/system/bin/linker,-z,nocopyreloc,--no-undefined $(CURDIR)/$(TARGET_CRTBEGIN_DYNAMIC_O) $(CURDIR)/$(TARGET_CRTEND_O) -L$(CURDIR)/$(TARGET_OUT_STATIC_LIBRARIES) -L$(CURDIR)/$(TARGET_OUT_SHARED_LIBRARIES)
# FIXME remove -fno-strict-aliasing once all aliasing violations are fixed
BB_COMPILER_FLAGS := $(subst -I ,-I$(CURDIR)/,$(subst -include ,-include $(CURDIR)/,$(TARGET_GLOBAL_CFLAGS))) $(foreach d,$(TARGET_C_INCLUDES),-I$(CURDIR)/$(d)) -fno-stack-protector -Wno-error=format-security -fno-strict-aliasing
BB_LDLIBS := dl m c gcc
ifneq ($(strip $(SHOW_COMMANDS)),)
BB_VERBOSE="V=1"
endif

LOCAL_MODULE := busybox
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional

include $(BUILD_PREBUILT)

BB_OUT_INTERMEDIATES := $(dir $(LOCAL_BUILT_MODULE))

BB_MAKE_FLAGS := O=$(CURDIR)/$(BB_OUT_INTERMEDIATES) CROSS_COMPILE=$(notdir $(TARGET_TOOLS_PREFIX)) CONFIG_EXTRA_CFLAGS="$(BB_COMPILER_FLAGS)" EXTRA_LDFLAGS="$(BB_LDFLAGS)" LDLIBS="$(BB_LDLIBS)" $(BB_VERBOSE)

$(BB_OUT_INTERMEDIATES):
	mkdir -p "$@"

$(BB_OUT_INTERMEDIATES)/.config: $(BB_PATH)/configs/android_defconfig | $(BB_OUT_INTERMEDIATES)
	# sed -e "s|^CONFIG_CROSS_COMPILER_PREFIX=.*|CONFIG_CROSS_COMPILER_PREFIX=\"$(BB_TC_PREFIX)\"|;s|^CONFIG_EXTRA_CFLAGS=.*|CONFIG_EXTRA_CFLAGS=\"$(BB_COMPILER_FLAGS)\"|" configs/android_defconfig >.config
	for i in $(BB_PATH)/scripts/kconfig/*_shipped; do ln -sf `basename $$i` $${i/_shipped/}; done
	export PATH=$(BB_TC_DIR):$(PATH) && $(MAKE) -C $(BB_PATH) android_defconfig $(BB_MAKE_FLAGS)

$(LOCAL_BUILT_MODULE): $(TARGET_CRTBEGIN_DYNAMIC_O) $(TARGET_CRTEND_O) $(TARGET_OUT_SHARED_LIBRARIES)/libm.so $(TARGET_OUT_SHARED_LIBRARIES)/libc.so $(TARGET_OUT_SHARED_LIBRARIES)/libdl.so $(BB_OUT_INTERMEDIATES)/.config FORCE
	export PATH=$(BB_TC_DIR):$(PATH) && $(MAKE) -C $(BB_PATH) $(BB_MAKE_FLAGS)


ifneq ($(BB_ADD_SYMLINKS),false)
BUSYBOX_TOOLS := \
	ar \
	arp \
	ash \
	awk \
	base64 \
	basename \
	beep \
	blkid \
	blockdev \
	bunzip2 \
	bzcat \
	cal \
	catv \
	chat \
	chattr \
	chgrp \
	chpst \
	chroot \
	chrt \
	chvt \
	cksum \
	clear \
	comm \
	cp \
	cpio \
	cttyhack \
	cut \
	dc \
	deallocvt \
	depmod \
	devmem \
	diff \
	dirname \
	dnsd \
	dos2unix \
	dpkg \
	dpkg-deb \
	du \
	dumpkmap \
	echo \
	ed \
	egrep \
	envdir \
	envuidgid \
	expand \
	fakeidentd \
	false \
	fbset \
	fbsplash \
	fdflush \
	fdformat \
	fdisk \
	fgconsole \
	fgrep \
	find \
	findfs \
	flash_lock \
	flash_unlock \
	flashcp \
	flock \
	fold \
	freeramdisk \
	ftpd \
	ftpget \
	ftpput \
	fuser \
	getopt \
	grep \
	gunzip \
	halt \
	hdparm \
	head \
	hexdump \
	httpd \
	ifdown \
	ifup \
	inotifyd \
	install \
	iostat \
	ipaddr \
	ipcalc \
	iplink \
	iproute \
	iprule \
	iptunnel \
	klogd \
	linuxrc \
	loadkmap \
	losetup \
	lpd \
	lpq \
	lpr \
	lsattr \
	lspci \
	lsusb \
	lzcat \
	lzma \
	lzop \
	lzopcat \
	makedevs \
	makemime \
	man \
	md5sum \
	mesg \
	mkfifo \
	mknod \
	mkswap \
	mktemp \
	modinfo \
	modprobe \
	more \
	mpstat \
	nbd-client \
	nc \
	nice \
	nmeter \
	nohup \
	od \
	openvt \
	patch \
	pidof \
	pipe_progress \
	pmap \
	popmaildir \
	poweroff \
	printf \
	pscan \
	pstree \
	pwd \
	pwdx \
	raidautorun \
	rdev \
	readlink \
	readprofile \
	realpath \
	reformime \
	reset \
	resize \
	rev \
	rpm \
	rpm2cpio \
	rtcwake \
	run-parts \
	runsv \
	runsvdir \
	rx \
	script \
	scriptreplay \
	sed \
	sendmail \
	seq \
	setkeycodes \
	setlogcons \
	setserial \
	setsid \
	setuidgid \
	sha1sum \
	sha256sum \
	sha512sum \
	showkey \
	smemcap \
	softlimit \
	sort \
	split \
	start-stop-daemon \
	strings \
	stty \
	sum \
	sv \
	svlogd \
	sysctl \
	tac \
	tail \
	tar \
	taskset \
	tcpsvd \
	tee \
	telnet \
	telnetd \
	test \
	time \
	timeout \
	tr \
	traceroute \
	true \
	ttysize \
	tunctl \
	tune2fs \
	udhcpc \
	uname \
	uncompress \
	unexpand \
	uniq \
	unix2dos \
	unlzma \
	unlzop \
	unxz \
	unzip \
	usleep \
	uudecode \
	uuencode \
	vi \
	volname \
	watch \
	wc \
	wget \
	which \
	whoami \
	whois \
	xargs \
	xz \
	xzcat \
	yes \
	zcat

SYMLINKS := $(addprefix $(TARGET_OUT)/bin/,$(BUSYBOX_TOOLS))
$(SYMLINKS): BUSYBOX_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(BB_PATH)/Android.mk
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf "$@"
	$(hide) ln -sf $(BUSYBOX_BINARY) "$@"

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
	$(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
endif
