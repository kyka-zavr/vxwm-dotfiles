VERSION = 1.0

PREFIX  = /usr/local
MANDIR  = $(PREFIX)/share/man/man1

PKG_DEPS = xcomposite xfixes xdamage xrender xext x11 xproto libdrm

INCS = $(shell pkg-config --cflags $(PKG_DEPS))
LIBS = $(shell pkg-config --libs   $(PKG_DEPS)) -lm -pthread

CFLAGS += $(INCS)

all: vcompmgr

vcompmgr:
	$(CC) $(CFLAGS) -o $@ vcompmgr.c $(LIBS)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f vcompmgr $(DESTDIR)$(PREFIX)/bin/vcompmgr
	chmod 755 ${DESTDIR}${PREFIX}/bin/vcompmgr

clean:
	rm -f vcompmgr

.PHONY: all install clean
