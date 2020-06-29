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
 * houserelays_history.c - Keep a log of executed commands.
 *
 * SYNOPSYS:
 *
 * void houserelays_history_add (const char *name,
 *                               const char * command, int pulse);
 *
 *    Add one more record to the history.
 *
 * int houserelays_history_first (time_t *timestamp,
 *                                char **name, char **command, int *pulse);
 * int houserelays_history_next  (int cursor, time_t *timestamp,
 *                                char **name, char **command, int *pulse);
 *
 *    Retrieve the historical records, starting with the oldest.
 */

#include <string.h>
#include <stdlib.h>
#include <gpiod.h>

#include "houserelays.h"
#include "houserelays_history.h"

#define RELAY_LOG_NAMESIZE 32
#define RELAY_LOG_CMDSIZE   8

struct RelayHistory {
    time_t timestamp;
    char name[RELAY_LOG_NAMESIZE];
    char command[RELAY_LOG_CMDSIZE];
    int  pulse;
};

#define RELAY_LOG_DEPTH 1024

static struct RelayHistory RelaysLog[RELAY_LOG_DEPTH];
static int RelaysLogCursor = 0;

void houserelays_history_add (const char *name,
                              const char * command, int pulse) {

    RelaysLog[RelaysLogCursor].timestamp = time(0);
    strncpy (RelaysLog[RelaysLogCursor].name, name, RELAY_LOG_NAMESIZE);
    RelaysLog[RelaysLogCursor].name[RELAY_LOG_NAMESIZE-1] = 0;
    strncpy (RelaysLog[RelaysLogCursor].command, command, RELAY_LOG_CMDSIZE);
    RelaysLog[RelaysLogCursor].command[RELAY_LOG_CMDSIZE-1] = 0;
    RelaysLog[RelaysLogCursor].pulse = pulse;

    if (++RelaysLogCursor >= RELAY_LOG_DEPTH) RelaysLogCursor = 0;
    RelaysLog[RelaysLogCursor].timestamp = 0;
}

int houserelays_history_first (time_t *timestamp,
                               char **name, char **command, int *pulse) {
    return houserelays_history_next (RelaysLogCursor,
                                     timestamp, name, command, pulse);
}

int houserelays_history_next  (int cursor, time_t *timestamp,
                               char **name, char **command, int *pulse) {

    int i;

    for (i = cursor + 1; i != RelaysLogCursor; ++i) {
        if (i >= RELAY_LOG_DEPTH) i = 0;
        if (RelaysLog[i].timestamp) {
            *timestamp = RelaysLog[i].timestamp;
            *name = RelaysLog[i].name;
            *command = RelaysLog[i].command;
            *pulse = RelaysLog[i].pulse;
            return i;
        }
    }
    return -1;
}
