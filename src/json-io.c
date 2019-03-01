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

/*
 * FIXME:
 *
 * 1. We do not enforce at parse time that non-optional fields are present.
 * 2. We do not check for trailing extraneous data after the JSON root
 *    object.
 * 3. We neither implement nor ignore report.parameters (parse)
 * 4. We neither implement nor ignore report.conflict   (parse)
 */

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <limits.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>

#include <json.h>

#include "lmap.h"
#include "utils.h"
#include "json-io.h"

#define JSON_READ_BUFFER_SZ     64000

#define RENDER_CONFIG_TRUE      0x01
#define RENDER_CONFIG_FALSE     0x02

#define PARSE_CONFIG_TRUE       0x01
#define PARSE_CONFIG_FALSE      0x02

#define YANG_CONFIG_TRUE        0x01
#define YANG_CONFIG_FALSE       0x02
#define YANG_KEY                0x04

/* portable, dangerous version of __attribute__((__unused__)) */
#define UNUSED(x) (void)(x)

/* callback function types */
typedef int (lmap_json_file_parse_func)(struct lmap *, const char *);
typedef int (lmap_parse_doc_func)(struct lmap *, json_object *);

/*
 * Process the parsed JSON data structure
 */

#define JSONHANDLEMAP_STRHDLR    0x1000 /* handler wants a string */
#define JSONHANDLEMAP_ARRAYITER  0x2000 /* handler wants obj iteration */

typedef int (lmap_jsonmap_strhdl_func)(void *, const char *);
typedef int (lmap_jsonmap_objhdl_func)(void *, json_object *, int);

struct lmap_jsonmap {
    char *name;          /* NULL for EOT */
    enum json_type type; /* type_null for any type */
    int flags;           /* JSONHANDLEMAP_*, YANG_* bitmask */
    /* main handler for this node */
    union {
      lmap_jsonmap_objhdl_func *jobj_handler;
      lmap_jsonmap_strhdl_func *str_handler;
    };
};

/* boilerplate to build common lmap_jsonmap entries */
#define JSONMAP_ENTRY_OBJANY_X(aname, aflag, afunc) \
    { .name = #aname, \
      .type = json_type_null, \
      .flags = aflag, \
      .jobj_handler = afunc \
    }

#define JSONMAP_ENTRY_OBJANY(aname, aflag, afprefix) \
    JSONMAP_ENTRY_OBJANY_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_STRING_X(akey, aflag, afunc) \
    { .name = #akey, \
      .type = json_type_string, \
      .flags = JSONHANDLEMAP_STRHDLR | aflag, \
      .str_handler = afunc \
    }

#define JSONMAP_ENTRY_STRING(aname, aflag, afprefix) \
    JSONMAP_ENTRY_STRING_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_STRARRAY_X(akey, aflag, afunc) \
    { .name = #akey, \
      .type = json_type_array, \
      .flags = JSONHANDLEMAP_ARRAYITER | JSONHANDLEMAP_STRHDLR | aflag, \
      .str_handler = afunc \
    }

#define JSONMAP_ENTRY_STRARRAY(aname, aflag, afprefix) \
    JSONMAP_ENTRY_STRARRAY_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_INT2STR_X(akey, aflag, afunc) \
    { .name = #akey, \
      .type = json_type_int, \
      .flags = JSONHANDLEMAP_STRHDLR | aflag, \
      .str_handler = afunc \
    }

#define JSONMAP_ENTRY_INT2STR(aname, aflag, afprefix) \
    JSONMAP_ENTRY_INT2STR_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_BOOL2STR_X(akey, aflag, afunc) \
    { .name = #akey, \
      .type = json_type_boolean, \
      .flags = JSONHANDLEMAP_STRHDLR | aflag, \
      .str_handler = afunc \
    }

#define JSONMAP_ENTRY_BOOL2STR(aname, aflag, afprefix) \
    JSONMAP_ENTRY_BOOL2STR_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_OBJARRAY_X(aname, aflag, afunc) \
    { .name = #aname, \
      .type = json_type_array, \
      .flags = JSONHANDLEMAP_ARRAYITER | aflag, \
      .jobj_handler = afunc \
    }

