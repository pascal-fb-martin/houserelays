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
 * houserelays_config.c - Access the relays config.
 *
 * SYNOPSYS:
 *
 * const char *houserelays_config_load (int argc, const char **argv);
 *
 *    Load the configuration from the specified config option, or else
 *    from the default config file.
 *
 * int houserelays_config_file (void);
 * int houserelays_config_size (void);
 *
 *    Return a file descriptor (and the size) of the configuration file
 *    being used.
 *
 * const char *houserelays_config_update (const char *text);
 *
 *    Update both the live configuration and the configuration file with
 *    the provided text.
 *
 * const char *houserelays_config_string  (int parent, const char *path);
 * int         houserelays_config_integer (int parent, const char *path);
 * double      houserelays_config_boolean (int parent, const char *path);
 *
 *    Access individual items starting from the specified parent
 *    (the config root is index 0).
 *
 * int houserelays_config_array (int parent, const char *path);
 * int houserelays_config_array_length (int array);
 *
 *    Retrieve an array.
 * 
 * int houserelays_config_object (int parent, const char *path);
 *
 *    Retrieve an object.
 * 
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp_json.h>

#include "houselog.h"
#include "houserelays.h"
#include "houserelays_config.h"

#define CONFIGMAXSIZE 1024

static ParserToken ConfigParsed[CONFIGMAXSIZE];
static int   ConfigTokenCount = 0;
static char *ConfigText;
static int   ConfigTextSize = 0;
static int   ConfigTextLength = 0;

static const char *ConfigFile = "/etc/house/relays.json";

static const char *houserelays_config_refresh (const char *file) {

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = echttp_parser_load (file);
    ConfigTextLength = strlen(ConfigText);

    ConfigTokenCount = CONFIGMAXSIZE;
    return echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
}

const char *houserelays_config_load (int argc, const char **argv) {

    int i;

    for (i = 1; i < argc; ++i) {
        if (strncmp ("--config=", argv[i], 9) == 0) {
            ConfigFile = strdup(argv[i] + 9);
        }
    }
    houselog_event ("SYSTEM", "CONFIG", "LOAD", "FILE %s", ConfigFile);
    return houserelays_config_refresh (ConfigFile);
}

const char *houserelays_config_update (const char *text) {

    int fd;

    fd = open (ConfigFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd >= 0) {
        write (fd, text, strlen(text));
        close (fd);
        houselog_event ("SYSTEM", "CONFIG", "UPDATED", "FILE %s", ConfigFile);
    }
    return houserelays_config_refresh (ConfigFile);
}

int houserelays_config_file (void) {
    return open(ConfigFile, O_RDONLY);
}

int houserelays_config_size (void) {
    return ConfigTextLength;
}

int houserelays_config_find (int parent, const char *path, int type) {
    int i;
    if (parent < 0 || parent >= ConfigTokenCount) return -1;
    i = echttp_json_search(ConfigParsed+parent, path);
    if (i >= 0 && ConfigParsed[parent+i].type == type) return parent+i;
    return -1;
}

const char *houserelays_config_string (int parent, const char *path) {
    int i = houserelays_config_find(parent, path, PARSER_STRING);
    return (i >= 0) ? ConfigParsed[i].value.string : 0;
}

int houserelays_config_integer (int parent, const char *path) {
    int i = houserelays_config_find(parent, path, PARSER_INTEGER);
    return (i >= 0) ? ConfigParsed[i].value.integer : 0;
}

int houserelays_config_boolean (int parent, const char *path) {
    int i = houserelays_config_find(parent, path, PARSER_BOOL);
    return (i >= 0) ? ConfigParsed[i].value.bool : 0;
}

int houserelays_config_array (int parent, const char *path) {
    return houserelays_config_find(parent, path, PARSER_ARRAY);
}

int houserelays_config_array_length (int array) {
    if (array < 0
            || array >= ConfigTokenCount
            || ConfigParsed[array].type != PARSER_ARRAY) return 0;
    return ConfigParsed[array].length;
}

int houserelays_config_object (int parent, const char *path) {
    return houserelays_config_find(parent, path, PARSER_OBJECT);
}

