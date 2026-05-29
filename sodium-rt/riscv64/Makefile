# RISC-V sodium runtime library
# Assemble with: riscv64-linux-gnu-as -march=rv64gc -o file.o file.asm
# Archive with: riscv64-linux-gnu-ar rcs sodium-rt.a *.o

AS = riscv64-elf-as
AR = riscv64-elf-ar
CFLAGS = -march=rv64gc

SRCS = crt0.asm exit.asm malloc.asm print.asm read.asm
OBJS = $(SRCS:.asm=.o)

all: sodium-rt.a

%.o: %.asm
	$(AS) $(CFLAGS) -o $@ $<

sodium-rt.a: $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -f *.o sodium-rt.a
