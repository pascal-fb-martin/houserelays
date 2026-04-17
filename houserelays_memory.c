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
 * houserelays_memory.c - A mechanism to record GPIO changes of state.
 *
 * This module manages the recording of GPIO changes of state, and make
 * them accessible to clients in JSON format. The caller is responsible
 * for reading the GPIO state and detecting changes.
 *
 * The history is a circular buffer of events, with two cursors: next
 * slot to use (always considered empty) and oldest filled slot.
 * The buffer is full when the next cursor refers to the entry before
 * the oldest: the buffer can contain up to one event less than its size.
 *
 * SYNOPSYS:
 *
 * void houserelays_memory_reset (int count, int rate);
 *
 *    Reset the whole storage. Count represents the (maximum) number of
 *    points to handle. Rate represents the sampling rate.
 *
 * int houserelays_memory_add (const char *name);
 *
 *    Add one more input point to add to the memory dictionary. This returns
 *    the index assigned to the input point. The lifetime of the name is
 *    controlled by the caller: it must last at least until the next reset.
 *
 * void houserelays_memory_store (long long timestamp, int index, int state);
 *
 *    Store one more state change event.
 *
 * void houserelays_memory_done (long long timestamp);
 *
 *    This must be called at the end of a scan, even if no change was detected,
 *    to set the end of the period that the current changes cover.
 *
 * void houserelays_memory_history (long long since,
 *                                  ParserContext context, int root);
 *
 *    Populate the context with an history of the input changes that occurred
 *    after the provided millisecond timestamp. If since is 0, return the
 *    complete recent history.
 *
 * void houserelays_memory_background (time_t now);
 *
 *    This function must be called every second.
 */

#include <stdlib.h>
#include <sys/time.h>

#include "echttp_json.h"

#include "houserelays_memory.h"

struct MemoryRecord {
    unsigned int   delay;
    unsigned int   point; // Bit 31: value, bit 30-0: index
};

#define MEMORY_DEPTH 1024
static struct MemoryRecord MemoryStore[MEMORY_DEPTH];

static int MemoryNext = 0;
static int MemoryOldest = 0;

static long long MemoryOldestTimestamp = 0; // Time of the oldest change.
static long long MemoryNewestTimestamp = 0; // Time of the newest change.
static long long MemoryScanTimestamp = 0;   // Time of the last scan.

static const char **MemoryDictionary = 0;
static int          MemoryDictionarySize = 0;
static int          MemoryDictionaryCount = 0;

static char *MemoryFlags = 0;
static int   MemorySamplingRate = 0;

void houserelays_memory_reset (int size, int rate) {

    if (size > MemoryDictionarySize) {
        if (MemoryDictionary) free (MemoryDictionary);
        MemoryDictionary = calloc (size, sizeof (char *));
        MemoryDictionarySize = size;

        if (MemoryFlags) free (MemoryFlags);
        MemoryFlags = malloc (size);
    }
    MemoryDictionaryCount = 0;
    MemoryNext = MemoryOldest = 0;
    MemoryNewestTimestamp = MemoryOldestTimestamp = 0;

    MemorySamplingRate = rate;
}

int houserelays_memory_add (const char *name) {

    if (MemoryDictionaryCount >= MemoryDictionarySize) return -1;
    int index = MemoryDictionaryCount++;
    MemoryDictionary[index] = name;
    return index;
}

static int houserelays_memory_next (int index) {
    if (++index >= MEMORY_DEPTH) index = 0;
    return index;
}

void houserelays_memory_store (long long timestamp, int index, int state) {

    if ((index < 0) || (index >= MemoryDictionaryCount)) return; // Invalid.

    int cursor = MemoryNext;

    if (MemoryOldestTimestamp == 0)
        MemoryOldestTimestamp = MemoryNewestTimestamp = timestamp;

    MemoryNext = houserelays_memory_next (MemoryNext);
    if (MemoryNext == MemoryOldest) {
       // Remove the oldest change and move on to the next
       MemoryOldestTimestamp += MemoryStore[MemoryOldest].delay;
       MemoryOldest = houserelays_memory_next (MemoryOldest);
    }

    MemoryStore[cursor].delay =
       (unsigned int) (timestamp - MemoryNewestTimestamp);
    MemoryStore[cursor].point = (unsigned int)index | (state?0x80000000u:0);
    MemoryNewestTimestamp = timestamp;
}

void houserelays_memory_done (long long timestamp) {
    MemoryScanTimestamp = timestamp;
}

void houserelays_memory_history (long long since,
                                 ParserContext context, int root) {

    if (since == 0) since = MemoryOldestTimestamp;

    echttp_json_add_integer (context, root, "start", since);
    echttp_json_add_integer (context, root, "step", MemorySamplingRate);
    echttp_json_add_integer (context, root, "end", MemoryScanTimestamp-since);

    // Attach the list of points, to interpret the index values provided
    // in the history below.
    int top = echttp_json_add_array (context, root, "names");
    int i;
    for (i = 0; i < MemoryDictionaryCount; ++i) {
        echttp_json_add_string (context, top, 0, MemoryDictionary[i]);
    }

    // List all the changes that occurred after "since"

    if (MemoryNewestTimestamp <= since) return; // No new data.

    long long start = MemoryOldestTimestamp;
    for (i = MemoryOldest; i != MemoryNext; i = houserelays_memory_next (i)) {
        long long changetime = start + MemoryStore[i].delay;
        if (changetime > since) break; // That change is more recent.
        start = changetime;
    }

    int adjust = 1; // Adjust the first delay.
    top = echttp_json_add_array (context, root, "data");
    for (; i != MemoryNext; i = houserelays_memory_next (i)) {
        int change = echttp_json_add_array (context, top, 0);
        int value = (MemoryStore[i].point & 0x80000000)?1:0;
        int index = MemoryStore[i].point & 0x7fffffff;
        long long adjusted = MemoryStore[i].delay;
        if (adjust) {
           adjusted += (start - since);
           adjust = 0;
        }
        echttp_json_add_integer (context, change, 0, adjusted);
        echttp_json_add_integer (context, change, 0, index);
        echttp_json_add_integer (context, change, 0, value);
    }
}

void houserelays_memory_background (time_t now) {

    // Remove everything if there was no new change for the last hour.
    //
    if (MemoryNewestTimestamp / 1000 < now - 3600) {
        MemoryNext = MemoryOldest = 0;
        MemoryNewestTimestamp = MemoryOldestTimestamp = 0;
    }
}