#define JSONMAP_ENTRY_OBJARRAY(aname, aflag, afprefix) \
    JSONMAP_ENTRY_OBJARRAY_X(aname, aflag, afprefix ## _ ## aname)

#define JSONMAP_ENTRY_OBJECT_X(aname, aflag, afunc) \
    { .name = #aname, \
      .type = json_type_object, \
      .flags = aflag, \
      .jobj_handler = afunc \
    }

#define JSONMAP_ENTRY_OBJECT(aname, aflag, afprefix) \
    JSONMAP_ENTRY_OBJECT_X(aname, aflag, afprefix ## _ ## aname)

/*
 * returns -1 on error (and reports it)
 *          0 on processed (silent) or none found (reports it)
 *
 * ctx_flags should be either zero, or a set of YANG_* flags for
 * PARSE_CONFIG_TRUE|FALSE-based filtering
 *
 * json_type_null acts as match-all for types (including NULL)
 */
static int
lookup_jsonmap(void *ctx, int ctx_flags,
		const char * const key,
		json_object * const obj,
		const struct lmap_jsonmap * table)
{
    json_object *jo;
    const char *jos;
    int res = 0;
    int i;

    assert(table);

    if (!key)
	return 0;
    if (!ctx)
	return -1;

    while (table->name) {
	if (!strcmp(key, table->name)) {
	    if (table->type != json_type_null && !json_object_is_type(obj, table->type)) {
		jo = obj;
		goto type_error;
	    }

	    /* Apply PARSE_CONFIG_* versus YANG_* filtering */
	    if (ctx_flags
		&& table->flags
		&& !(table->flags & YANG_KEY
		     || (ctx_flags & PARSE_CONFIG_TRUE  && table->flags & YANG_CONFIG_TRUE)
		     || (ctx_flags & PARSE_CONFIG_FALSE && table->flags & YANG_CONFIG_FALSE)))
		goto not_found;

	    if (json_object_get_type(obj) == json_type_array &&
		    table->flags & JSONHANDLEMAP_ARRAYITER) {
		if (table->jobj_handler) {
		    for (i = 0; !res && i < json_object_array_length(obj); i++) {
			jo = json_object_array_get_idx(obj, i);
			if (table->flags & JSONHANDLEMAP_STRHDLR) {
			    /* We might need a flag if we want to stringify JSON nodes */
			    if (!json_object_is_type(jo, json_type_string))
				goto type_error;
			    res = (* table->str_handler)(ctx, json_object_get_string(jo));
			} else {
			    /* jo might be NULL when handling [null] */
			    res = (* table->jobj_handler)(ctx, jo, ctx_flags);
			}
		    }
		}
	    } else if (table->flags & JSONHANDLEMAP_STRHDLR) {
		/* note that JSON-C can stringfy other types, and we use that! */
		if (table->str_handler) {
		    jos = json_object_get_string(obj);
		    res = (* table->str_handler)(ctx, (jos)? jos : "null");
		}
	    } else if (table->jobj_handler) {
		    res = (* table->jobj_handler)(ctx, obj, ctx_flags);
	    }

	    return res;
	}
	table++;
    }

not_found:
    lmap_wrn("unknown JSON field \"%s\" of type %s", key,
	    json_type_to_name(json_object_get_type(obj)));

    return 0; /* do not abort on unknown field */

type_error:
    lmap_err("expected a JSON %s for field \"%s\", found a JSON %s",
	    json_type_to_name(table->type), key,
	    json_type_to_name(json_object_get_type(jo)));

    return -1;
}

/* parse option list; common to report, config, state */

static int xx_los_id(void *p, const char *s)
{ struct option *o = p; return lmap_option_set_id(o, s); }

static int xx_los_name(void *p, const char *s)
{ struct option *o = p; return lmap_option_set_name(o, s); }

static int xx_los_value(void *p, const char *s)
{ struct option *o = p; return lmap_option_set_value(o, s); }

/* ctx is an array member of "option" or NULL */
/* what has the PARSE_CONFIG_FOO flags */
static struct option *
parse_option(json_object *ctx, int what)
{
    struct option *option;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(id,    YANG_CONFIG_TRUE, xx_los),
	JSONMAP_ENTRY_STRING(name,  YANG_CONFIG_TRUE, xx_los),
	JSONMAP_ENTRY_STRING(value, YANG_CONFIG_TRUE, xx_los),
	{ .name = NULL }
    };

    if (!ctx)
	return NULL;

    option = lmap_option_new();
    if (!option)
	return NULL;

    if (json_object_is_type(ctx, json_type_object)) {
	json_object_object_foreach(ctx, key, jo) {
	    res = lookup_jsonmap(option, what, key, jo, tab);
	    if (res)
		break;
	}
    }

    if (res) {
	lmap_wrn("invalid option in options array");
	lmap_option_free(option);
	option = NULL;
    }

    return option;
}

/*
 * state and config are mostly the same, state has a few runtime
 * state fields that are not present in config.
 */

/* FIXME: implement this */
static int
parse_control_doc(struct lmap *lmap, json_object *root, int flags)
{
    lmap_err("JSON parsing of config and state documents not implemented yet");
    return -1;
}

static int
parse_state_doc(struct lmap *lmap, json_object *root)
{
    return parse_control_doc(lmap, root, PARSE_CONFIG_TRUE | PARSE_CONFIG_FALSE);
}

static int
parse_config_doc(struct lmap *lmap, json_object *root)
{
    return parse_control_doc(lmap, root, PARSE_CONFIG_TRUE);
}

/*
 * report
 */

/* FIXME: unimplemented in lmapd */
static int
xx_lrst_function(void *p, json_object *ctx, int unused)
{ lmap_wrn("table function list not implemented yet"); return 0; }

/* FIXME: unimplemented in lmapd */
static int
xx_lrst_column(void *p, json_object *ctx, int unused)
{ lmap_wrn("table column list not implemented yet"); return 0; }

static int
parse_report_result_table_value(void *p, const char *s)
{
    struct row *row = p;
    struct value *row_value;
    int res = -1;

    row_value = lmap_value_new();
    if (!row_value)
	return -1;

    res = lmap_value_set_value(row_value, s);
    if (!res)
	res = lmap_row_add_value(row, row_value);

    if (res)
	lmap_value_free(row_value);

    return res;
}

static int
parse_report_result_table_row(void *p, json_object *ctx, int unused)
{
    struct table *restbl = p;
    struct row *row;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRARRAY_X(value, 0, parse_report_result_table_value),
	{ .name = NULL }
    };

    UNUSED(unused);

    if (!ctx)
	return 0; /* do not insert an empty row */

    row = lmap_row_new();
    if (!row)
	return -1;

    if (json_object_is_type(ctx, json_type_object)) {
	json_object_object_foreach(ctx, key, jo) {
	    res = lookup_jsonmap(row, 0, key, jo, tab);
	    if (res)
		break;
	}
    }

    if (!res)
	res = lmap_table_add_row(restbl, row);

    if (res) {
	lmap_wrn("invalid result table row");
	lmap_row_free(row);
    }

    return res;
}

