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
 * int houserelays_gpio_count (void);
 *
 *    Return the number of configured relay points available.
 *
 * const char *houserelays_gpio_name (int point);
 *
 *    Return the name of a relay point.
 *
 * int houserelays_gpio_status (int point);
 * time_t houserelays_gpio_deadline (int point);
 *
 *    Return the last commanded state, or the command deadline, for
 *    the specified relay point.
 *
 * int houserelays_gpio_set (int point, int state, int pulse);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0,
 *    the relay is maintained until a new state is applied.
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

#include "houserelays.h"
#include "houserelays_config.h"
#include "houserelays_gpio.h"

struct RelayMap {
    const char *name;
    int gpio;
    int on;
    int off;
    struct gpiod_line *line;
    int state;
    time_t deadline;
};

static struct RelayMap *Relays;
static int RelaysCount = 0;

static struct gpiod_chip *RelayChip;


const char *houserelays_gpio_configure (int argc, const char **argv) {

    int i;
    int chip = houserelays_config_integer (0, ".relays.iochip");

    int relays = houserelays_config_array (0, ".relays.points");
    if (relays < 0) return "cannot find points array";

    RelaysCount = houserelays_config_array_length (relays);
    if (RelaysCount <= 0) return "no point found";

    Relays = calloc(sizeof(struct RelayMap), RelaysCount);
    if (!Relays) return "no more memory";

    RelayChip = gpiod_chip_open_by_number(chip);

    for (i = 0; i < RelaysCount; ++i) {
        int point;
        char path[128];
        snprintf (path, sizeof(path), "[%d]", i);
        point = houserelays_config_object (relays, path);
        if (point > 0) {
            Relays[i].name = houserelays_config_string (point, ".name");
            Relays[i].gpio = houserelays_config_integer (point, ".gpio");
            Relays[i].on  = houserelays_config_integer (point, ".on") & 1;

            Relays[i].off = 1 - Relays[i].on;
            Relays[i].line = gpiod_chip_get_line (RelayChip, Relays[i].gpio);
            Relays[i].state = 0;
            Relays[i].deadline = 0;

            gpiod_line_request_output (Relays[i].line, Relays[i].name,
                                       Relays[i].on?GPIOD_LINE_ACTIVE_STATE_HIGH:GPIOD_LINE_ACTIVE_STATE_LOW);
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

int houserelays_gpio_status (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].state;
}

time_t houserelays_gpio_deadline (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    return Relays[point].deadline;
}

int houserelays_gpio_set (int point, int state, int pulse) {
    if (point < 0 || point > RelaysCount) return 0;
    gpiod_line_set_value(Relays[point].line,
                         state?Relays[point].on:Relays[point].off);
    if (pulse > 0)
        Relays[point].deadline = time(0) + pulse;
    else
        Relays[point].deadline = 0;
    Relays[point].state = state;
    return 1;
}

void houserelays_gpio_periodic (void) {
    int i;
    time_t now = time(0);

    for (i = 0; i < RelaysCount; ++i) {
        if (Relays[i].deadline > 0 && Relays[i].deadline < now) {
            houserelays_gpio_set (i, 1 - Relays[i].state, 0);
        }
    }
}
