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
 * houserelays.c - Main loop of the houserelays program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "echttp_static.h"
#include "houseportalclient.h"

#include "houserelays.h"
#include "houserelays_config.h"
#include "houserelays_gpio.h"

static int use_houseportal = 0;

static void hc_help (const char *argv0) {

    int i = 1;
    const char *help;

    printf ("%s [-h] [-debug] [-test]%s\n", argv0, echttp_help(0));

    printf ("\nGeneral options:\n");
    printf ("   -h:              print this help.\n");

    printf ("\nHTTP options:\n");
    help = echttp_help(i=1);
    while (help) {
        printf ("   %s\n", help);
        help = echttp_help(++i);
    }
    exit (0);
}

static void relays_status_one (char *buffer, int size, int point,
                               const char *prefix, const char *suffix) {

    time_t pulsed = houserelays_gpio_deadline(point);
    const char *name = houserelays_gpio_name(point);
    const char *commanded = houserelays_gpio_commanded(point)?"on":"off";

    if (pulsed) {
        int remains = (int) (pulsed - time(0));
        snprintf (buffer, size,
                  "%s\"%s\":{\"state\":%d,\"command\":\"%s\",\"pulse\":%d}%s", prefix,
                  name, houserelays_gpio_get(point), commanded, remains, suffix);
    } else {
        snprintf (buffer, size,
                  "%s\"%s\":{\"state\":%d,\"command\":\"%s\"}%s", prefix,
                  name, houserelays_gpio_get(point), commanded, suffix);
    }
}

static const char *relays_status (const char *method, const char *uri,
                                   const char *data, int length) {
    static char buffer[65537];
    int count = houserelays_gpio_count();
    int i;
    int cursor;
    const char *prefix = "";

    snprintf (buffer, sizeof(buffer), "{\"status\":{");
    cursor = strlen(buffer);

    for (i = 0; i < count; ++i) {
        relays_status_one
            (buffer+cursor, sizeof(buffer)-cursor, i, prefix, "");
        prefix = ",";
        cursor += strlen(buffer+cursor);
    }

    snprintf (buffer+cursor, sizeof(buffer)-cursor, "}}");

    echttp_content_type_json ();
    return buffer;
}

static const char *relays_set (const char *method, const char *uri,
                                const char *data, int length) {
    static char buffer[65537];
    const char *point = echttp_parameter_get("point");
    const char *statep = echttp_parameter_get("state");
    const char *pulsep = echttp_parameter_get("pulse");
    int state;
    int pulse;
    int i;
    int count = houserelays_gpio_count();

    if (!point) {
        echttp_error (404, "missing point name");
        return "";
    }
    if (!statep) {
        echttp_error (400, "missing state value");
        return "";
    }
    if ((strcmp(statep, "on") == 0) || (strcmp(statep, "1") == 0)) {
        state = 1;
    } else if ((strcmp(statep, "off") == 0) || (strcmp(statep, "0") == 0)) {
        state = 0;
    } else {
        echttp_error (400, "invalid state value");
        return "";
    }

    state = atoi(statep) & 1;

    pulse = pulsep ? atoi(pulsep) : 0;
    if (pulse < 0) {
        echttp_error (400, "invalid pulse value");
        return "";
    }

    for (i = 0; i < count; ++i) {
       if (strcmp (point, houserelays_gpio_name(i)) == 0) {
           houserelays_gpio_set (i, state, pulse);
           relays_status_one
               (buffer, sizeof(buffer), i, "{\"status\":{", "}}");
       }
    }

    echttp_content_type_json ();
    return buffer;
}

static const char *relays_history (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[65537];

    buffer[0] = 0;
    echttp_content_type_json ();
    return buffer;
}

static const char *relays_config (const char *method, const char *uri,
                                   const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        echttp_transfer (houserelays_config_file(), houserelays_config_size());
        echttp_content_type_json ();
    } else if (strcmp ("POST", method) == 0) {
        // TBD.
    } else {
        echttp_error (400, "invalid state value");
    }
    return "";
}

static const char *relays_hardware (const char *method, const char *uri,
                                       const char *data, int length) {
    static char buffer[65537];

    buffer[0] = 0;
    echttp_content_type_json ();
    return buffer;
}

static void hc_background (int fd, int mode) {

    static time_t LastRenewal = 0;
    time_t now = time(0);

    if (use_houseportal) {
        static const char *path[] = {"/relays"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    houserelays_gpio_periodic();
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        use_houseportal = 1;
    }
    error = houserelays_config_load (argc, argv);
    if (error) {
        fprintf (stderr, "Cannot load configuration: %s\n", error);
        exit(1);
    }
    error = houserelays_gpio_configure (argc, argv);
    if (error) {
        fprintf (stderr, "Cannot initialize GPIO: %s\n", error);
        exit(1);
    }

    echttp_route_uri ("/relays/status", relays_status);
    echttp_route_uri ("/relays/set", relays_set);
    echttp_route_uri ("/relays/history", relays_history);

    echttp_route_match ("/relays/config", relays_config);

    echttp_static_route ("/relays", "/usr/share/house/public/relays");
    echttp_background (&hc_background);
    echttp_loop();
}