/* ctx is an array member of "report.result.table" or NULL */
static int
parse_report_result_table(void *p, json_object *ctx, int unused)
{
    struct result *resctx = p;
    struct table *restbl = NULL;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_OBJARRAY(function, 0, xx_lrst),
	JSONMAP_ENTRY_OBJARRAY(column,   0, xx_lrst),
	JSONMAP_ENTRY_OBJARRAY_X(row, 0, parse_report_result_table_row)
    };

    UNUSED(unused);

    restbl = lmap_table_new();
    if (!restbl)
	return -1;

    if (json_object_is_type(ctx, json_type_object)) {
	json_object_object_foreach(ctx, key, jo) {
	    res = lookup_jsonmap(restbl, 0, key, jo, tab);
	    if (res)
		break;
	}
    }

    if (!res) {
	res = lmap_result_add_table(resctx, restbl);
    } else {
	lmap_wrn("incorrect report result table");
    }

    if (res)
	lmap_table_free(restbl);

    return res;
}

static int xx_lrs_schedule(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_schedule(r, s); }

static int xx_lrs_action(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_action(r, s); }

static int xx_lrs_task(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_task(r, s); }

static int xx_lrs_tag(void *p, const char *s)
{ struct result *r = p; return lmap_result_add_tag(r, s); }

