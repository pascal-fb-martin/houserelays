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
 * houserelays_memory.h - A mechanism to record GPIO changes of state.
 */
void houserelays_memory_reset (int count, int rate);
int  houserelays_memory_add (const char *name);
void houserelays_memory_store (long long timestamp, int index, int state);
void houserelays_memory_done  (long long timestamp);
void houserelays_memory_history (long long since,
                                 ParserContext context, int root);
void houserelays_memory_background (time_t now);

