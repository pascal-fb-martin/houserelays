
OBJS= houserelays.o houserelays_gpio.o
LIBOJS=

SHARE=/usr/local/share/house

# Local build ---------------------------------------------------

all: houserelays

main: houserelays.o

clean:
	rm -f *.o *.a houserelays

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

houserelays: $(OBJS)
	gcc -Os -o houserelays $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

# Distribution agnostic file installation -----------------------

install-files:
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/houserelays
	cp houserelays /usr/local/bin
	chown root:root /usr/local/bin/houserelays
	chmod 755 /usr/local/bin/houserelays
	mkdir -p $(SHARE)/public/relays
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/relays
	cp public/* $(SHARE)/public/relays
	chown root:root $(SHARE)/public/relays/*
	chmod 644 $(SHARE)/public/relays/*
	if [ ! -e /etc/house/relays.json ] ; then cp config.json /etc/house/relays.json ; fi
	chown root:root /etc/house/relays.json
	chmod 644 /etc/house/relays.json
	touch /etc/default/houserelays

uninstall-files:
	rm -rf $(SHARE)/public/relays
	rm -f /usr/local/bin/houserelays

purge-config:
	rm -rf /etc/house/relays.config /etc/default/houserelays

# Distribution agnostic systemd support -------------------------

install-systemd:

uninstall-systemd:
	if [ -e /etc/init.d/houserelays ] ; then systemctl stop houserelays ; systemctl disable houserelays ; rm -f /etc/init.d/houserelays ; fi
	if [ -e /lib/systemd/system/houserelays.service ] ; then systemctl stop houserelays ; systemctl disable houserelays ; rm -f /lib/systemd/system/houserelays.service ; systemctl daemon-reload ; fi

stop-systemd: uninstall-systemd

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-config

# Void Linux install --------------------------------------------

install-void: install-files

uninstall-void: uninstall-files

purge-void: uninstall-void purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

