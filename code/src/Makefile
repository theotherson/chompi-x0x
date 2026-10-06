# CHOMPI x0x: a TB-303 / x0xb0x-style firmware for the CHOMPI, built on TEMPO.
#
# Build with Arm GNU Embedded Toolchain 10.3-2021.10, the one CHOMPI Club
# recommends for custom firmware (TEMPO's prebuilt libraries were built with
# it too). Build the libraries once first, see README.md.
# Output: build/CHOMPI.bin

TARGET = CHOMPI

DEBUG = 0
OPT = -O3

# Runs from SRAM, loaded there by the CHOMPI bootloader.
APP_TYPE = BOOT_SRAM
LDSCRIPT = chompi_sram.lds

# FreeRTOS coreJSON (for options.json, from milestone 6)
C_SOURCES = ../libs/coreJSON/source/core_json.c
C_INCLUDES += -I../libs/coreJSON/source/include

CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

USE_FATFS = 1

LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
