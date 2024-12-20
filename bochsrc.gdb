# how much memory the emulated machine will have
megs: 32

# filename of ROM images
romimage:file=/usr/local/share/bochs/BIOS-bochs-latest  
vgaromimage:file=/usr/local/share/bochs/VGABIOS-lgpl-latest  

# what disk images will be used
floppya: 1_44=a.img, status=inserted

# hard disk
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
# !! Remember to change these if the hd img is changed:
#    1. include/sys/config.h::MINOR_BOOT
#    2. boot/include/load.inc::ROOT_BASE
#    3. Makefile::HD
#    4. commands/Makefile::HD
ata0-master: type=disk, path="hd60M.img", mode=flat, cylinders=121, heads=16, spt=63
ata0-slave: type=disk, path="hd80M.img", mode=flat, cylinders=162, heads=16, spt=63

# choose the boot disk.
boot: disk

# where do we send log messages?
log: bochsout.txt

# disable the mouse
mouse: enabled=0

# enable key mapping, using US layout as default.
keyboard: keymap=/usr/local/share/bochs/keymaps/x11-pc-us.map
gdbstub: enabled=1, port=1234, text_base=0, data_base=0, bss_base=0
