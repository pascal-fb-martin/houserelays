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
 * const char *houserelays_gpio_initialize (int argc, const char **argv);
 *
 *    Retrieve the configuration and initialize access to the relays.
 *    Return 0 on success, and error message otherwise.
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
 * int houserelays_gpio_set (int point, int state, int pulse, const char *cause);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0,
 *    the relay is maintained until a new state is applied. The cause
 *    parameter, if not null, is used to populate the event.
 *
 *    Return 1 on success, 0 if the point is not known and -1 on error.
 *
 * void houserelays_gpio_update (void);
 *
 *    Force an update of all the GPIO status. This can be done periodically
 *    and/or before a request for the current status.
 *
 * void houserelays_gpio_status (ParserContext context, int root);
 *
 *    Populate the context with the list of known points and their values.
 *
 * void houserelays_gpio_fast (int period);
 *
 *    Enable fast scanning for a few seconds. The period is in millisecond
 *    and must be in the 10 to 999 range. Otherwise a period of 0 means
 *    keep using the existing sampling period.
 *
 *    This should be called periodically to maintain fast scanning active.
 *    This is typically called when the client asks for the change history.
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
 *
 * int houserelays_gpio_same (void);
 *
 *    Return true if something changed compare to the known state.
 *    See the HousePortal's housestate.c module for how this works.
 *
 * int houserelays_gpio_current (void);
 *
 *    Return an identifier of the current state of the GPIO.
 *    See the HousePortal's housestate.c module for how this works.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>
#include <gpiod.h>

#include "echttp.h"
#include "echttp_hash.h"
#include "echttp_json.h"

#include "houselog.h"
#include "housestate.h"
#include "houseconfig.h"

#include "houserelays.h"
#include "houserelays_gpio.h"
#include "houserelays_memory.h"

#define DEBUG if (echttp_isdebug()) printf

#define HOUSE_GPIO_MODE_INPUT  1
#define HOUSE_GPIO_MODE_OUTPUT 2

// Keep about 6 seconds worth of history, to allow processing periodic requests
// up to 5 seconds aparts with some margin.
//
#define HOUSE_GPIO_PERIOD_DEFAULT 100  // Milliseconds.
#define HOUSE_GPIO_PERIOD_MIN     10   // Milliseconds.
#define HOUSE_GPIO_SCAN_TIMEOUT 15   // Seconds.

struct RelayMap {
    const char *name;
    const char *gear;
    const char *desc;
    unsigned int signature; // Accelerates search.
    int mode;
    int gpio;
    int on;
    int failed; // Failure already detected, avoid logging the same error.
    int state;
    int commanded;
    time_t deadline;

    int history; // Index of this input point in the history.
};

static struct RelayMap *Relays = 0;
static int *RelayValues = 0;
static int RelayCount = 0;

static int *InputIndex = 0;
static unsigned int *InputOffset = 0;
static int InputCount = 0;

static int *OutputIndex = 0;
static unsigned int *OutputOffset = 0;
static int OutputCount = 0;

struct RelayIo {
    const char *name;
    struct gpiod_line_settings *settings;
    unsigned int *offsets;
    int count;
};

static struct gpiod_chip *RelayChip = 0;
static struct gpiod_line_request *RelayLine = 0;

static const char *DebugChip = 0;

static int       RelaySamplingPeriod = HOUSE_GPIO_PERIOD_DEFAULT;
static time_t    RelayFastScanEnabled = 0; // Fastscan is on a timer.

static int LiveGpioState = -1;

static void houserelays_gpio_setperiod (int period) {
    if ((period < 1000) && (period >= HOUSE_GPIO_PERIOD_MIN))
        RelaySamplingPeriod = period;
}

const char *houserelays_gpio_initialize (int argc, const char **argv) {

    int i;
    const char *value;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-chip=", argv[i], &DebugChip)) continue;
        if (echttp_option_match ("-period=", argv[i], &value)) {
            houserelays_gpio_setperiod (atoi (value));
            continue;
        }
    }
    LiveGpioState = housestate_declare ("live");

    if (houseconfig_active()) return houserelays_gpio_refresh ();
    return 0;
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

    if (point < 0 || point > RelayCount) return 0;
    switch (Relays[point].mode) {
    case HOUSE_GPIO_MODE_OUTPUT: return "output";
    case HOUSE_GPIO_MODE_INPUT:  return "input";
    }
    return 0;
}

