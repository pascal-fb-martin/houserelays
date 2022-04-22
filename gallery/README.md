# HouseRelays' Gallery

This gallery is meant to give an idea of the HouseRelays web UI. Note that HouseRelays is mainly a behind-the-scene service, meant as a general purpose interface to a relay board connected to GPIO pins, and thus its UI is minimal.

The HouseRelays main web page acts as a sort of dashboard for the manual control of devices:

![HouseRelays Main Page](https://raw.githubusercontent.com/pascal-fb-martin/houserelays/master/gallery/main-page.png)

The top line is a navigation bar that gives access to the HouseRelays major pages.

The table lists all the known relays devices, and their status. The control button can be used to turn each relay on or off manually. This is mostly meant for testing--the HouseRelays service is typically used by other services to control sprinklers, lights or some other devices.

The event page shows a record of the major changes detected by the HouseRelays service. Its main purpose is to help troubleshoot issues with device configuration and control.

![HouseRelays Event Page](https://raw.githubusercontent.com/pascal-fb-martin/houserelays/master/gallery/event-page.png)

The configuration page is used to list the relays and identify their pin connection.

![HouseRelays Config Page](https://raw.githubusercontent.com/pascal-fb-martin/houserelays/master/gallery/config-page.png)

