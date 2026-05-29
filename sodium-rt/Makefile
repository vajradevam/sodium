# Sodium Runtime Library Makefile
# Produces sodium-rt.a for linking with compiled Cyan programs

NASM = nasm
AR = ar
ASFLAGS = -felf64

TARGET = sodium-rt.a

X86_OBJS = x86_64/crt0.o x86_64/exit.o x86_64/malloc.o x86_64/print.o x86_64/read.o

all: $(TARGET)

$(TARGET): $(X86_OBJS)
	$(AR) rcs $@ $^

x86_64/%.o: x86_64/%.asm
	$(NASM) $(ASFLAGS) -o $@ $<

.PHONY: clean

clean:
	rm -f x86_64/*.o $(TARGET)
