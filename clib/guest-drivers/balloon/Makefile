# Zixiao Hypervisor - Balloon Driver Makefile

CC := gcc
CFLAGS := -Wall -Wextra -O2 -fPIC -std=c11
LDFLAGS :=

# Source files
SRCS := balloon.c
OBJS := $(SRCS:.c=.o)

# Output
LIB_STATIC := libballoon.a
LIB_SHARED := libballoon.so

.PHONY: all clean static shared

all: static shared

static: $(LIB_STATIC)

shared: $(LIB_SHARED)

$(LIB_STATIC): $(OBJS)
	ar rcs $@ $^

$(LIB_SHARED): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

%.o: %.c balloon.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_STATIC) $(LIB_SHARED)

install: all
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/zixiao
	install -m 644 $(LIB_STATIC) $(DESTDIR)/usr/local/lib/
	install -m 755 $(LIB_SHARED) $(DESTDIR)/usr/local/lib/
	install -m 644 balloon.h $(DESTDIR)/usr/local/include/zixiao/
