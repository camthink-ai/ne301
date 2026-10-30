CROSS ?= arm-none-eabi-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
CFLAGS = -mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -Os -ffreestanding -fno-builtin -Wall -Werror -std=c11 -I../../../Custom/Common/Inc

all: apphost_test.bin

apphost_test.bin: apphost_payload.bin pack_image.py
	python3 pack_image.py testapp.elf apphost_payload.bin apphost_test.bin

apphost_payload.bin: main.c testapp.ld
	$(CC) $(CFLAGS) -nostdlib -T testapp.ld -o testapp.elf main.c
	$(OBJCOPY) -O binary testapp.elf apphost_payload.bin

clean:
	rm -f testapp.elf apphost_payload.bin apphost_test.bin
