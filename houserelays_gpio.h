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
 */
const char *houserelays_gpio_configure (int argc, const char **argv);
const char *houserelays_gpio_refresh (void);

int houserelays_gpio_count (void);
const char *houserelays_gpio_name (int point);
const char *houserelays_gpio_description (int point);

int    houserelays_gpio_commanded (int point);
time_t houserelays_gpio_deadline  (int point);
int    houserelays_gpio_get       (int point);
int    houserelays_gpio_set       (int point, int state, int pulse);

void houserelays_gpio_periodic (time_t now);

