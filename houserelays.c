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
#include <unistd.h>

#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "houselog.h"

#include "houserelays.h"
#include "houserelays_config.h"
#include "houserelays_gpio.h"

static int  UseHousePortal = 0;
static char HostName[256];

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
    ParserToken token[1024];
    char pool[65537];
    int count = houserelays_gpio_count();
    int i;

    ParserContext context = echttp_json_start (token, 1024, pool, 65537);

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", HostName);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long)time(0));
    int top = echttp_json_add_object (context, root, "control");
    int container = echttp_json_add_object (context, top, "status");

    for (i = 0; i < count; ++i) {
        time_t pulsed = houserelays_gpio_deadline(i);
        const char *name = houserelays_gpio_name(i);
        const char *status = houserelays_gpio_failure(i);
        if (!status) status = houserelays_gpio_get(i)?"on":"off";
        const char *commanded = houserelays_gpio_commanded(i)?"on":"off";
        const char *gear = houserelays_gpio_gear(i);

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_string (context, point, "state", status);
        echttp_json_add_string (context, point, "command", commanded);
        if (pulsed)
            echttp_json_add_integer (context, point, "pulse", (int)pulsed);
        if (gear && gear[0] != 0)
            echttp_json_add_string (context, point, "gear", gear);
    }
    const char *error = echttp_json_export (context, buffer, 65537);
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

    if (UseHousePortal) {
        static const char *path[] = {"control:/relays"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    houserelays_gpio_periodic(now);
    houselog_background (now);
}

static void relays_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    gethostname (HostName, sizeof(HostName));

    echttp_default ("-http-service=dynamic");

    echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        UseHousePortal = 1;
    }
    houselog_initialize ("relays", argc, argv);
    houselog_event ("SERVICE", "relays", "START", "ON %s", HostName);
    const char *error = houserelays_config_load (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot load configuration: %s\n", error);
    }
    error = houserelays_gpio_configure (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot configure GPIO: %s\n", error);
    }

    echttp_cors_allow_method("GET");
    echttp_protect (0, relays_protect);

    echttp_route_uri ("/relays/status", relays_status);
    echttp_route_uri ("/relays/set",    relays_set);

    echttp_route_uri ("/relays/config", relays_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&hc_background);
    echttp_loop();
}

