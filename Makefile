# houserelays - A simple home web server for world domination through relays
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=houserelays
HCAT=automation

EXTRADOC=/var/lib/house/note/extra

# Application build. --------------------------------------------

GPIODOPT=$(shell pkg-config --atleast-version=2 libgpiod 2> /dev/null && echo -DUSE_GPIOD2)

OBJS= houserelays.o houserelays_gpio.o
LIBOJS=

all: houserelays

main: houserelays.o

clean:
	rm -f *.o *.a houserelays

rebuild: clean all

%.o: %.c
	gcc $(GPIODOPT) -c -Wall -Os -o $@ $<

houserelays: $(OBJS)
	gcc -Os -o houserelays $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lmagic -lrt

# Distribution agnostic file installation -----------------------

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/relays
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/relays
	$(INSTALL) -m 0755 -d $(DESTDIR)$(EXTRADOC)/$(HPKG)/gallery
	$(INSTALL) -m 0644 gallery/* $(DESTDIR)$(EXTRADOC)/$(HPKG)/gallery

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s houserelays $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/houserelays

install-app: install-ui install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/relays
	rm -f $(DESTDIR)$(prefix)/bin/houserelays

purge-app:

purge-config:
	rm -f $(DESTDIR)/etc/house/relays.config
	rm -f $(DESTDIR)/etc/default/houserelays

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package: debian-package-generic

# System installation. ------------------------------------------

include $(SHARE)/install.mak