static long long houserelays_gpio_timestamp (void) {

    struct timeval now;
    gettimeofday (&now, 0);
    return (1000LL * now.tv_sec) + now.tv_usec / 1000;
}

static int houserelays_gpio_store (int point, int state) {

    if (point < 0 || point > RelayCount) return 0;

    if (Relays[point].state == state) return 0;
    if (!Relays[point].name) return 0;

    DEBUG ("Point %s has new state %d\n", Relays[point].name, state);
    Relays[point].state = state;
    return 1;
}

static void houserelays_gpio_scanner (int fd, int mode) {

    if (InputCount <= 0) return; // Beter safe than sorry.

    int i;
    int changed = 0;
    long long timestamp = houserelays_gpio_timestamp ();

    if (gpiod_line_request_get_values_subset
             (RelayLine, InputCount, InputOffset, RelayValues)) {
        DEBUG ("gpiod_line_request_get_values_subset(scan) failed\n");
        return;
    }
    for (i = 0; i < InputCount; ++i) {
        int point = InputIndex[i];
        int state = RelayValues[i];
        if (houserelays_gpio_store (point, state)) {
            houserelays_memory_store (timestamp, Relays[point].history, state);
            changed = 1;
        }
    }
    if (changed) housestate_changed (LiveGpioState);
    houserelays_memory_done (timestamp);
}

void houserelays_gpio_fast (int period) {

    if (InputCount <= 0) return; // Nothing to enable anyway.

    if (period && RelayFastScanEnabled) {
        // If an explicit sampling period is requested while already
        // scanning, only accept smaller periods (faster). This is
        // because there could be multiple clients and we must adjust
        // for the fastest sampling speed requested.
        //
        int old = RelaySamplingPeriod;
        if (period < old) houserelays_gpio_setperiod (period);
        period = 0; // We are done with updating the period.

        // If the period has changed, reset the memory storage.
        if (old != RelaySamplingPeriod) RelayFastScanEnabled = 0;
    }
    if (!RelayFastScanEnabled) { // Protection against self reset.
        if (period) houserelays_gpio_setperiod (period);
        echttp_fastscan (houserelays_gpio_scanner, RelaySamplingPeriod);
        houserelays_memory_reset (InputCount, RelaySamplingPeriod);
        int i;
        for (i = 0; i < InputCount; ++i) {
            int point = InputIndex[i];
            Relays[point].history = houserelays_memory_add (Relays[point].name);
        }
    }
    RelayFastScanEnabled = time(0); // Keep fast scanning for now.
}

static void houserelays_gpio_slow (void) {

    if (RelayFastScanEnabled) {
        echttp_fastscan (0, 0);
        RelayFastScanEnabled = 0;
    }
}

static int houserelay_gpio_addpoint (struct RelayIo *io,
                                     int direction, int bias, int point) {

    if (io->count <= 0) {
        io->settings = gpiod_line_settings_new();
        gpiod_line_settings_set_direction (io->settings, direction);
        gpiod_line_settings_set_bias (io->settings, bias);
        gpiod_line_settings_set_active_low
            (io->settings, (bias == GPIOD_LINE_BIAS_PULL_UP));
        io->offsets = calloc (RelayCount, sizeof(unsigned int));
    }
    DEBUG ("adding point at offset %d to %s\n", point, io->name);
    io->offsets[io->count++] = point;
    return io->count == 1;
}

static int houserelay_gpio_apply (struct RelayIo *io,
                                  struct gpiod_line_config *line) {

    if (io->count > 0) {
        if (gpiod_line_config_add_line_settings
               (line, io->offsets, io->count, io->settings))
            DEBUG ("Cannot apply settings for %s\n", io->name);
    }
    return io->count;
}

static void houserelay_gpio_cleanup (struct RelayIo *io) {

    if (io->count > 0) {
        if (io->settings) gpiod_line_settings_free(io->settings);
        io->settings = 0; // Stay safe.
        if (io->offsets) free (io->offsets);
        io->offsets = 0; // Stay safe.
        io->count = 0;
    }
}

