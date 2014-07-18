DEFINES += USING_QT DBG KERNEL RC_INVOKED \
	NDIS60=1 NDIS_SUPPORT_NDIS6 NTDDI_VERSION=0x06010000

INCLUDEPATH += \
	C:/WinDDK/7600.16385.1/inc/api \
	C:/WinDDK/7600.16385.1/inc/crt \
	C:/WinDDK/7600.16385.1/inc/ddk \
	C:/WinDDK/7600.16385.1/inc/wdf/kmdf/1.9

SOURCES += \
	../wfp_common.cpp \
	debug_print.c \
	hone.cpp \
	network_monitor.cpp \
	process_monitor.cpp \
	read_interface.cpp \
	queue_manager.cpp \
	system_id.cpp

OTHER_FILES += \
	SOURCES \
	DEVELOPMENT.txt \
	hone.rc

HEADERS += \
	../ioctls.h \
	../version.h \
	../version_info.h \
	../wfp_common.h \
	common.h \
	debug_print.h \
	hone.h \
	hone_info.h \
	llrb.h \
	llrb_clear.h \
	network_monitor.h \
	network_monitor_priv.h \
	process_monitor.h \
	process_monitor_priv.h \
	queue_manager.h \
	queue_manager_priv.h \
	read_interface.h \
	read_interface_priv.h \
	ring_buffer.h \
	system_id.h
