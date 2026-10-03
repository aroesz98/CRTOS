# getty: the shell on the USB serial port (/dev/ttyACM0) once a terminal opens it on the computer
crtos_app(getty SOURCES getty.c STACK 8192 HEAP 16384)
