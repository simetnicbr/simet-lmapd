/*
 * This file is part of lmapd.
 *
 * lmapd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lmapd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lmapd. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LMAP_IO_H
#define LMAP_IO_H

#include "lmap.h"

enum lmap_io_engine {
    LMAP_IO_DEFAULT = 0,
    LMAP_IO_XML,
    LMAP_IO_JSON,
    LMAP_IO_MAX
};

/* change active engine, call at any point */
extern int lmap_io_set_engine(enum lmap_io_engine engine);

/* returns data about currently-in-use engine */
extern const char *lmap_io_engine_name(void);
extern const char *lmap_io_engine_ext(void);

/* dispatchers to currently-in-use engine */
extern int lmap_io_parse_config_path(struct lmap *lmap, const char *path);
extern int lmap_io_parse_state_file(struct lmap *lmap, const char *filename);
extern int lmap_io_parse_state_path(struct lmap *lmap, const char *path);
extern char *lmap_io_render_config(struct lmap *lmap);
extern char *lmap_io_render_state(struct lmap *lmap);
extern char *lmap_io_render_report(struct lmap *lmap);

#endif