const char *houserelays_gpio_refresh (void) {

    if (RelayLine) {
       gpiod_line_request_release (RelayLine);
       RelayLine = 0;
    }

    int i;
    for (i = 0; i < RelayCount; ++i) {
        Relays[i].name = 0;
        Relays[i].signature = 0;
    }
    if (RelayChip) gpiod_chip_close (RelayChip);

    houserelays_gpio_slow (); // Will scan fast only on demand.

    char path[127];
    if (DebugChip) {
        snprintf (path, sizeof(path), "/dev/gpiochip%s", DebugChip);
    } else {
        int chip = houseconfig_integer (0, ".relays.iochip");
        snprintf (path, sizeof(path), "/dev/gpiochip%d", chip);
    }
    RelayChip = gpiod_chip_open(path);
    if (!RelayChip) return "cannot access GPIO";

    int relays = houseconfig_array (0, ".relays.points");
    if (relays < 0) return "cannot find points array";

    int oldcount = RelayCount;
    RelayCount = houseconfig_array_length (relays);
    if (RelayCount <= 0) return "no point found";
    DEBUG ("found %d points\n", RelayCount);

    if (RelayCount > oldcount) {
        if (Relays) free(Relays);
        Relays = calloc(RelayCount, sizeof(struct RelayMap));
        if (!Relays) return "no more memory";

        if (RelayValues) free(RelayValues);
        RelayValues = calloc (RelayCount, sizeof(int));
        if (!RelayValues) return "no more memory";

        if (InputIndex) free(InputIndex);
        InputIndex = calloc (RelayCount, sizeof(int));
        if (!InputIndex) return "no more memory";

        if (InputOffset) free(InputOffset);
        InputOffset = calloc (RelayCount, sizeof(int));
        if (!InputOffset) return "no more memory";

        if (OutputIndex) free(OutputIndex);
        OutputIndex = calloc (RelayCount, sizeof(int));
        if (!OutputIndex) return "no more memory";

        if (OutputOffset) free(OutputOffset);
        OutputOffset = calloc (RelayCount, sizeof(int));
        if (!OutputOffset) return "no more memory";
    }
    InputCount = 0;
    OutputCount = 0;

    int *list = calloc (RelayCount, sizeof(int));
    houseconfig_enumerate (relays, list, RelayCount);
    for (i = 0; i < RelayCount; ++i) {
        Relays[i].name = 0;
        Relays[i].signature = 0;
        int point = houseconfig_object (list[i], 0);
        if (point > 0) {
            Relays[i].name = houseconfig_string (point, ".name");
            if ((!Relays[i].name) || (!Relays[i].name[0])) continue;
            Relays[i].gear = houseconfig_string (point, ".gear");
            Relays[i].mode =
                houserelays_gpio_to_mode (houseconfig_string (point, ".mode"));
            Relays[i].desc = houseconfig_string (point, ".description");
            Relays[i].gpio = houseconfig_integer (point, ".gpio");
            Relays[i].on  = houseconfig_integer (point, ".on") & 1;
            DEBUG ("found point %s, gpio %d, on %d %s\n", Relays[i].name, Relays[i].gpio, Relays[i].on, Relays[i].desc);

            Relays[i].signature = echttp_hash_signature (Relays[i].name);
            Relays[i].state = 0;
            Relays[i].commanded = 0;
            Relays[i].deadline = 0;
            Relays[i].failed = 0;

            if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT) {
                OutputOffset[OutputCount] = Relays[i].gpio;
                OutputIndex[OutputCount++] = i;
            } else {
                InputOffset[InputCount] = Relays[i].gpio;
                InputIndex[InputCount++] = i;
            }
        }
    }

    // Now that the points configuration was retrieved,
    // initialize the access to the IO.
    //
    struct RelayIo inhigh = {"inputs active high", 0, 0, 0};
    struct RelayIo inlow = {"inputs active low", 0, 0, 0};
    struct RelayIo outhigh = {"outputs active high", 0, 0, 0};
    struct RelayIo outlow = {"outputs active low", 0, 0, 0};

    for (i = 0; i < RelayCount; ++i) {
        if (Relays[i].name == 0) continue;
        if (Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT) {
            if (Relays[i].on) {
                if (houserelay_gpio_addpoint (&outhigh,
                                              GPIOD_LINE_DIRECTION_OUTPUT,
                                              GPIOD_LINE_BIAS_DISABLED,
                                              Relays[i].gpio)) {
                    gpiod_line_settings_set_output_value
                              (outhigh.settings, GPIOD_LINE_VALUE_INACTIVE);
                    gpiod_line_settings_set_drive
                              (outhigh.settings, GPIOD_LINE_DRIVE_PUSH_PULL);
                }
            } else {
                if (houserelay_gpio_addpoint (&outlow,
                                              GPIOD_LINE_DIRECTION_OUTPUT,
                                              GPIOD_LINE_BIAS_PULL_UP,
                                              Relays[i].gpio)) {
                    gpiod_line_settings_set_output_value
                              (outlow.settings, GPIOD_LINE_VALUE_INACTIVE);
                    gpiod_line_settings_set_drive
                              (outlow.settings, GPIOD_LINE_DRIVE_OPEN_DRAIN);
                }
            }
        } else {
            if (Relays[i].on) {
                if (houserelay_gpio_addpoint (&inhigh,
                                              GPIOD_LINE_DIRECTION_INPUT,
                                              GPIOD_LINE_BIAS_DISABLED,
                                              Relays[i].gpio))
                    gpiod_line_settings_set_edge_detection
                              (inhigh.settings, GPIOD_LINE_EDGE_NONE);
            } else {
                if (houserelay_gpio_addpoint (&inlow,
                                              GPIOD_LINE_DIRECTION_INPUT,
                                              GPIOD_LINE_BIAS_PULL_UP,
                                              Relays[i].gpio))
                    gpiod_line_settings_set_edge_detection
                              (inlow.settings, GPIOD_LINE_EDGE_NONE);
            }
        }
    }

    int done = 0;
    struct gpiod_line_config *lineconfig = gpiod_line_config_new();
    done += houserelay_gpio_apply (&outhigh, lineconfig);
    done += houserelay_gpio_apply (&outlow,  lineconfig);
    done += houserelay_gpio_apply (&inhigh,  lineconfig);
    done += houserelay_gpio_apply (&inlow,   lineconfig);

    struct gpiod_request_config *requestconfig = gpiod_request_config_new();
    gpiod_request_config_set_consumer (requestconfig, "HouseRelays");

    if (done) {
        RelayLine = gpiod_chip_request_lines
                        (RelayChip, requestconfig, lineconfig);
        if (!RelayLine) {
            houselog_trace (HOUSE_FAILURE, "GPIO",
                            "gpiod_chip_request_lines() failed");
        }
    }

    // The list of controls changed: remove all references to the old names
    // and erase the existing history.
    houserelays_memory_reset (InputCount, RelaySamplingPeriod);

    houserelay_gpio_cleanup (&outhigh);
    houserelay_gpio_cleanup (&outlow);
    houserelay_gpio_cleanup (&inhigh);
    houserelay_gpio_cleanup (&inlow);
    gpiod_line_config_free (lineconfig);
    gpiod_request_config_free (requestconfig);
    free (list);

    return 0;
}