static int xx_lrs_event(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_event(r, s); }

static int xx_lrs_start(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_start(r, s); }

static int xx_lrs_end(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_end(r, s); }

static int xx_lrs_cycle_number(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_cycle_number(r, s); }

static int xx_lrs_status(void *p, const char *s)
{ struct result *r = p; return lmap_result_set_status(r, s); }

/* ctx is an array member of "report.result.option" or NULL */
static int parse_report_result_option(void *p, json_object *ctx, int unused)
{
    struct result *r = p;
    struct option *lmapo;
    int res = -1;

    UNUSED(unused);

    lmapo = parse_option(ctx, PARSE_CONFIG_TRUE | PARSE_CONFIG_FALSE);
    if (lmapo)
	res = lmap_result_add_option(r, lmapo);

    return res;
}

/* ctx is an array member of "report.result" or NULL */
static int
parse_report_result(void *p, json_object *ctx, int unused)
{
    struct lmap *lmap = p;
    struct result *resctx;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(schedule, 0, xx_lrs),
	JSONMAP_ENTRY_STRING(action,   0, xx_lrs),
	JSONMAP_ENTRY_STRING(task,     0, xx_lrs),
	JSONMAP_ENTRY_STRING(event,    0, xx_lrs),
	JSONMAP_ENTRY_STRING(start,    0, xx_lrs),
	JSONMAP_ENTRY_STRING(end,      0, xx_lrs),
	JSONMAP_ENTRY_STRING_X(cycle-number, 0, xx_lrs_cycle_number),
	JSONMAP_ENTRY_INT2STR(status,  0, xx_lrs),
	JSONMAP_ENTRY_STRARRAY(tag,    0, xx_lrs),
	JSONMAP_ENTRY_OBJARRAY(option, 0, parse_report_result),
	JSONMAP_ENTRY_OBJARRAY(table,  0, parse_report_result),
	{ .name = NULL }
    };

    UNUSED(unused);

    if (!ctx)
	return 0; /* empty result table... */

    resctx = lmap_result_new();
    if (!resctx)
	return -1;

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(resctx, 0, key, jo, tab);
	if (res)
	    break;
    }

    if (!res)
	res = lmap_add_result(lmap, resctx);
    else
	lmap_wrn("invalid result in report result array");

    if (res)
	lmap_result_free(resctx);

    return 0; /* FIXME: xml-io does the same, continue without the result */
}

static int xx_lrs_date(void *p, const char *s)
{ struct lmap *lmap = p; return lmap_agent_set_report_date(lmap->agent, s); }

static int xx_las_agent_id(void *p, const char *s)
{
    struct lmap *lmap = p;
    int res = lmap_agent_set_agent_id(lmap->agent, s);
    if (!res)
	lmap_agent_set_report_agent_id(lmap->agent, "true");
    return res;
}

static int xx_las_group_id(void *p, const char *s)
{
    struct lmap *lmap = p;
    int res = lmap_agent_set_group_id(lmap->agent, s);
    if (!res)
	lmap_agent_set_report_group_id(lmap->agent, "true");
    return res;
}

static int xx_las_measurement_point(void *p, const char *s)
{
    struct lmap *lmap = p;
    int res = lmap_agent_set_measurement_point(lmap->agent, s);
    if (!res)
	lmap_agent_set_report_measurement_point(lmap->agent, "true");
    return res;
}

