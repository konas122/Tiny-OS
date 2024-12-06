BUILD_DIR = ./out
ENTRY_POINT = 0xc0001500

AS = nasm
CC = gcc
LD = ld


ASLIB = -I boot/include/
ASFLAGS = -f elf32

CLIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/include -I device/include \
       -I thread/include -I user/include
CFLAGS = -Wall -fno-builtin -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
		 $(CLIB) -c -W -m32 -g

LDFLAGS = -Ttext $(ENTRY_POINT) -m elf_i386 -e main -Map $(BUILD_DIR)/kernel.map

OBJS =	$(BUILD_DIR)/main.o $(BUILD_DIR)/print.o $(BUILD_DIR)/interrupt.o $(BUILD_DIR)/init.o \
		$(BUILD_DIR)/kernel.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/debug.o $(BUILD_DIR)/string.o \
		$(BUILD_DIR)/bitmap.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
		$(BUILD_DIR)/switch.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o \
		$(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o $(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o \
		$(BUILD_DIR)/syscall_init.o $(BUILD_DIR)/stdio.o $(BUILD_DIR)/stdio_kernel.o $(BUILD_DIR)/ide.o \


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

$(BUILD_DIR)/console.o: device/console.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/keyboard.o: device/keyboard.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ioqueue.o: device/ioqueue.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ide.o: device/ide.c
	$(CC) $(CFLAGS) $< -o $@


# ====== Lib =======
$(BUILD_DIR)/string.o: lib/string.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio_kernel.o: lib/kernel/stdio_kernel.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c
	$(CC) $(CFLAGS) $< -o $@


# ===== Thread =====
$(BUILD_DIR)/thread.o: thread/thread.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: thread/sync.c
	$(CC) $(CFLAGS) $< -o $@

# ====== User ======
$(BUILD_DIR)/tss.o: user/tss.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: user/process.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall_init.o: user/syscall_init.c
	$(CC) $(CFLAGS) $< -o $@


# ================================================
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: thread/switch.S
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