int houserelays_gpio_search (const char *name) {
    int i;
    unsigned int signature = echttp_hash_signature (name);
    for (i = 0; i < RelayCount; ++i) {
       if (Relays[i].signature != signature) continue; // Faster than strcmp()
       if (!Relays[i].name) continue;
       if (!strcmp (name, Relays[i].name)) return i;
    }
    return -1;
}

int houserelays_gpio_count (void) {
    return RelayCount;
}

int houserelays_gpio_set (int point, int state, int pulse, const char *cause) {

    if (point < 0 || point > RelayCount) return 0;
    if (!Relays[point].name) return 0;

    // Silently ignore control requests on points that are not output.
    // This is not considered as an error.
    if (Relays[point].mode != HOUSE_GPIO_MODE_OUTPUT) return 1;

    time_t now = time(0);
    const char *namedstate = state?"on":"off";

    if (echttp_isdebug()) {
        if (pulse)
            printf ("set %s to %s at %lld (pulse %ds)\n", Relays[point].name, namedstate, (long long)now, pulse);
        else
            printf ("set %s to %s at %lld\n", Relays[point].name, namedstate, (long long)now);
    }

    enum gpiod_line_value gpiod_state =
          state?GPIOD_LINE_VALUE_ACTIVE:GPIOD_LINE_VALUE_INACTIVE;
    DEBUG ("point %s set to libgpiod state %d\n", Relays[point].name, gpiod_state);
    if (gpiod_line_request_set_value
             (RelayLine, Relays[point].gpio, gpiod_state)) {
        DEBUG ("Setting %s to %d failed\n", Relays[point].name, gpiod_state);
        houselog_event ("GPIO",
                        Relays[point].name, namedstate, "CONTROL FAILED");
        return 0;
    }

    char comment[256];
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
    housestate_changed (LiveGpioState);
    return 1;
}

