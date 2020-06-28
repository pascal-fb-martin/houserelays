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
 */

const char *houserelays_config_load (int argc, const char **argv);

int houserelays_config_file (void);
int houserelays_config_size (void);

const char *houserelays_config_string  (int parent, const char *path);
int         houserelays_config_integer (int parent, const char *path);
double      houserelays_config_boolean (int parent, const char *path);

int houserelays_config_array        (int parent, const char *path);
int houserelays_config_array_length (int array);
int houserelays_config_object       (int parent, const char *path);
