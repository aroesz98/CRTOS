# spi: one SPI message through /dev/spidev* (as Linux spidev_test)
crtos_app(spi SOURCES spi.c STACK 8192 HEAP 32768)
