QT       -= core gui
TEMPLATE  = app
TARGET    = honeutil
CONFIG   += console
CONFIG   -= app_bundle
DEFINES  += NTDDI_VERSION=0x06010000 _MBCS
DEFINES  -= UNICODE

LIBS += -LC:/WinDDK/7600.16385.1/lib/win7/i386 \
	-lfwpuclnt \
	-liphlpapi \
	-luuid

SOURCES += \
	../wfp_common.cpp \
	common.cpp \
	filters.cpp \
	honeutil.cpp \
	oconn.cpp \
	read.cpp \
	stats.cpp

OTHER_FILES += \
	SOURCES \
	honeutil.rc \
	honeutil.manifest

HEADERS += \
	../ioctls.h \
	../version.h \
	../version_info.h \
	../wfp_common.h \
	common.h \
	filters.h \
	honeutil_info.h \
	oconn.h \
	read.h \
	stats.h
