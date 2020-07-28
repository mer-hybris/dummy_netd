OBJS = dummy_netd.o

DESTDIR ?= /

CFLAGS = -O3 -Wall -Werror -flto
CFLAGS += `pkg-config --cflags libgbinder`
CFLAGS += `pkg-config --cflags libsystemd`
CFLAGS += `pkg-config --cflags glib-2.0`

LIBS += `pkg-config --libs libgbinder`
LIBS += `pkg-config --libs libsystemd`
LIBS += `pkg-config --libs glib-2.0`

dummy_netd: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

install:
	mkdir -p $(DESTDIR)/usr/sbin
	cp dummy_netd $(DESTDIR)/usr/sbin

clean:
	$(RM) $(OBJS) dummy_netd

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

all: dummy_netd