static int
parse_report_doc(struct lmap *lmap, json_object *root)
{
    json_object *report_obj = NULL;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING_X(date,     0, xx_lrs_date),
	JSONMAP_ENTRY_STRING_X(agent-id, 0, xx_las_agent_id),
	JSONMAP_ENTRY_STRING_X(group-id, 0, xx_las_group_id),
	JSONMAP_ENTRY_STRING_X(measurement-point, 0, xx_las_measurement_point),
	JSONMAP_ENTRY_OBJARRAY_X(result, 0, parse_report_result),
	{ .name = NULL }
    };

    if (!root)
	goto err_exit;

    /* must be an object with a single field, report, which is
     * also an object */
    if (!json_object_object_get_ex(root, "ietf-lmap-report:report", &report_obj)
	 && !json_object_object_get_ex(root, "report", &report_obj))
	goto err_exit;
    if (json_object_object_length(root) != 1
	|| !json_object_is_type(report_obj, json_type_object))
	goto err_exit;

    if (!lmap->agent) {
	lmap->agent = lmap_agent_new();
	if (! lmap->agent)
            return -1;
    }

    /* iterate the inner "report" object */
    json_object_object_foreach(report_obj, ctxkey, jo) {
	res = lookup_jsonmap(lmap, 0, ctxkey, jo, tab);
	if (res)
	    break;
    }

err_exit:
    if (res)
	lmap_err("could not read report data");

    return res;
}


/*
 * JSON input I/O and parsing
 */

/* piece-wise read and parse JSON data file */
static json_object *
parse_file(const char *file, const char *what)
{
    char *buf = NULL;
    ssize_t res = -1;
    int fd = -1;

    enum json_tokener_error jerr;
    json_object *jo  = NULL;
    struct json_tokener *jtk = NULL;

    buf = malloc(JSON_READ_BUFFER_SZ);
    jtk = json_tokener_new();
    if (!jtk || !buf) {
	lmap_err("out of memory while reading %s file '%s'", what, file);
	goto res_out;
    }

    fd = open(file, O_CLOEXEC, O_RDONLY);
    if (fd == -1) {
	lmap_err("failed to open '%s': %s", file, strerror(errno));
	goto res_out;
    }

    json_tokener_set_flags(jtk, JSON_TOKENER_STRICT);
    do {
	res = read(fd, buf, JSON_READ_BUFFER_SZ);
	if (res == -1) {
	    if (errno == EAGAIN || errno == EINTR)
		continue;

	    lmap_err("error while reading '%s': %s", file, strerror(errno));
	    goto res_out; /* res = -1 already */
	}

	if (res > 0) {
	    jo = json_tokener_parse_ex(jtk, buf, res); /* res clamped to ( 0, JSON_READ_BUFFER_SZ ] */
	    jerr = json_tokener_get_error(jtk);
	}
    } while (res > 0 && jerr == json_tokener_continue);

    if (jerr != json_tokener_success || !jo) {
	lmap_err("invalid JSON in '%s': %s", file, json_tokener_error_desc(jerr));
	res = -1;
    }

res_out:
    if (jtk)
	json_tokener_free(jtk);
    if (fd != -1)
	(void) close(fd);
    free(buf);

    if (res == -1) {
	json_object_put(jo);
	return NULL;
    }

    return jo;
}

static int
parse_path(struct lmap *lmap, const char *path,
	   lmap_json_file_parse_func *cb, const char *what)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(path && cb && what);

    dfd = opendir(path);
    if (!dfd) {
	if (errno == ENOTDIR) {
	    return (*cb)(lmap, path);
	} else {
	    lmap_err("cannot read %s path '%s'", what, path);
	    return -1;
	}
    }

    while ((dp = readdir(dfd)) != NULL) {
	size_t len = strlen(dp->d_name);
	if (len < 6)
	    continue;
	if (dp->d_name[0] == '.')
	    continue;
	if (strcmp(dp->d_name + len - 5, ".json"))
	    continue;

	(void) snprintf(filepath, sizeof(filepath), "%s/%s", path, dp->d_name);
	if ((*cb)(lmap, filepath) < 0) {
	    ret = -1;
	    break;
	}
    }
    (void) closedir(dfd);

    return ret;
}

