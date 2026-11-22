# Sodium Runtime Library Makefile (x86-64)
# Produces sodium-rt.a for linking with compiled Cyan programs

NASM = nasm
CC   = gcc
AR   = ar
ASFLAGS  = -felf64
CFLAGS   = -ffreestanding -nostdlib -fno-builtin -O2 -c

TARGET = sodium-rt.a

X86_OBJS = x86_64/crt0.o x86_64/exit.o x86_64/allocator.o x86_64/printfmt.o x86_64/syscall_wrappers.o x86_64/syscalls.o x86_64/print.o x86_64/read.o x86_64/read_str.o

all: $(TARGET)

$(TARGET): $(X86_OBJS)
	$(AR) rcs $@ $^

# C files (explicit rules take priority over pattern rule below)
x86_64/allocator.o: allocator.c
	$(CC) $(CFLAGS) -o $@ $<
x86_64/printfmt.o: printfmt.c
	$(CC) $(CFLAGS) -o $@ $<
x86_64/syscall_wrappers.o: syscalls.c
	$(CC) $(CFLAGS) -o $@ $<

# Assembly files
x86_64/%.o: x86_64/%.asm
	$(NASM) $(ASFLAGS) -o $@ $<

.PHONY: clean

clean:
	rm -f x86_64/*.o $(TARGET)
