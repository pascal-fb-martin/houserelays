# HouseRelays

A web server to control relays

## Overview

This is a web server designed to provide access to a relay board. This project depends on [echttp](https://github.com/pascal-fb-martin/echttp) and [houseportal](https://github.com/pascal-fb-martin/houseportal).

See the [gallery](https://github.com/pascal-fb-martin/houserelays/blob/master/gallery/README.md) for a view of HouseRelays's web UI.

The primary intent is to support a distributed network of relay boards, to avoid pulling electric wires from one side of the home to the other: a small set of relay boards is installed, each board located close to existing wiring or near the equipment to control. Each relay board is attached to a small computer (e.g. Raspberry Pi Zero W). The typical use is to control sprinklers, a garage door, etc. By using such a micro-service architecture, the application may run anywhere in the home and still access all the devices, regardless of their locations.

The secondary intent is to share a relay board between multiple independent applications: sprinkler system, garage door controller, etc. Each application controls only those devices that it is configured for. A `gear` configuration attribute allows an application to select which points it wants to control.

A third intent is to share access to digital inputs, e.g. reed relay status. Since the overhead of web polling is relatively high and the timing of HTTP requests is not really accurate, this server stores a limited history of input changes, which clients can access at a lower rate. The sampling rate of inputs is typically 100ms and about 6 seconds worth of the most recent changes is kept available. Since these are input points, multiple applications may access them simultanously.

This way relay boards may be installed at convenient points across the home, and be accessed by separate applications independently of the relays or applications physical locations.

## Installation.

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/relays.json (see below)

## Hardware

The typical hardware supported by this applications are relay boards controlled through 5V TTL digital pins. These are wired to the digital I/O pins of a Raspberry Pi, Odroid or other small Linux computers. Depending on the model, these board are either controlled using open-drain outputs (active low) or 3-state outputs (active high).

The hardware interface is configured through the HouseDepot file "<host>/relays.json", or through the local file /etc/house/relays.json if local storage is enabled.

> [!NOTE]
> The group name is not used for HouseDepot. This is because the relays interface is different for each computer: which configuration to load depends on the name of the computer that this service is running on.

A typical exemple of configuration is:

```
{
    "relays" : {
        "iochip" : 0,
        "points" : [
            {
                "name" : "relay1",
                "gpio" : 4,
                "mode" : "output",
                "on" : 0,
                "gear" : "valve",
                "connection" : 1,
                "description": "Relay 1"
            },
            {
                "name" : "relay2",
                "gpio" : 17,
                "mode" : "output",
                "on" : 0,
                "gear" : "valve",
                "connection" : 2,
                "description": "Relay 2"
            }
        ]
    }
}
```

The iochip item must match the Linux gpiod chip index. The gpio item must match the gpiod line offset (this also matches the usual Raspberry Pi naming convention for I/O pin names, e.g. GPIO4, GPIO17).

The mode can be `input` or `output`. If the item is missing, the mode defaults to `output`. All control requests that target an input point are ignored.

If on is 0, the output is configured as open-drain, the on command sets the output to 0, and the off command sets the output to 1.

If on is 1, the output is configured as 3-state, the on command sets the output to 1, and the off command sets the output to 0.

The `gear` attribute is used by applications to filter which control points to show on their user interface. The typical values are valve (irrigation) and light.

The connection and description items are informational. The connection item can be used to match the markings on the relays motherboard. The description item can be used to store any useful comment about this point's purpose or special properties.

## Web API

This program implements the [House control API](https://github.com/pascal-fb-martin/houseportal/blob/master/controlapi.md), including the sequence of changes extension.

The server is also capable of serving static pages, location in /usr/share/house/public/relays. The URL of each page must start with /relays.

## Testing with simulated GPIO

The Linux kernel supports declaring fake GPIO that can be controlled by test scripts. There are two such GPIO simulators: gpio-mockup and gpio-sim. Both work by declaring an additional GPIO chip. In order to simplify testing with such a simulator without tinkering with the configuration, HouseRelay supports a `--chip=N` command line option that superseeds the `relays.iochip` item in the configuration.

For example, if the gpio-mockup module was loaded and created device `/dev/gpiochip2`, then the following command forces HouseRelay to interface with the gpio-mockup GPIO pins:

```
houserelays --chip=2
```

GPIO pins 0 and 1 may not be accessible, as they might be already used depending on the system configuration. Use `gpioinfo` to check the status of the GPIO pins.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```

