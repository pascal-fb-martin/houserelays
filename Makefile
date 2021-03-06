
OBJS= houserelays.o houserelays_config.o houserelays_gpio.o
LIBOJS=

SHARE=/usr/local/share/house

all: houserelays

main: houserelays.o

clean:
	rm -f *.o *.a houserelays

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

houserelays: $(OBJS)
	gcc -g -O -o houserelays $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lgpiod -lrt

install:
	if [ -e /etc/init.d/houserelays ] ; then systemctl stop houserelays ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f /usr/local/bin/houserelays /etc/init.d/houserelays
	cp houserelays /usr/local/bin
	cp init.debian /etc/init.d/houserelays
	chown root:root /usr/local/bin/houserelays /etc/init.d/houserelays
	chmod 755 /usr/local/bin/houserelays /etc/init.d/houserelays
	mkdir -p $(SHARE)/public/relays
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/relays
	cp public/* $(SHARE)/public/relays
	chown root:root $(SHARE)/public/relays/*
	chmod 644 $(SHARE)/public/relays/*
	if [ ! -e /etc/house/relays.json ] ; then cp config.json /etc/house/relays.json ; fi
	chown root:root /etc/house/relays.json
	chmod 755 /etc/house/relays.json
	touch /etc/default/houserelays
	systemctl daemon-reload
	systemctl enable houserelays
	systemctl start houserelays

uninstall:
	systemctl stop houserelays
	systemctl disable houserelays
	rm -f /usr/local/bin/houserelays /etc/init.d/houserelays
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/house/relays.config /etc/default/houserelays

