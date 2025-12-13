# Zixiao Hypervisor - VirtIO Driver Makefile

CC := gcc
CFLAGS := -Wall -Wextra -O2 -fPIC -std=c11
LDFLAGS :=

# Source files
SRCS := virtio_ring.c
OBJS := $(SRCS:.c=.o)

# Output
LIB_STATIC := libvirtio.a
LIB_SHARED := libvirtio.so

.PHONY: all clean static shared

all: static shared

static: $(LIB_STATIC)

shared: $(LIB_SHARED)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

%.o: %.c virtio.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_STATIC) $(LIB_SHARED)

install: all
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/zixiao
	install -m 644 $(LIB_STATIC) $(DESTDIR)/usr/local/lib/
	install -m 755 $(LIB_SHARED) $(DESTDIR)/usr/local/lib/
	install -m 644 virtio.h $(DESTDIR)/usr/local/include/zixiao/
