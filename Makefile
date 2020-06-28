
OBJS= houserelays.o houserelays_config.o houserelays_gpio.o
LIBOJS=

all: houserelays

main: houserelays.o

clean:
	rm -f *.o *.a houserelays

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

houserelays: $(OBJS)
	gcc -g -O -o houserelays $(OBJS) -lhouseportal -lechttp -lcrypto -lgpiod -lrt

install:
	if [ -e /etc/init.d/houserelays ] ; then systemctl stop houserelays ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	rm -f /usr/local/bin/houserelays /etc/init.d/houserelays
	cp houserelays /usr/local/bin
	cp init.debian /etc/init.d/houserelays
	chown root:root /usr/local/bin/houserelays /etc/init.d/houserelays
	chmod 755 /usr/local/bin/houserelays /etc/init.d/houserelays
	touch /etc/default/houserelays
	mkdir -p /etc/house
	touch /etc/house/relays.json
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

