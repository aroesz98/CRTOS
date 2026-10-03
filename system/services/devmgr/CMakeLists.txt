# devmgr: loads the kernel modules the device tree asks for, reports devices (uevents)
crtos_app(devmgr SOURCES devmgr.c STACK 8192 HEAP 32768)