static int
parse_string(struct lmap *lmap, const char *string, lmap_parse_doc_func *cb)
{
    int ret = 0;
    enum json_tokener_error je = json_tokener_success;
    json_object *jo;

    assert(lmap && cb);

    if (!string)
	return 0;

    jo = json_tokener_parse_verbose(string, &je);
    if (!jo) {
	if (je == json_tokener_success) {
	    lmap_err("could not create JSON tokener");
	} else {
	    lmap_err("invalid JSON: %s", json_tokener_error_desc(je));
	}
	ret = -1;
    } else {
	ret = (*cb)(lmap, jo);
    }

    if (jo)
	json_object_put(jo);

    return ret;
}

int
lmap_json_parse_config_file(struct lmap *lmap, const char *file)
{
    return parse_config_doc(lmap, parse_file(file, "config"));
}

int
lmap_json_parse_config_path(struct lmap *lmap, const char *path)
{
    return parse_path(lmap, path, &lmap_json_parse_config_file, "config");
}

int
lmap_json_parse_config_string(struct lmap *lmap, const char *string)
{
    return parse_string(lmap, string, &parse_config_doc);
}

int
lmap_json_parse_state_file(struct lmap *lmap, const char *file)
{
    return parse_state_doc(lmap, parse_file(file, "state"));
}

int
lmap_json_parse_state_path(struct lmap *lmap, const char *path)
{
    return parse_path(lmap, path, &lmap_json_parse_state_file, "capability");
}

int
lmap_json_parse_state_string(struct lmap *lmap, const char *string)
{
    return parse_string(lmap, string, &parse_state_doc);
}

int
lmap_json_parse_report_file(struct lmap *lmap, const char *file)
{
    return parse_report_doc(lmap, parse_file(file, "report"));
}

int
lmap_json_parse_report_string(struct lmap *lmap, const char *string)
{
    return parse_string(lmap, string, &parse_report_doc);
}


/*
 * JSON output I/O and rendering/serializing
 */

static void
render_leaf(json_object *jobj, char *name, char *content)
{
    assert(jobj);

    if (name && content) {
	json_object_object_add(jobj, name, json_object_new_string(content));
    }
}

static void
render_leaf_int32(json_object *jobj, char *name, int32_t value)
{
    json_object_object_add(jobj, name, json_object_new_int(value));
}

static void
render_leaf_datetime(json_object *jobj, char *name, time_t *tp)
{
    char buf[32];
    struct tm *tmp;

    tmp = localtime(tp);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tmp);

    /*
     * Hack to insert the ':' in the timezone offset since strftime()
     * implementations do not generate this separator.
     */

    if (strlen(buf) == 24) {
	buf[25] = buf[24];
	buf[24] = buf[23];
	buf[23] = buf[22];
	buf[22] = ':';
    }

    render_leaf(jobj, name, buf);
}

static void
render_tags(struct tag *tags, const char *name, json_object *jobj)
{
    json_object *ja;
    struct tag *t;

    if (!tags)
	return;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);
    for (t = tags; t; t = t->next) {
	json_object_array_add(ja, json_object_new_string(t->tag));
    }
}

/* jobj is the option array */
static void
render_option(struct option *option, json_object *jobj)
{
    json_object *robj;

    if (! option) {
	return;
    }

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    render_leaf(robj, "id", option->id);
    render_leaf(robj, "name", option->name);
    render_leaf(robj, "value", option->value);
}

static void
render_options(struct option *options, json_object *jobj)
{
    struct option *opt;
    json_object *ja;

    if (!options)
	return;

    ja = json_object_new_array();
    if (ja) {
	json_object_object_add(jobj, "option", ja);
	for (opt = options; opt; opt = opt->next) {
	    render_option(opt, ja);
	}
    }
}

