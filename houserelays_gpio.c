/* houserelays - A simple home web server for world domination through relays
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * houserelays_gpio.c - Access a GPIO mapped relay board.
 *
 * SYNOPSYS:
 *
 * const char *houserelays_gpio_configure (int argc, const char **argv);
 *
 *    Retrieve the configuration and initialize access to the relays.
 *    Return the number of configured relay points available.
 *
 * const char *houserelays_gpio_refresh (void);
 *
 *    Re-evaluate the GPIO setup after the configuration changed.
 *
 * int houserelays_gpio_count (void);
 *
 *    Return the number of configured relay points available.
 *
 * const char *houserelays_gpio_name (int point);
 *
 *    Return the name of a relay point. The point name serves as an
 *    identifier for application access.
 *
 * const char *houserelays_gpio_failure (int point);
 *
 *    Return a strig describing the failure, or a null pointer if healthy.
 *
 * const char *houserelays_gpio_description (int point);
 * const char *houserelays_gpio_gear (int point);
 *
 *    Return the point's attributes. This is just text intended to help
 *    the user remember what the point is, for example it may match labels
 *    on the hardware. Most equipment labels each relay 1, 2, 3, etc. The
 *    description would match this. The application should not assume that
 *    the description text follow some specific semantic or syntax.
 *
 * int houserelays_gpio_commanded (int point);
 * time_t houserelays_gpio_deadline (int point);
 *
 *    Return the last commanded state, or the command deadline, for
 *    the specified relay point.
 *
 * int houserelays_gpio_get (int point);
 *
 *    Get the actual state of the point.
 *
 * int houserelays_gpio_set (int point, int state, int pulse, const char *cause);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0,
 *    the relay is maintained until a new state is applied. The cause
 *    parameter, if not null, is used to populate the event.
 *
 *    Return 1 on success, 0 if the point is not known and -1 on error.
 *
 * void houserelays_gpio_periodic (void);
 *
 *    This function must be called every second. It ends the expired pulses.
 */

#include <string.h>
#include <stdlib.h>
#include <gpiod.h>

#include "houselog.h"
#include "houseconfig.h"

#include "houserelays.h"
#include "houserelays_gpio.h"

struct RelayMap {
    const char *name;
    const char *gear;
    const char *desc;
    int gpio;
    int on;
    int off;
#ifdef USE_GPIOD2
    struct gpiod_line_request *line;
#else
    struct gpiod_line *line;
#endif
    int commanded;
    time_t deadline;
};

static struct RelayMap *Relays = 0;
static int RelaysCount = 0;

static struct gpiod_chip *RelayChip = 0;


const char *houserelays_gpio_configure (int argc, const char **argv) {
    return houserelays_gpio_refresh ();
}

