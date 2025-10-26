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
 * int houserelays_gpio_search (const char *name);
 *
 *    Search for the index of the named point. Returns -1 if not found.
 *
 * int houserelays_gpio_count (void);
 *
 *    Return the number of configured relay points available.
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
 * void houserelays_gpio_status (ParserContext context, int root);
 *
 *    Populate the context with the list of known points and their values.
 *
 * void houserelays_gpio_changes (long long since,
 *                                ParserContext context, int root);
 *
 *    Populate the context with an history of the input changes that occurred
 *    after the provided millisecond timestamp. Point that have not changed
 *    are ommited. If since is 0, return the complete recent history.
 *
 * void houserelays_gpio_periodic (void);
 *
 *    This function must be called every second. It ends the expired pulses.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <gpiod.h>

#include "echttp_hash.h"
#include "echttp_json.h"

#include "houselog.h"
#include "houseconfig.h"

#include "houserelays.h"
#include "houserelays_gpio.h"

#define HOUSE_GPIO_MODE_INPUT  1
#define HOUSE_GPIO_MODE_OUTPUT 2

// Keep about 6 seconds worth of history, to allow processing periodic requests
// up to 5 seconds aparts with some margin.
//
#define HOUSE_GPIO_SEQUENCE_PERIOD 100  // Milliseconds.
#define HOUSE_GPIO_SEQUENCE_DEPTH   64

struct RelayMap {
    const char *name;
    const char *gear;
    const char *desc;
    unsigned int signature; // Accelerates search.
    int mode;
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

    char sequence[HOUSE_GPIO_SEQUENCE_DEPTH];
};

static struct RelayMap *Relays = 0;
static int RelaysCount = 0;

static int *InputIndex = 0;
static int InputCount = 0;

static long long RelayTimestamps[HOUSE_GPIO_SEQUENCE_DEPTH];
static int RelayLastScan = 0;

static struct gpiod_chip *RelayChip = 0;


const char *houserelays_gpio_configure (int argc, const char **argv) {
    return houserelays_gpio_refresh ();
}

static int houserelays_gpio_to_mode (const char *text) {

   // Default is output for compatibility with previous versions.
   if (!text) return HOUSE_GPIO_MODE_OUTPUT;
   if (!text[0]) return HOUSE_GPIO_MODE_OUTPUT;

   if (!strcmp (text, "out")) return HOUSE_GPIO_MODE_OUTPUT;
   if (!strcmp (text, "output")) return HOUSE_GPIO_MODE_OUTPUT;
   if (!strcmp (text, "in")) return HOUSE_GPIO_MODE_INPUT;
   if (!strcmp (text, "input")) return HOUSE_GPIO_MODE_INPUT;

   return HOUSE_GPIO_MODE_INPUT; // Safer, no short circuit.
}

static const char *houserelays_gpio_from_mode (int point) {
    if (point < 0 || point > RelaysCount) return 0;
    switch (Relays[point].mode) {
    case HOUSE_GPIO_MODE_OUTPUT: return "output";
    case HOUSE_GPIO_MODE_INPUT:  return "input";
    }
    return 0;
}

static int houserelays_gpio_to_sequence (long long timestamp) {
    return (int) ((timestamp / HOUSE_GPIO_SEQUENCE_PERIOD) % HOUSE_GPIO_SEQUENCE_DEPTH);
}

static void houserelays_gpio_scanner (int fd, int mode) {

    struct timeval now;
    gettimeofday (&now, 0);

    long long timestamp = (1000LL * now.tv_sec) + now.tv_usec / 1000;
    int index = houserelays_gpio_to_sequence (timestamp);

    int i;
    for (i = 0; i < InputCount; ++i) {
        int point = InputIndex[i];
        Relays[point].sequence[index] = (char) houserelays_gpio_get (point);
    }
    RelayTimestamps[index] = timestamp;
    RelayLastScan = index;
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
        Relays[i].signature = 0;
    }
    if (RelayChip) gpiod_chip_close (RelayChip);

    InputCount = 0;
    echttp_fastscan (0, 0);
    RelayLastScan = 0; // Do not keep data that relates to different points

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
    if (InputIndex) free(InputIndex);
    InputIndex = calloc (sizeof(int), RelaysCount);

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
            Relays[i].mode =
                houserelays_gpio_to_mode (houseconfig_string (point, ".mode"));
            Relays[i].desc = houseconfig_string (point, ".description");
            Relays[i].signature = echttp_hash_signature (Relays[i].name);
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

            if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT) {
                gpiod_line_settings_set_direction (settings, GPIOD_LINE_DIRECTION_OUTPUT);
                gpiod_line_settings_set_output_value (settings, Relays[i].off);
                gpiod_line_settings_set_drive (settings,
                        Relays[i].on?GPIOD_LINE_DRIVE_PUSH_PULL:GPIOD_LINE_DRIVE_OPEN_DRAIN);
            } else {
                gpiod_line_settings_set_direction (settings, GPIOD_LINE_DIRECTION_INPUT);
                gpiod_line_settings_set_edge_detection (settings, GPIOD_LINE_EDGE_NONE);
                InputIndex[InputCount++] = i;
            }

            const unsigned iopin = Relays[i].gpio;
            gpiod_line_config_add_line_settings (lineconfig, &iopin, 1, settings);

            gpiod_request_config_set_consumer (requestconfig, "HouseRelays");

            Relays[i].line = gpiod_chip_request_lines(RelayChip, requestconfig, lineconfig);
            if (!Relays[i].line) goto endv2init;

            if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT)
                gpiod_line_request_set_value(Relays[i].line, Relays[i].gpio, Relays[i].off);

