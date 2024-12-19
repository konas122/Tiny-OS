BUILD_DIR = ./out
BUILD_LIB_DIR = $(BUILD_DIR)/lib
ENTRY_POINT = 0xc0001500

AR = ar
AS = nasm
CC = gcc
LD = ld


ARFLAGS = rcs

ASLIB = -I boot/include/
ASFLAGS = -f elf32

CLIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/include -I device/include \
       -I thread/include -I user/include -I fs/include -I shell/include

CFLAGS = -Wall -fno-builtin -Wstrict-prototypes -Wmissing-prototypes -fno-stack-protector \
		$(CLIB) -c -W -m32 -g

LDFLAGS = -Ttext $(ENTRY_POINT) -m elf_i386 -e main -Map $(BUILD_DIR)/kernel.map -z noexecstack

CLIB_OBJS = $(BUILD_LIB_DIR)/stdio.o $(BUILD_LIB_DIR)/string.o \
            $(BUILD_LIB_DIR)/start.o $(BUILD_LIB_DIR)/syscall.o

OBJS =	$(BUILD_DIR)/main.o $(BUILD_DIR)/print.o $(BUILD_DIR)/interrupt.o $(BUILD_DIR)/init.o \
		$(BUILD_DIR)/kernel.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/debug.o $(BUILD_DIR)/string.o \
		$(BUILD_DIR)/bitmap.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
		$(BUILD_DIR)/switch.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o \
		$(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o $(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o \
		$(BUILD_DIR)/syscall_init.o $(BUILD_DIR)/stdio.o $(BUILD_DIR)/stdio_kernel.o \
		$(BUILD_DIR)/ide.o $(BUILD_DIR)/fs.o $(BUILD_DIR)/dir.o $(BUILD_DIR)/file.o $(BUILD_DIR)/inode.o \
		$(BUILD_DIR)/fork.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/cmd.o $(BUILD_DIR)/assert.o \
		$(BUILD_DIR)/exec.o $(BUILD_DIR)/wait_exit.o

include defines.mk

# ================================================
# ===== Kernel =====
$(BUILD_DIR)/main.o: kernel/main.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/init.o: kernel/init.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/debug.o: kernel/debug.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/memory.o: kernel/memory.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

# ===== Device =====
$(BUILD_DIR)/timer.o: device/timer.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/console.o: device/console.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/keyboard.o: device/keyboard.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/ioqueue.o: device/ioqueue.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/ide.o: device/ide.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ===== Fs =====
$(BUILD_DIR)/fs.o: fs/fs.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/dir.o: fs/dir.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/file.o: fs/file.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/inode.o: fs/inode.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ====== Lib =======
$(BUILD_DIR)/string.o: lib/string.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/stdio.o: lib/stdio.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/list.o: lib/kernel/list.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/stdio_kernel.o: lib/kernel/stdio_kernel.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/assert.o: lib/user/assert.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ===== Thread =====
$(BUILD_DIR)/thread.o: thread/thread.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/sync.o: thread/sync.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ====== User ======
$(BUILD_DIR)/tss.o: user/tss.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/process.o: user/process.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/syscall_init.o: user/syscall_init.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/fork.o: user/fork.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/exec.o: user/exec.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/wait_exit.o: user/wait_exit.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ================================================

$(BUILD_DIR)/cmd.o: shell/cmd.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@

$(BUILD_DIR)/shell.o: shell/shell.c
	@$(CC) $(CFLAGS) $< -o $@
	@echo "    CC   " $@


# ================================================
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	@$(AS) $(ASFLAGS) $< -o $@
	@echo "    AS   " $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	@$(AS) $(ASFLAGS) $< -o $@
	@echo "    AS   " $@

$(BUILD_DIR)/switch.o: thread/switch.S
	@$(AS) $(ASFLAGS) $< -o $@
	@echo "    AS   " $@


# ================================================
$(BUILD_DIR)/mbr.bin: boot/mbr.S
	@$(AS) $(ASLIB) $< -o $@
	@echo "    AS   " $@

$(BUILD_DIR)/loader.bin: boot/loader.S
	@$(AS) $(ASLIB) $< -o $@
	@echo "    AS   " $@

$(BUILD_DIR)/kernel.bin: $(OBJS)
	@$(LD) $(LDFLAGS) $^ -o $@
	@echo "    LD   " $@


# ================================================
$(BUILD_LIB_DIR)/stdio.o: lib/stdio.c
	@$(CC) $(CFLAGS) -DNDEBUG $< -o $@
	@echo "    CC   " $@

$(BUILD_LIB_DIR)/string.o: lib/string.c
	@$(CC) $(CFLAGS) -DNDEBUG $< -o $@
	@echo "    CC   " $@

$(BUILD_LIB_DIR)/syscall.o: lib/user/syscall.c
	@$(CC) $(CFLAGS) -DNDEBUG $< -o $@
	@echo "    CC   " $@

$(BUILD_LIB_DIR)/start.o: shell/command/start.S
	@$(AS) $(ASFLAGS) $< -o $@
	@echo "    AS   " $@

$(BUILD_LIB_DIR)/clib.a: $(CLIB_OBJS)
	@$(AR) $(ARFLAGS) $@ $^
	@echo "    AR   " $@
	@echo


# ================================================

.PHONY : mk_dir hd clean lib all

mk_dir:
	@if [ ! -d $(BUILD_DIR) ]; then mkdir $(BUILD_DIR); fi
	@if [ ! -d $(BUILD_LIB_DIR) ]; then mkdir $(BUILD_LIB_DIR); fi

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

crt: $(BUILD_LIB_DIR)/clib.a

lib: mk_dir crt

all: clean mk_dir crt build hd