void houserelays_gpio_update (void) {

    int i;
    int changed = 0;

    if (OutputCount > 0) {
        // The output points must be read everytime because they are never
        // scanned at a high rate.
        if (gpiod_line_request_get_values_subset
                 (RelayLine, OutputCount, OutputOffset, RelayValues)) {
           DEBUG ("gpiod_line_request_get_values_subset(update) failed\n");
           return;
        }
        for (i = 0; i < OutputCount; ++i) {
            changed |= houserelays_gpio_store(OutputIndex[i], RelayValues[i]);
        }
    }

    if (!RelayFastScanEnabled && (InputCount > 0)) {
       // Must read input points now since there is no high speed scan.
       if (gpiod_line_request_get_values_subset
                (RelayLine, InputCount, InputOffset, RelayValues)) {
          DEBUG ("gpiod_line_request_get_values_subset(update) failed\n");
          if (changed) housestate_changed (LiveGpioState);
          return;
       }
       for (i = 0; i < InputCount; ++i) {
          changed |= houserelays_gpio_store (InputIndex[i], RelayValues[i]);
       }
    }
    if (changed) housestate_changed (LiveGpioState);
}

void houserelays_gpio_status (ParserContext context, int root) {

    int i;
    for (i = 0; i < RelayCount; ++i) {
       if (!Relays[i].name) continue;
       const char *mode = houserelays_gpio_from_mode(i);
       const char *status = Relays[i].state?"on":"off";
       const char *commanded = Relays[i].commanded?"on":"off";

       int point = echttp_json_add_object (context, root, Relays[i].name);
       if (mode) echttp_json_add_string (context, point, "mode", mode);
       echttp_json_add_string (context, point, "state", status);
       if ((Relays[i].mode == HOUSE_GPIO_MODE_OUTPUT) &&
           (strcmp (status, commanded)))
           echttp_json_add_string (context, point, "command", commanded);
       if (Relays[i].deadline) {
           echttp_json_add_integer
               (context, point, "pulse", Relays[i].deadline);
       }
       if (Relays[i].gear && (Relays[i].gear[0] != 0))
           echttp_json_add_string (context, point, "gear", Relays[i].gear);
    }
}

void houserelays_gpio_periodic (time_t now) {

    int i;
    for (i = 0; i < RelayCount; ++i) {
        if (!Relays[i].name) continue;
        if (Relays[i].mode != HOUSE_GPIO_MODE_OUTPUT) continue;
        if (Relays[i].deadline > 0 && now >= Relays[i].deadline) {
            houserelays_gpio_set (i, 1 - Relays[i].commanded, -1, 0);
        }
    }

    if (RelayFastScanEnabled) {
        // If there was no changes request for much more than the stored
        // history, disable fast scan: there is no active client anymore.
        time_t deadline = RelayFastScanEnabled + HOUSE_GPIO_SCAN_TIMEOUT;

        if (now > deadline) houserelays_gpio_slow ();
    }
}

int houserelays_gpio_same (void) {
    return housestate_same (LiveGpioState);
}

int houserelays_gpio_current (void) {
    return housestate_current (LiveGpioState);
}

