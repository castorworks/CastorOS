# pwrbtn - 电源键驱动：按下就请 init 关机。PC 上是 ACPI 的电源键，arm64 上是接在 GPIO 上的按键
TARGET = pwrbtn
SOURCES = pwrbtn.cpp
ifeq ($(ARCH),arm64)
SOURCES += gpio_button.cpp
else
SOURCES += acpi_button.cpp
endif
include ../program.mk
