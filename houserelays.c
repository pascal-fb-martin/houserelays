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

#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"

#include "houserelays.h"
#include "houserelays_history.h"
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

static const char *relays_status (const char *method, const char *uri,
                                   const char *data, int length) {
    static char buffer[65537];
    JsonToken token[1024];
    char pool[65537];
    int count = houserelays_gpio_count();
    int i;

    JsonContext context = echttp_json_start (token, 1024, pool, 65537);

    int root = echttp_json_add_object (context, 0, 0);
    int container = echttp_json_add_object (context, root, "status");

    for (i = 0; i < count; ++i) {
        time_t pulsed = houserelays_gpio_deadline(i);
        const char *name = houserelays_gpio_name(i);
        const char *commanded = houserelays_gpio_commanded(i)?"on":"off";

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_integer (context, point, "state", houserelays_gpio_get(i));
        echttp_json_add_string (context, point, "command", commanded);
        if (pulsed)
            echttp_json_add_integer (context, point, "pulse", (int)pulsed);
    }
    const char *error = echttp_json_format (context, buffer, 65537);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *relays_set (const char *method, const char *uri,
                                const char *data, int length) {

    const char *point = echttp_parameter_get("point");
    const char *statep = echttp_parameter_get("state");
    const char *pulsep = echttp_parameter_get("pulse");
    int state;
    int pulse;
    int i;
    int count = houserelays_gpio_count();
    int found = 0;

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

    pulse = pulsep ? atoi(pulsep) : 0;
    if (pulse < 0) {
        echttp_error (400, "invalid pulse value");
        return "";
    }

    for (i = 0; i < count; ++i) {
       if ((strcmp (point, "all") == 0) ||
           (strcmp (point, houserelays_gpio_name(i)) == 0)) {
           found = 1;
           houserelays_gpio_set (i, state, pulse);
       }
    }

    if (! found) {
        echttp_error (404, "invalid point name");
        return "";
    }
    return relays_status (method, uri, data, length);
}

static const char *relays_history (const char *method, const char *uri,
                                      const char *data, int length) {
    static char buffer[81920];
    static JsonToken token[8192];
    static char pool[81920];
    time_t timestamp;
    char *name;
    char *command;
    int pulse;
    int i = houserelays_history_first (&timestamp, &name, &command, &pulse);

    JsonContext context = echttp_json_start (token, 8192, pool, 81920);

    int root = echttp_json_add_object (context, 0, 0);
    int container = echttp_json_add_array (context, root, "history");

    while (i >= 0) {
        int event = echttp_json_add_object (context, container, 0);
        echttp_json_add_integer (context, event, "time", (long)timestamp);
        echttp_json_add_string (context, event, "point", name);
        echttp_json_add_string (context, event, "cmd", command);
        if (pulse) echttp_json_add_integer (context, event, "pulse", pulse);

        i = houserelays_history_next (i, &timestamp, &name, &command, &pulse);
    }
    const char *error = echttp_json_format (context, buffer, 81920);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *relays_config (const char *method, const char *uri,
                                   const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        echttp_transfer (houserelays_config_file(), houserelays_config_size());
        echttp_content_type_json ();
    } else if (strcmp ("POST", method) == 0) {
        const char *error = houserelays_config_update (data);
        if (error) echttp_error (400, error);
        houserelays_gpio_refresh ();
    } else {
        echttp_error (400, "invalid state value");
    }
    return "";
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

