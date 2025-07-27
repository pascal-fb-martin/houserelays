# houserelays
A web server to control relays

## Overview

This is a web server designed to provide access to a relay board. This project depends on [echttp](https://github.com/pascal-fb-martin/echttp) and [houseportal](https://github.com/pascal-fb-martin/houseportal).

See the [gallery](https://github.com/pascal-fb-martin/houserelays/blob/master/gallery/README.md) for a view of HouseRelays's web UI.

The primary intent is to support a distributed network of relay boards, to avoid pulling electric wires from one side of the home to the other: a small set of relay boards is installed, each board located close to existing wiring or near the equipment to control. Each relay board is attached to a small computer (e.g. Raspberry Pi Zero W). The typical use is to control sprinklers, a garage door, etc. By using such a micro-service architecture, the application may run anywhere in the home and still access all the devices, regardless of their locations.

The secondary intent is to share a relay board between multiple independent applications: sprinkler system, garage door controller, etc. Each application control only those devices that it is configured for.

This way relay boards may be installed at convenient points across the home, and be accessed by separate applications independently of the relays or applications physical locations.

This program implements the House control web service and API.

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

The hardware interface is configured through the file /etc/house/relays.json. A typical exemple of configuration is:
```
{
    "relays" : {
        "iochip" : 0,
        "points" : [
            {
                "name" : "relay1",
                "gpio" : 4,
                "on" : 0,
                "gear" : "valve"
            },
            {
                "name" : "relay2",
                "gpio" : 17,
                "on" : 0,
                "gear" : "valve"
            }
        ]
    }
}
```
The iochip item must match the Linux gpiod chip index. The gpio item must match the gpiod line offset (this also matches the usual Raspberry Pi naming convention for I/O pin names, e.g. GPIO4, GPIO17).

If on is 0, the output is configured as open-drain, the on command sets the output to 0, and the off command sets the output to 1.

If on is 1, the output is configured as 3-state, the on command sets the output to 1, and the off command sets the output to 0.

The gear attribute is used by applications to filter which control points to show on their user interface. The typical values are valve (irrigation) and light.

## Web API

This program implements the House control web API.

The API supported by this server is designed to be as generic as possible. The goal is to reuse the same API for different classes of hardware in the future. Each relay is accessed by a name, which is independent of the actual wiring between the relay board and the computer.

It is recommended to use for each relay a name that represents the device connected to the relay and is unique across the network. Not only does this make the client independent of which relay is used, but will also allow the clients to discover which servers offer access to the devices they are controlling. (The default configuration provided does not follow this convention because it assumes that no application has been configured yet.)

The basic services included are:

* Send controls (with optional pulse timer).
* Get the current status of all devices, which also serves as a way to discover the list of devices present on this server.
* Get a recent history of the controls received.
* Get the current config.
* Post a new config.

```
GET /relays/status
```
Returns a status JSON object that lists each relay by name. Each relay is itself an object with state, command and gear elements and an optional pulse element. The state and command elements are either on or 1 (active), or else off or 0 (inactive). The pulse element is present if there is a pulse timer active (see below for more information about pulses) and indicates the remaining number of seconds during which the current state will be maintained.
```
GET /relays/set?point=NAME&state=off|0|on|1[&cause=TEXT]
GET /relays/set?point=NAME&state=off|0|on|1&pulse=N[&cause=TEXT]
```
Set the specified relay point to the specified state. If the pulse parameter is present the point is maintained for the specified number of seconds, then reverted (i.e. if state is 1 and pulse if 10, the relay is set active for 10 seconds then changed to inactive after 10 seconds). If the pulse parameter is not present or its value is 0, the specified state is maintained until the next set request is issued.

The point name "all" denotes all points served by this web server. Use with caution as the service maybe shared between multiple applications. It is intended for maintenance only.

The optional cause parameter is reflected in the event that records the control. This is a way to describe what caused the control to be issued, thus the name.
```
GET /relays/recent
```
Return a JSON array of all the recent relay state changes. The history is not saved to disk and the server keeps only a fixed number of state changes.
```
GET /relays/config
```
Return a JSON dump of the server's local configuration.
```
POST /relays/config
```
Upload and activate a new server configuration.

The server is also capable of serving static pages, location in /usr/share/house/public/relays. The URL of each page must start with /relays.

