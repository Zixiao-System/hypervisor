# Zixiao Hypervisor - OVS-DPDK Network Library Makefile

CC := gcc
CFLAGS := -Wall -Wextra -O2 -fPIC -std=c11 -I./include
LDFLAGS :=

# Optional DPDK support (set DPDK_DIR to enable)
ifdef DPDK_DIR
    CFLAGS += -I$(DPDK_DIR)/include
    LDFLAGS += -L$(DPDK_DIR)/lib -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive
    LDFLAGS += -lpthread -lnuma -ldl
    CFLAGS += -DHAVE_DPDK
endif

# Directories
SRC_DIR := src
INC_DIR := include
OBJ_DIR := obj

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Output
LIB_STATIC := libovs_dpdk.a
LIB_SHARED := libovs_dpdk.so

.PHONY: all clean static shared install

all: $(OBJ_DIR) static shared

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

static: $(OBJ_DIR) $(LIB_STATIC)

shared: $(OBJ_DIR) $(LIB_SHARED)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(LIB_STATIC) $(LIB_SHARED)

install: all
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/zixiao
	install -m 644 $(LIB_STATIC) $(DESTDIR)/usr/local/lib/
	install -m 755 $(LIB_SHARED) $(DESTDIR)/usr/local/lib/
	install -m 644 $(INC_DIR)/*.h $(DESTDIR)/usr/local/include/zixiao/

# Dependencies
$(OBJ_DIR)/ovs_bridge.o: $(SRC_DIR)/ovs_bridge.c $(INC_DIR)/ovs_bridge.h
$(OBJ_DIR)/dpdk_port.o: $(SRC_DIR)/dpdk_port.c $(INC_DIR)/dpdk_port.h
$(OBJ_DIR)/vhost_user.o: $(SRC_DIR)/vhost_user.c $(INC_DIR)/dpdk_port.h $(INC_DIR)/ovs_bridge.h

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: all

# Test target (placeholder)
test: all
	@echo "Running tests..."
	@echo "Note: Requires OVS and optionally DPDK to be installed"

.PHONY: debug test
