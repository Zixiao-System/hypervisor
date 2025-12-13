# Zixiao Hypervisor - eBPF Network Acceleration Makefile

CC := gcc
CLANG := clang
LLC := llc

CFLAGS := -Wall -Wextra -O2 -fPIC -std=c11 -I./include
LDFLAGS := -lelf

# BPF compilation flags
BPF_CFLAGS := -O2 -target bpf -D__TARGET_ARCH_x86

# Directories
SRC_DIR := src
BPF_DIR := bpf
INC_DIR := include
OBJ_DIR := obj

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# BPF programs
BPF_SRCS := $(wildcard $(BPF_DIR)/*.bpf.c)
BPF_OBJS := $(BPF_SRCS:$(BPF_DIR)/%.bpf.c=$(OBJ_DIR)/%.bpf.o)

# Output
LIB_STATIC := libebpf_accel.a
LIB_SHARED := libebpf_accel.so

.PHONY: all clean static shared bpf

all: $(OBJ_DIR) static shared

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

static: $(OBJ_DIR) $(LIB_STATIC)

shared: $(OBJ_DIR) $(LIB_SHARED)

bpf: $(OBJ_DIR) $(BPF_OBJS)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.bpf.o: $(BPF_DIR)/%.bpf.c
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(LIB_STATIC) $(LIB_SHARED)

install: all
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/zixiao
	install -d $(DESTDIR)/usr/local/lib/bpf
	install -m 644 $(LIB_STATIC) $(DESTDIR)/usr/local/lib/
	install -m 755 $(LIB_SHARED) $(DESTDIR)/usr/local/lib/
	install -m 644 $(INC_DIR)/*.h $(DESTDIR)/usr/local/include/zixiao/
	-install -m 644 $(OBJ_DIR)/*.bpf.o $(DESTDIR)/usr/local/lib/bpf/