endv2init:
            if (requestconfig) gpiod_request_config_free(requestconfig);
            if (lineconfig) gpiod_line_config_free(lineconfig);
            if (settings) gpiod_line_settings_free(settings);

#else // GPIOD 1 API
            Relays[i].line = gpiod_chip_get_line (RelayChip, Relays[i].gpio);
            if (!Relays[i].line) continue;
            if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT) {
                if (Relays[i].on) {
                    gpiod_line_request_output
                        (Relays[i].line, Relays[i].name, GPIOD_LINE_ACTIVE_STATE_HIGH);
                } else {
                    gpiod_line_request_output_flags
                        (Relays[i].line, Relays[i].name, GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN, GPIOD_LINE_ACTIVE_STATE_HIGH);
                }
                gpiod_line_set_value(Relays[i].line, Relays[i].off);
            } else {
                gpiod_line_request_input (Relays[i].line, Relays[i].name);
                InputIndex[InputCount++] = i;
            }
#endif
        }
    }

    if (InputCount > 0) {
        memset (RelayTimestamps, 0, sizeof(RelayTimestamps));
        echttp_fastscan (houserelays_gpio_scanner, HOUSE_GPIO_SEQUENCE_PERIOD);
    }
    return 0;
}

int houserelays_gpio_search (const char *name) {
    int i;
    unsigned int signature = echttp_hash_signature (name);
    for (i = 0; i < RelaysCount; ++i) {
       if (Relays[i].signature != signature) continue; // Faster than strcmp()
       if (!strcmp (name, Relays[i].name)) return i;
    }
    return -1;
}

int houserelays_gpio_count (void) {
    return RelaysCount;
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

    if (point < 0 || point > RelaysCount) return 0;
    if (!Relays[point].line) return 0;

    // Silently ignore control requests on points that are not output.
    // This is not considered as an error.
    if (Relays[point].mode != HOUSE_GPIO_MODE_OUTPUT) return 1;

    time_t now = time(0);
    const char *namedstate = state?"on":"off";
    char comment[256];

    if (echttp_isdebug()) {
        if (pulse)
            fprintf (stderr, "set %s to %s at %ld (pulse %ds)\n", Relays[point].name, namedstate, now, pulse);
        else
            fprintf (stderr, "set %s to %s at %ld\n", Relays[point].name, namedstate, now);
    }

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

static int houserelays_gpio_next (int index) {
    return (++index) % HOUSE_GPIO_SEQUENCE_DEPTH;
}

void houserelays_gpio_status (ParserContext context, int root) {

    int i;
    for (i = 0; i < RelaysCount; ++i) {
       const char *mode = houserelays_gpio_from_mode(i);
       const char *status = houserelays_gpio_get(i)?"on":"off";
       const char *commanded = Relays[i].commanded?"on":"off";

       int point = echttp_json_add_object (context, root, Relays[i].name);
       if (mode) echttp_json_add_string (context, point, "mode", mode);
       echttp_json_add_string (context, point, "state", status);
       if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT)
           echttp_json_add_string (context, point, "command", commanded);
       if (Relays[i].deadline) {
           echttp_json_add_integer
               (context, point, "pulse", Relays[i].deadline);
       }
       if (Relays[i].gear && (Relays[i].gear[0] != 0))
           echttp_json_add_string (context, point, "gear", Relays[i].gear);
    }
}

void houserelays_gpio_changes (long long since,
                               ParserContext context, int root) {

    if (RelayLastScan <= since) {
       // (If there are no input points, then RelayLastScan remains at 0.)
       return;
    }

    int start;
    int end = RelayLastScan;
    int origin = houserelays_gpio_to_sequence (since);

    if (RelayTimestamps[origin] != since) since = 0; // Force full report.

    if (since) {
       start = houserelays_gpio_next (origin);
    } else {
       start = end;
       do {
           start = houserelays_gpio_next (start);
       } while (RelayTimestamps[start] <= since);
    }

    echttp_json_add_integer (context, root, "start", RelayTimestamps[start]);
    echttp_json_add_integer (context, root, "step", HOUSE_GPIO_SEQUENCE_PERIOD);
    echttp_json_add_integer (context, root, "end",
                             RelayTimestamps[end]-RelayTimestamps[start]);

    int top = echttp_json_add_object (context, root, "data");
    int i;
    for (i = 0; i < InputCount; ++i) {
       int point = InputIndex[i];
       if (since) {
           int haschanged = 0;
           int j = start;
           int reference = Relays[point].sequence[origin];
           while (1) {
               if (Relays[point].sequence[j] != reference) {
                  haschanged = 1;
                  break;
               }
               if (j == end) break;
               j = houserelays_gpio_next (j);
           }
           if (!haschanged) continue; // Skip this point (no change).
       }

       int array = echttp_json_add_array (context, top, Relays[point].name);
       int j = start;
       while (1) {
           echttp_json_add_integer
               (context, array, 0, Relays[point].sequence[j]);
           if (j == end) break;
           j = houserelays_gpio_next (j);
       }
    }
}

void houserelays_gpio_periodic (time_t now) {

    int i;
    for (i = 0; i < RelaysCount; ++i) {
        if (Relays[i].mode != HOUSE_GPIO_MODE_OUTPUT) continue;
        if (Relays[i].deadline > 0 && now >= Relays[i].deadline) {
            houserelays_gpio_set (i, 1 - Relays[i].commanded, -1, 0);
        }
    }
}

