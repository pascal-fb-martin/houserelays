# houserelays
A web server to control relays

# Overview

This web server is designed to provide web access to a relay board.

The primary intent is to support a distributed network of relay boards, to avoid pulling electric wires from one side of the home to the other: a small set of relay boards is installed, each board located close to existing wiring or near the equipment to control. Each relay board is attached to a small computer (e.g. Raspberry Pi Zero W). By using such a micro-service architecture, the sprinkler controler software may run anywhere in the home and still access all the devices, regardless of their locations.

The secondary intent is to share a relay board between multiple independent applications: sprinkler system, garage door controller, etc. Each application control only those devices that it is configured for.

This way relay boards may be installed at convenient points in the home, and be accessed by separate applications independently of the relays or applications physical locations.

# Hardware

The typical hardware supported by this applications are the relay board controlled by 5V TTL signals, typically connected to the digital output of a Raspberry Pi, Odroid or other small Linux computers.

# Web API

The API supported by this server is designed to be as generic as possible. The goal is to reuse the same API for different classes of hardware in the future. Each relay is accessed by a name, which is independent of the actual wiring between the relay board and the computer. It is recommended to use a name that represents the device connected to the relay. Not only this makes the client independent of which relay is used, but by keeping the names are unique across the network this will also allow the client to discover which server offer access to the device

The basic services included are:
* Send controls (with optional pulse timer).
* Get the current status of all devices, which also serves as a way to discover the list of devices present on this server.
* Get a recent history of the controls received.
* Get the current config.
* Post a new config.

```
GET /relays/status
```
Returns a status JSON object that lists each relay by name. Each relay is itself an object with a state element and an optional pulse element. The state is either on or 1 (active), or else off or 0 (inactive). The pulse element is present if there is a pulse timer active (see below for more information about pulses) and indicates the remaining number of seconds during which the current statei will be maintained.
```
GET /relays/set?point=NAME&state=off|0|on|1
GET /relays/set?point=NAME&state=off|0|on|1&pulse=N
```
Set the specified relay point to the specified state. If the pulse parameter is present the point is maintained for the specified number of seconds, then reverted (i.e. if state is 1 and pulse if 10, the relay is set active for 10 seconds then changed to inactive after 10 seconds). If the pulse parameter is not present or its value is 0, the specified state is maintained until the next set request is issued.

The point name "all" denotes all points served by this web server. Use with caution if the service is shared between applications. It is more meant for maintenance.
```
GET /relays/history
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