const char *houserelays_gpio_refresh (void) {

    int i;
    for (i = 0; i < RelaysCount; ++i) {
        if (!Relays[i].line) continue;
#ifdef USE_GPIOD2
        gpiod_line_request_release (Relays[i].line);
#else
        gpiod_line_release (Relays[i].line);
#endif
        Relays[i].name = 0;
    }
    if (RelayChip) gpiod_chip_close (RelayChip);

    int chip = houseconfig_integer (0, ".relays.iochip");
    char path[127];
    snprintf (path, sizeof(path), "/dev/gpiochip%d", chip);

    int relays = houseconfig_array (0, ".relays.points");
    if (relays < 0) return "cannot find points array";

    RelaysCount = houseconfig_array_length (relays);
    if (RelaysCount <= 0) return "no point found";
    if (echttp_isdebug()) fprintf (stderr, "found %d points\n", RelaysCount);

    if (Relays) free(Relays);
    Relays = calloc(sizeof(struct RelayMap), RelaysCount);
    if (!Relays) return "no more memory";

    RelayChip = gpiod_chip_open(path);
    if (!RelayChip) return "cannot access GPIO";

    for (i = 0; i < RelaysCount; ++i) {
        int point;
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        point = houseconfig_object (relays, path);
        if (point > 0) {
            Relays[i].name = houseconfig_string (point, ".name");
            Relays[i].gear = houseconfig_string (point, ".gear");
            Relays[i].desc = houseconfig_string (point, ".description");
            Relays[i].gpio = houseconfig_integer (point, ".gpio");
            Relays[i].on  = houseconfig_integer (point, ".on") & 1;
            if (echttp_isdebug()) fprintf (stderr, "found point %s, gpio %d, on %d %s\n", Relays[i].name, Relays[i].gpio, Relays[i].on, Relays[i].desc);

            Relays[i].off = 1 - Relays[i].on;
            Relays[i].line = 0;
            Relays[i].commanded = 0;
            Relays[i].deadline = 0;

#ifdef USE_GPIOD2
            struct gpiod_line_settings *settings = gpiod_line_settings_new();
            struct gpiod_line_config *lineconfig = gpiod_line_config_new();
            struct gpiod_request_config *requestconfig = gpiod_request_config_new();
            if ((!settings) || (!lineconfig) || (!requestconfig))
                goto endv2init;

            gpiod_line_settings_set_direction (settings, GPIOD_LINE_DIRECTION_OUTPUT);
            gpiod_line_settings_set_output_value (settings, Relays[i].off);
            gpiod_line_settings_set_drive (settings,
                    Relays[i].on?GPIOD_LINE_DRIVE_PUSH_PULL:GPIOD_LINE_DRIVE_OPEN_DRAIN);

            const unsigned iopin = Relays[i].gpio;
            gpiod_line_config_add_line_settings (lineconfig, &iopin, 1, settings);

            gpiod_request_config_set_consumer (requestconfig, "HouseRelays");

            Relays[i].line = gpiod_chip_request_lines(RelayChip, requestconfig, lineconfig);
            if (!Relays[i].line) goto endv2init;

            gpiod_line_request_set_value(Relays[i].line, Relays[i].gpio, Relays[i].off);

endv2init:
            if (requestconfig) gpiod_request_config_free(requestconfig);
            if (lineconfig) gpiod_line_config_free(lineconfig);
            if (settings) gpiod_line_settings_free(settings);
#else
            Relays[i].line = gpiod_chip_get_line (RelayChip, Relays[i].gpio);
            if (!Relays[i].line) continue;
            if (Relays[i].on) {
                gpiod_line_request_output
                    (Relays[i].line, Relays[i].name, GPIOD_LINE_ACTIVE_STATE_HIGH);
            } else {
                gpiod_line_request_output_flags
                    (Relays[i].line, Relays[i].name, GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN, GPIOD_LINE_ACTIVE_STATE_HIGH);
            }
            gpiod_line_set_value(Relays[i].line, Relays[i].off);
#endif
        }
    }
    return 0;
}

int houserelays_gpio_count (void) {
    return RelaysCount;
}

const char *houserelays_gpio_name (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].name;
}

const char *houserelays_gpio_gear (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].gear;
}

const char *houserelays_gpio_description (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].desc;
}

const char *houserelays_gpio_failure (int point) {
    return 0; // A GPIO never fail, or never report it to us..
}

int houserelays_gpio_commanded (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].commanded;
}

time_t houserelays_gpio_deadline (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].deadline;
}

int houserelays_gpio_get (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    if (!Relays[point].line) return 0;
#ifdef USE_GPIOD2
    int state = gpiod_line_request_get_value (Relays[point].line, Relays[point].gpio);
#else
    int state = gpiod_line_get_value (Relays[point].line);
#endif
    return (state == Relays[point].on);
}

int houserelays_gpio_set (int point, int state, int pulse, const char *cause) {

    time_t now = time(0);
    const char *namedstate = state?"on":"off";
    char comment[256];

    if (echttp_isdebug()) {
        if (pulse)
            fprintf (stderr, "set %s to %s at %ld (pulse %ds)\n", Relays[point].name, namedstate, now, pulse);
        else
            fprintf (stderr, "set %s to %s at %ld\n", Relays[point].name, namedstate, now);
    }
    if (point < 0 || point > RelaysCount) return 0;
    if (!Relays[point].line) return 0;
#ifdef USE_GPIOD2
    gpiod_line_request_set_value(Relays[point].line, Relays[point].gpio,
                                 state?Relays[point].on:Relays[point].off);
#else
    gpiod_line_set_value(Relays[point].line,
                         state?Relays[point].on:Relays[point].off);
#endif
    if (cause)
        snprintf (comment, sizeof(comment), " (%s)", cause);
    else
        comment[0] = 0;
    if (pulse > 0) {
        Relays[point].deadline = time(0) + pulse;
        houselog_event ("GPIO", Relays[point].name, namedstate,
                        "FOR %d SECONDS%s", pulse, comment);
    } else if (pulse < 0) {
        Relays[point].deadline = 0;
        houselog_event ("GPIO", Relays[point].name, namedstate,
                        "END OF PULSE");
    } else {
        Relays[point].deadline = 0;
        houselog_event ("GPIO", Relays[point].name, namedstate,
                        "LATCHED%s", comment);
    }
    Relays[point].commanded = state;
    return 1;
}

void houserelays_gpio_periodic (time_t now) {

    int i;
    for (i = 0; i < RelaysCount; ++i) {
        if (Relays[i].deadline > 0 && now >= Relays[i].deadline) {
            houserelays_gpio_set (i, 1 - Relays[i].commanded, -1, 0);
        }
    }
}

