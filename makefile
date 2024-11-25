BUILD_DIR = ./out
ENTRY_POINT = 0xc0001500

AS = nasm
CC = gcc
LD = ld


ASLIB = -I boot/include/
ASFLAGS = -f elf32

CLIB = -I lib/ -I lib/kernel/ -I kernel/include -I device/include -I thread/include
CFLAGS = -Wall -fno-builtin -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
		 $(CLIB) -c -W -m32 -g

LDFLAGS = -Ttext $(ENTRY_POINT) -m elf_i386 -e main

OBJS =	$(BUILD_DIR)/main.o $(BUILD_DIR)/print.o $(BUILD_DIR)/interrupt.o $(BUILD_DIR)/init.o \
		$(BUILD_DIR)/kernel.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/debug.o $(BUILD_DIR)/string.o \
		$(BUILD_DIR)/bitmap.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o


# ================================================
# ===== Kernel =====
$(BUILD_DIR)/main.o: kernel/main.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c
	$(CC) $(CFLAGS) $< -o $@

# ===== Device =====
$(BUILD_DIR)/timer.o: device/timer.c
	$(CC) $(CFLAGS) $< -o $@

# ====== Lib =======
$(BUILD_DIR)/string.o: lib/string.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c
	$(CC) $(CFLAGS) $< -o $@

# ===== Thread =====
$(BUILD_DIR)/thread.o: thread/thread.c
	$(CC) $(CFLAGS) $< -o $@


# ================================================
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@


# ================================================
$(BUILD_DIR)/mbr.bin: boot/mbr.S
	$(AS) $(ASLIB) $< -o $@

$(BUILD_DIR)/loader.bin: boot/loader.S
	$(AS) $(ASLIB) $< -o $@

$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@


# ================================================

.PHONY : mk_dir hd clean all

mk_dir:
	@echo
	if [ ! -d $(BUILD_DIR) ]; then mkdir $(BUILD_DIR); fi

hd:
	@echo
	dd if=$(BUILD_DIR)/mbr.bin of=hd60M.img bs=512 count=1 conv=notrunc && \
    dd if=$(BUILD_DIR)/loader.bin of=hd60M.img bs=512 count=4 seek=2 conv=notrunc && \
    dd if=$(BUILD_DIR)/kernel.bin of=hd60M.img bs=512 count=200 seek=9 conv=notrunc

clean:
	@echo
	rm -rf $(BUILD_DIR)/*
	@echo

build: $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.bin

all: mk_dir clean build hd
