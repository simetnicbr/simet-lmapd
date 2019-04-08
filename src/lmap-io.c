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

#include <unistd.h>

#include "lmap.h"
#include "lmap-io.h"

#ifdef WITH_XML
#include "xml-io.h"
#define DEFAULT_ENGINE LMAP_IO_XML
#endif

#ifdef WITH_JSON
#include "json-io.h"
#ifndef DEFAULT_ENGINE
#define DEFAULT_ENGINE LMAP_IO_JSON
#endif
#endif

#ifndef DEFAULT_ENGINE
#error Must define at least one IO engine!
#endif

typedef int (engine_parse_path_func)(struct lmap *lmap, const char *path);
typedef int (engine_render_func)(struct lmap *lmap);

static enum lmap_io_engine lmap_io_engine = DEFAULT_ENGINE;

/*
 * use direct calls and preprocessor instead of indirect function
 * tables, we only have two possibilities and this is easier for
 * debugging (and generates smaller code)
 */

int lmap_io_set_engine(enum lmap_io_engine engine)
{
    if (engine != LMAP_IO_DEFAULT
#ifdef WITH_XML
       && engine != LMAP_IO_XML
#endif
#ifdef WITH_JSON
       && engine != LMAP_IO_JSON
#endif
       )
	return -1;
    lmap_io_engine = engine;
    return 0;
}

const char *lmap_io_engine_name(void)
{
    const char *en[LMAP_IO_MAX] = {
	[LMAP_IO_XML] = "XML",
	[LMAP_IO_JSON] = "JSON"
    };

    if (lmap_io_engine >= 0 && lmap_io_engine < LMAP_IO_MAX && en[lmap_io_engine])
	return en[lmap_io_engine];

    return "";
}

const char *lmap_io_engine_ext(void)
{
    const char *en[LMAP_IO_MAX] = {
	[LMAP_IO_XML] = ".xml",
	[LMAP_IO_JSON] = ".json",
    };

    if (lmap_io_engine >= 0 && lmap_io_engine < LMAP_IO_MAX && en[lmap_io_engine])
	return en[lmap_io_engine];

    return "";
}

int lmap_io_parse_task_results_fd(int fd, int filetype, struct result *result)
{
#ifdef WITH_XML
    if (filetype == LMAP_FT_XML)
	return lmap_xml_parse_task_results_fd(fd, result);
#endif
#ifdef WITH_JSON
    if (filetype == LMAP_FT_JSON)
	return lmap_json_parse_task_results_fd(fd, result);
#endif
    return -1;
}

#ifdef WITH_XML
#define lmap_xml_dispatch(suffix, ...) \
    do { if (lmap_io_engine == LMAP_IO_XML) \
	return lmap_xml_ ## suffix ( __VA_ARGS__ ); \
    } while(0)
#else
#define lmap_xml_dispatch(...) do { } while(0)
#endif

#ifdef WITH_JSON
#define lmap_json_dispatch(suffix, ...) \
    do { if (lmap_io_engine == LMAP_IO_JSON) \
	return lmap_json_ ## suffix ( __VA_ARGS__ ); \
    } while(0)
#else
#define lmap_json_dispatch(...) do { } while(0)
#endif

#define CREATE_LMAP_IO_PARSE(suffix) \
    int lmap_io_parse_ ## suffix ( struct lmap *lmap, const char *s ) { \
        lmap_xml_dispatch(parse_ ## suffix, lmap, s); \
        lmap_json_dispatch(parse_ ## suffix, lmap, s); \
        return -1; \
    }

#define CREATE_LMAP_IO_RENDER(suffix) \
    char * lmap_io_render_ ## suffix ( struct lmap *lmap ) { \
        lmap_xml_dispatch(render_ ## suffix, lmap ); \
        lmap_json_dispatch(render_ ## suffix, lmap ); \
        return NULL; \
    }

/* function generators */
CREATE_LMAP_IO_PARSE(config_path)
CREATE_LMAP_IO_PARSE(state_path)
CREATE_LMAP_IO_PARSE(state_file)
CREATE_LMAP_IO_RENDER(config)
CREATE_LMAP_IO_RENDER(state)
CREATE_LMAP_IO_RENDER(report)
