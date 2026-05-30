# RISC-V sodium runtime library
# Produces sodium-rt.a for linking with compiled Cyan programs

AS   = riscv64-elf-as
CC   = riscv64-elf-gcc
AR   = riscv64-elf-ar
ASFLAGS = -march=rv64gc -mno-relax
CFLAGS  = -ffreestanding -nostdlib -fno-builtin -O2 -c

SRCS = crt0.asm exit.asm print.asm read.asm
OBJS = $(SRCS:.asm=.o) allocator.o

all: sodium-rt.a

# C file
allocator.o: ../allocator.c
	$(CC) $(CFLAGS) -o $@ $<

# Assembly files
%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<

sodium-rt.a: $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -f *.o sodium-rt.a