static void
render_agent_report(struct agent *agent, json_object *jobj)
{
    if (! agent) {
	return;
    }

    render_leaf_datetime(jobj, "date", &agent->report_date);
    if (agent->agent_id && agent->report_agent_id) {
	render_leaf(jobj, "agent-id", agent->agent_id);
    }
    if (agent->group_id && agent->report_group_id) {
	render_leaf(jobj, "group-id", agent->group_id);
    }
    if (agent->measurement_point && agent->report_measurement_point) {
	render_leaf(jobj, "measurement-point", agent->measurement_point);
    }
}

static void
render_row(struct row *row, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct value *val;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    aobj = json_object_new_array();
    if (!aobj) {
	return;
    }

    json_object_object_add(robj, "value", aobj);
    for (val = row->values; val; val = val->next) {
	json_object_array_add(aobj, json_object_new_string(val->value ? val->value : ""));
    }
}

static void
render_table(struct table *tab, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct row *row;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    aobj = json_object_new_array();
    if (! aobj) {
	return;
    }

    json_object_object_add(robj, "row", aobj);
    for (row = tab->rows; row; row = row->next) {
	render_row(row, aobj);
    }
}

static void
render_result(struct result *res, json_object *jobj)
{
    json_object *robj;
    json_object *aobj;
    struct table *tab;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    render_leaf(robj, "schedule", res->schedule);
    render_leaf(robj, "action", res->action);
    render_leaf(robj, "task", res->task);
    render_options(res->options, robj);
    render_tags(res->tags, "tag", robj);

    if (res->event) {
	render_leaf_datetime(robj, "event", &res->start);
    }

    if (res->start) {
	render_leaf_datetime(robj, "start", &res->start);
    }

    if (res->end) {
	render_leaf_datetime(robj, "end", &res->end);
    }

    if (res->cycle_number) {
	render_leaf(robj, "cycle-number", res->cycle_number);
    }

    if (res->flags & LMAP_RESULT_FLAG_STATUS_SET) {
	render_leaf_int32(robj, "status", res->status);
    }

    aobj = json_object_new_array();
    if (aobj) {
	json_object_object_add(robj, "table", aobj);
	for (tab = res->tables; tab; tab = tab->next) {
	    render_table(tab, aobj);
	}
    }
}

/**
 * @brief Returns a JSON rendering of the lmap config
 *
 * This function renders the current lmap config into an JSON document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap config to be rendered.
 * @return An JSON document as a string that must be freed by the
 *         caller or NULL on error
 */
char *
lmap_json_render_config(struct lmap *lmap)
{
    return NULL;
}

/**
 * @brief Returns a JSON rendering of the lmap state
 *
 * This function renders the current lmap state into an JSON document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap state to be rendered.
 * @return An JSON document as a string that must be freed by the
 *         caller or NULL on error
 */
char *
lmap_json_render_state(struct lmap *lmap)
{
    return NULL;
}

/**
 * @brief Returns a JSON rendering of the lmap report
 *
 * This function renders the current lmap report into an JSON document
 * according to the IETF's LMAP YANG data model.
 *
 * @param lmap The pointer to the lmap report to be rendered.
 * @return An JSON document as a string that must be freed by the
 *         caller or NULL on error
 */
char *
lmap_json_render_report(struct lmap *lmap)
{
    char *report = NULL;
    json_object *jobj, *aobj, *robj;
    struct result *res;
    const char *r1;

    assert(lmap);

    if (!(jobj = json_object_new_object()))
	return NULL;
    if (!(robj = json_object_new_object()))
	goto err_exit;

    json_object_object_add(jobj, LMAPR_JSON_NAMESPACE ":" "report", robj);
    render_agent_report(lmap->agent, robj);

    if (!(aobj = json_object_new_array()))
	goto err_exit;
    json_object_object_add(robj, "result", aobj);
    for (res = lmap->results; res; res = res->next) {
	render_result(res, aobj);
    }

    r1 = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY);
    if (r1)
	report = strdup(r1);

err_exit:
    json_object_put(jobj);
    return report;
}
