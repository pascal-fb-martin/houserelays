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

HAPP=houserelays
HROOT=/usr/local
SHARE=$(HROOT)/share/house

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
	gcc $(GPIODOPT) -c -Os -o $@ $<

houserelays: $(OBJS)
	gcc -Os -o houserelays $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-app:
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	chmod 755 $(HROOT)/bin /var/lib/house /etc/house
	rm -f $(HROOT)/bin/houserelays
	cp houserelays $(HROOT)/bin
	chown root:root $(HROOT)/bin/houserelays
	chmod 755 $(HROOT)/bin/houserelays
	mkdir -p $(SHARE)/public/relays
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/relays
	cp public/* $(SHARE)/public/relays
	chown root:root $(SHARE)/public/relays/*
	chmod 644 $(SHARE)/public/relays/*
	if [ ! -e /etc/house/relays.json ] ; then cp config.json /etc/house/relays.json ; fi
	chown root:root /etc/house/relays.json
	chmod 644 /etc/house/relays.json
	touch /etc/default/houserelays

uninstall-app:
	rm -rf $(SHARE)/public/relays
	rm -f $(HROOT)/bin/houserelays

purge-app:

purge-config:
	rm -rf /etc/house/relays.config /etc/default/houserelays

# System installation. ------------------------------------------

include $(SHARE)/install.mak

