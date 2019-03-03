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
 * 5. Consolidate all the "foos": { "foo": [ ] } drivers into calls
 *    to a single helper function.
 */

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L

#ifdef WITH_JSON

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

/* RFC-7951 demands that empty == [null], but we accept other
 * ways to represent an existing-but-empty json field as well */
static int lmap_json_object_is_empty(json_object * const jobj)
{
    switch (json_object_get_type(jobj)) {
    case json_type_array:
	/* empty array or array with a null element are acceptable */
	return !!(json_object_array_length(jobj) == 0
		|| (json_object_array_length(jobj) == 1
		    && json_object_is_type(json_object_array_get_idx(jobj, 0), json_type_null)));
    case json_type_object:
	/* empty object is acceptable */
	return !!(json_object_object_length(jobj) == 0);
    /* maybe also json_type_string and an empty string? */
    default:
	/* anything else exists and has some sort of content */
	return 0;
    }
}

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


/* parse registry (function) list */

static int
xx_lreg_uri(void *p, const char *s)
{ struct registry *registry = p; return lmap_registry_set_uri(registry, s); }

static int
xx_lreg_role(void *p, const char *s)
{ struct registry *registry = p; return lmap_registry_add_role(registry, s); }

/* ctx is an array member of "function" or NULL */
static struct registry *
parse_registry(json_object *ctx, int what)
{
    struct registry *registry;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(uri,    YANG_CONFIG_TRUE, xx_lreg),
	JSONMAP_ENTRY_STRARRAY(role, YANG_CONFIG_TRUE, xx_lreg),
	{ .name = NULL }
    };

    if (!ctx)
	return NULL;

    registry = lmap_registry_new();
    if (!registry)
	return NULL;

    if (json_object_is_type(ctx, json_type_object)) {
	json_object_object_foreach(ctx, key, jo) {
	    res = lookup_jsonmap(registry, what, key, jo, tab);
	    if (res)
		break;
	}
    }

    if (res) {
	lmap_wrn("invalid function in function array");
	lmap_registry_free(registry);
	registry = NULL;
    }

    return registry;
}

/* parsing of tasks.task structures */

typedef int (task_item_add_func)(void *p, struct task *task);

static int
xx_lt_name(void *p, const char *s)
{ struct task *task = p; return lmap_task_set_name(task, s); }

static int
xx_lt_program(void *p, const char *s)
{ struct task *task = p; return lmap_task_set_program(task, s); }

static int
xx_lt_version(void *p, const char *s)
{ struct task *task = p; return lmap_task_set_version(task, s); }

static int
xx_lt_tag(void *p, const char *s)
{ struct task *task = p; return lmap_task_add_tag(task, s); }

/* ctx is an array member of "tasks.task.option" or NULL */
static int xx_lt_option(void *p, json_object *ctx, int what)
{
    struct task *task = p;
    struct option *option;
    int res = -1;

    option = parse_option(ctx, what);
    if (option)
	res = lmap_task_add_option(task, option);

    return res;
}

/* ctx is an array member of "tasks.task.function" or NULL */
static int xx_lt_function(void *p, json_object *ctx, int what)
{
    struct task *task = p;
    struct registry *registry;
    int res = -1;

    registry = parse_registry(ctx, what);
    if (registry)
	res = lmap_task_add_registry(task, registry);

    return res;
}

static struct task *
parse_task(json_object *ctx, int what)
{
    struct task *task;
    int res = -1;

    /* NOTE: unusual handling of "what", see parse_tasks() */
    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(name,       YANG_KEY,                             xx_lt),
	JSONMAP_ENTRY_STRING(program,    YANG_CONFIG_TRUE | YANG_CONFIG_FALSE, xx_lt),
	JSONMAP_ENTRY_STRING(version,                       YANG_CONFIG_FALSE, xx_lt),
	JSONMAP_ENTRY_STRARRAY(tag,      YANG_CONFIG_TRUE,                     xx_lt),
	JSONMAP_ENTRY_OBJARRAY(option,   YANG_CONFIG_TRUE,                     xx_lt),
	JSONMAP_ENTRY_OBJARRAY(function, YANG_CONFIG_TRUE | YANG_CONFIG_FALSE, xx_lt),
	{ .name = NULL }
    };

    if (!ctx || !json_object_is_type(ctx, json_type_object))
	return NULL;

    task = lmap_task_new();
    if (!task)
	return NULL;

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(task, what, key, jo, tab);
	if (res)
	    break;
    }

    if (res) {
	lmap_task_free(task);
	task = NULL;
    }

    return task;
}

/*
 * NOTE: unusual handling of "what" required for normal use:
 * Either set PARSE_CONFIG_TRUE or PARSE_CONFIG_FALSE, not both.
 *
 * CONFIG_TRUE  enables fields valid in lmap::tasks::task
 * CONFIG_FALSE enables fields valid in lmap::capabilities::task
 */
static int
parse_tasks(void *p, json_object *ctx, int what, task_item_add_func *task_add)
{
    json_object *task_obj = NULL;
    struct task *task;
    int res, i;

    /* ctx must be an object with a single field "task", which is an array */
    if (!ctx)
	return -1;
    if (!json_object_object_get_ex(ctx, "task", &task_obj))
	return -1;
    if (json_object_object_length(ctx) != 1
	|| !json_object_is_type(task_obj, json_type_array))
	return -1;

    /* iterate the inner "task" array */
    for (res = 0, i = 0; !res && i < json_object_array_length(task_obj); i++) {
	task = parse_task(json_object_array_get_idx(task_obj, i), what);
	res = (task)? 0 : -1;
	if (!res)
	    (* task_add)(p, task);
    }

    if (res)
	lmap_wrn("invalid task in task array");
    return res;
}

/* lmap-control :: lmap :: tasks */

static int
xx_lctrlt_add_task(void *p, struct task *task)
{ struct lmap *lmap = p; return lmap_add_task(lmap, task); }

/* note the special handling of "what" for parse_task() */
static int
xx_lctrl_tasks(void *p, json_object *ctx, int what)
{ return parse_tasks(p, ctx, what & ~PARSE_CONFIG_FALSE, &xx_lctrlt_add_task); }

/* lmap-control :: lmap :: capabilities */

static int
xx_lcapt_add_task(void *p, struct task *task)
{ struct capability *cap = p; return lmap_capability_add_task(cap, task); }

/* note the special handling of "what" for parse_task() */
static int
xx_lcap_tasks(void *p, json_object *ctx, int what)
{ return parse_tasks(p, ctx, what & ~PARSE_CONFIG_TRUE, &xx_lcapt_add_task); }

static int
xx_lcap_version(void *p, const char *s)
{ struct capability *capability = p; return lmap_capability_set_version(capability, s); }

static int
xx_lcap_tag(void *p, const char *s)
{ struct capability *capability = p; return lmap_capability_add_tag(capability, s); }

static int
xx_lctrl_capabilities(void *p, json_object *ctx, int what)
{
    struct lmap *lmap = p;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(version, YANG_CONFIG_FALSE, xx_lcap),
	JSONMAP_ENTRY_STRARRAY(tag,   YANG_CONFIG_FALSE, xx_lcap),
	JSONMAP_ENTRY_OBJECT(tasks,   YANG_CONFIG_FALSE, xx_lcap),
	{ .name = NULL }
    };

    if (!ctx || !json_object_is_type(ctx, json_type_object))
	return -1;

    if (!lmap->capabilities) {
	if (!(lmap->capabilities = lmap_capability_new()))
	    return -1;
    }

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(lmap->capabilities, what, key, jo, tab);
	if (res)
	    break;
    }

    return res;
}

/* lmap-control :: lmap :: agent */

static int xx_lcas_agent_id(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_agent_id(agent, s); }

static int xx_lcas_group_id(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_group_id(agent, s); }

static int xx_lcas_measurement_point(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_measurement_point(agent, s); }

static int xx_lcas_report_agent_id(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_report_agent_id(agent, s); }

static int xx_lcas_report_group_id(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_report_group_id(agent, s); }

static int xx_lcas_report_measurement_point(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_report_measurement_point(agent, s); }

static int xx_lcas_controller_timeout(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_controller_timeout(agent, s); }

static int xx_lcas_last_started(void *p, const char *s)
{ struct agent *agent = p; return lmap_agent_set_last_started(agent, s); }

static int
xx_lctrl_agent(void *p, json_object *ctx, int what)
{
    struct lmap *lmap = p;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING_X(agent-id,            YANG_CONFIG_TRUE, xx_lcas_agent_id),
	JSONMAP_ENTRY_STRING_X(group-id,            YANG_CONFIG_TRUE, xx_lcas_group_id),
	JSONMAP_ENTRY_STRING_X(measurement-point,   YANG_CONFIG_TRUE, xx_lcas_measurement_point),
	JSONMAP_ENTRY_BOOL2STR_X(report-agent-id,   YANG_CONFIG_TRUE, xx_lcas_report_agent_id),
	JSONMAP_ENTRY_BOOL2STR_X(report-group-id,   YANG_CONFIG_TRUE, xx_lcas_report_group_id),
	JSONMAP_ENTRY_BOOL2STR_X(report-measurement-point, YANG_CONFIG_TRUE, xx_lcas_report_measurement_point),
	JSONMAP_ENTRY_INT2STR_X(controller-timeout, YANG_CONFIG_TRUE, xx_lcas_controller_timeout),
	JSONMAP_ENTRY_STRING_X(last-started,        YANG_CONFIG_FALSE, xx_lcas_last_started),
	{ .name = NULL }
    };

    if (!lmap->agent) {
	if (!(lmap->agent = lmap_agent_new()))
	    return -1;
    }

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(lmap->agent, what, key, jo, tab);
	if (res)
	    break;
    }

    return res;
}

/* lmap-control :: lmap :: events */

static int xx_lce_interval(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_interval(event, s); }

static int xx_lce_start(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_start(event, s); }

static int xx_lce_end(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_end(event, s); }

static int xx_lcec_month(void *p, const char *s)
{ struct event *event = p; return lmap_event_add_month(event, s); }

static int xx_lcec_day_of_month(void *p, json_object *jo, int unused)
{
    struct event *event = p; UNUSED(unused);
    return lmap_event_add_day_of_month(event, json_object_get_string(jo));
}

static int xx_lcec_day_of_week(void *p, json_object *jo, int unused)
{
    struct event *event = p; UNUSED(unused);
    return lmap_event_add_day_of_week(event, json_object_get_string(jo));
}

static int xx_lcec_hour(void *p, json_object *jo, int unused)
{
    struct event *event = p; UNUSED(unused);
    return lmap_event_add_hour(event, json_object_get_string(jo));
}

static int xx_lcec_minute(void *p, json_object *jo, int unused)
{
    struct event *event = p; UNUSED(unused);
    return lmap_event_add_minute(event, json_object_get_string(jo));
}

static int xx_lcec_second(void *p, json_object *jo, int unused)
{
    struct event *event = p; UNUSED(unused);
    return lmap_event_add_second(event, json_object_get_string(jo));
}

static int xx_lcec_timezone_offset(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_timezone_offset(event, s); }

static int
xx_lcee(void *p, json_object *ctx, int what,
        char* type, const struct lmap_jsonmap *tab)
{
    struct event *event = p;
    int res = -1;

    if (!event || !ctx)
	return -1;

    json_object_object_foreach(ctx, ctxkey, jo) {
	res = lookup_jsonmap(event, what, ctxkey, jo, tab);
	if (res)
	    break;
    }

    if (!res)
	lmap_event_set_type(event, type);

    return res;
}

static int
xx_lcee_periodic(void *p, json_object *ctx, int what)
{
    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_INT2STR(interval, YANG_CONFIG_TRUE, xx_lce),
	JSONMAP_ENTRY_STRING(start,     YANG_CONFIG_TRUE, xx_lce),
	JSONMAP_ENTRY_STRING(end,       YANG_CONFIG_TRUE, xx_lce),
	{ .name = NULL }
    };

    return xx_lcee(p, ctx, what, "periodic", tab);
}

static int
xx_lcee_calendar(void *p, json_object *ctx, int what)
{
    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRARRAY(month,          YANG_CONFIG_TRUE, xx_lcec),
	JSONMAP_ENTRY_OBJARRAY_X(day-of-month, YANG_CONFIG_TRUE, xx_lcec_day_of_month),
	JSONMAP_ENTRY_OBJARRAY_X(day-of-week,  YANG_CONFIG_TRUE, xx_lcec_day_of_week),
	JSONMAP_ENTRY_OBJARRAY(hour,           YANG_CONFIG_TRUE, xx_lcec),
	JSONMAP_ENTRY_OBJARRAY(minute,         YANG_CONFIG_TRUE, xx_lcec),
	JSONMAP_ENTRY_OBJARRAY(second,         YANG_CONFIG_TRUE, xx_lcec),
	JSONMAP_ENTRY_STRING_X(timezone-offset,YANG_CONFIG_TRUE, xx_lcec_timezone_offset),
	JSONMAP_ENTRY_STRING(start,            YANG_CONFIG_TRUE, xx_lce),
	JSONMAP_ENTRY_STRING(end,              YANG_CONFIG_TRUE, xx_lce),
	{ .name = NULL }
    };

    return xx_lcee(p, ctx, what, "calendar", tab);
}

static int
xx_lcee_one_off(void *p, json_object *ctx, int what)
{
    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING_X(time, YANG_CONFIG_TRUE, xx_lce_start),
	{ .name = NULL }
    };

    return xx_lcee(p, ctx, what, "one-off", tab);
}

static int
xx_lceez(void *p, json_object *ctx, char *type)
{
    struct event *event = p;

    if (!event || !ctx)
	return -1;
    if (!lmap_json_object_is_empty(ctx)) {
	lmap_wrn("invalid event of type %s: not empty", type);
	return -1;
    }

    return lmap_event_set_type(event, type);
}

static int xx_lceez_immediate(void *p, json_object *ctx, int unused)
{ UNUSED(unused); return xx_lceez(p, ctx, "immediate"); }

static int xx_lceez_startup(void *p, json_object *ctx, int unused)
{ UNUSED(unused); return xx_lceez(p, ctx, "startup"); }

static int xx_lceez_controller_lost(void *p, json_object *ctx, int unused)
{ UNUSED(unused); return xx_lceez(p, ctx, "controller-lost"); }

static int xx_lceez_controller_connected(void *p, json_object *ctx, int unused)
{ UNUSED(unused); return xx_lceez(p, ctx, "controller-connected"); }

static int xx_lce_name(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_name(event, s); }

static int xx_lce_random_spread(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_random_spread(event, s); }

static int xx_lce_cycle_interval(void *p, const char *s)
{ struct event *event = p; return lmap_event_set_cycle_interval(event, s); }

static struct event *
parse_event(json_object *ctx, int what)
{
    struct event *event;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(name, YANG_KEY, xx_lce),
	JSONMAP_ENTRY_INT2STR_X(random-spread,  YANG_CONFIG_TRUE, xx_lce_random_spread),
	JSONMAP_ENTRY_INT2STR_X(cycle-interval, YANG_CONFIG_TRUE, xx_lce_cycle_interval),
	JSONMAP_ENTRY_OBJECT(periodic,  YANG_CONFIG_TRUE, xx_lcee),
	JSONMAP_ENTRY_OBJECT(calendar,  YANG_CONFIG_TRUE, xx_lcee),
	JSONMAP_ENTRY_OBJECT_X(one-off, YANG_CONFIG_TRUE, xx_lcee_one_off),
	JSONMAP_ENTRY_OBJANY(immediate, YANG_CONFIG_TRUE, xx_lceez),
	JSONMAP_ENTRY_OBJANY(startup,   YANG_CONFIG_TRUE, xx_lceez),
	JSONMAP_ENTRY_OBJANY_X(controller-lost,      YANG_CONFIG_TRUE, xx_lceez_controller_lost),
	JSONMAP_ENTRY_OBJANY_X(controller-connected, YANG_CONFIG_TRUE, xx_lceez_controller_connected),
	{ .name = NULL }
    };

    if (!ctx || !json_object_is_type(ctx, json_type_object))
	return NULL;

    event = lmap_event_new();
    if (!event)
	return NULL;

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(event, what, key, jo, tab);
	if (res)
	    break;
    }

    if (res) {
	lmap_event_free(event);
	event = NULL;
    }

    return event;
}

static int
xx_lctrl_events(void *p, json_object *ctx, int what)
{
    json_object *event_obj = NULL;
    struct lmap *lmap = p;
    struct event *event;
    int res, i;

    /* ctx must be an object with a single field "event", which is an array */
    if (!ctx)
	return -1;
    if (!json_object_object_get_ex(ctx, "event", &event_obj))
	return -1;
    if (json_object_object_length(ctx) != 1
	|| !json_object_is_type(event_obj, json_type_array))
	return -1;

    /* iterate the inner "event" array */
    for (res = 0, i = 0; !res && i < json_object_array_length(event_obj); i++) {
	event = parse_event(json_object_array_get_idx(event_obj, i), what);
	res = (event)? 0 : -1;
	if (!res)
	    lmap_add_event(lmap, event);
    }

    if (res)
	lmap_wrn("invalid event in events array");

    return res;
}

/* lmap-control :: lmap :: suppressions */

static int xx_lcsp_name(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_set_name(supp, s); }

static int xx_lcsp_start(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_set_start(supp, s); }

static int xx_lcsp_end(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_set_end(supp, s); }

static int xx_lcsp_match(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_add_match(supp, s); }

static int xx_lcsp_stop_running(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_set_stop_running(supp, s); }

static int xx_lcsp_state(void *p, const char *s)
{ struct supp *supp = p; return lmap_supp_set_state(supp, s); }

static struct supp *
parse_suppression(json_object *ctx, int what)
{
    struct supp *supp;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(name,    YANG_KEY, xx_lcsp),
	JSONMAP_ENTRY_STRING(start,   YANG_CONFIG_TRUE, xx_lcsp),
	JSONMAP_ENTRY_STRING(end,     YANG_CONFIG_TRUE, xx_lcsp),
	JSONMAP_ENTRY_STRARRAY(match, YANG_CONFIG_TRUE, xx_lcsp),
	JSONMAP_ENTRY_BOOL2STR_X(stop-running, YANG_CONFIG_TRUE, xx_lcsp_stop_running),
	JSONMAP_ENTRY_STRING(state,   YANG_CONFIG_FALSE, xx_lcsp),
	{ .name = NULL }
    };

    if (!ctx || !json_object_is_type(ctx, json_type_object))
	return NULL;

    supp = lmap_supp_new();
    if (!supp)
	return NULL;

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(supp, what, key, jo, tab);
	if (res)
	    break;
    }

    if (res) {
	lmap_supp_free(supp);
	supp = NULL;
    }

    return supp;
}

static int
xx_lctrl_suppressions(void *p, json_object *ctx, int what)
{
    json_object *supp_obj = NULL;
    struct lmap *lmap = p;
    struct supp *supp;
    int res, i;

    /* ctx must be an object with a single field "suppression", which is an array */
    if (!ctx)
	return -1;
    if (!json_object_object_get_ex(ctx, "suppression", &supp_obj))
	return -1;
    if (json_object_object_length(ctx) != 1
	|| !json_object_is_type(supp_obj, json_type_array))
	return -1;

    /* iterate the inner "suppression" array */
    for (res = 0, i = 0; !res && i < json_object_array_length(supp_obj); i++) {
	supp = parse_suppression(json_object_array_get_idx(supp_obj, i), what);
	res = (supp)? 0 : -1;
	if (!res)
	    lmap_add_supp(lmap, supp);
    }

    if (res)
	lmap_wrn("invalid suppression in suppresions array");

    return res;
}

/* lmap-control :: lmap :: schedules :: action */

static int xx_lact_name(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_name(action, s); }

static int xx_lact_task(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_task(action, s); }

/* ctx is an array member of "action.option" or NULL */
static int xx_lact_option(void *p, json_object *ctx, int what)
{
    struct action *action = p;
    struct option *option;
    int res = -1;

    option = parse_option(ctx, what);
    if (option)
	res = lmap_action_add_option(action, option);

    return res;
}

static int xx_lact_destination(void *p, const char *s)
{ struct action *action = p; return lmap_action_add_destination(action, s); }

static int xx_lact_tag(void *p, const char *s)
{ struct action *action = p; return lmap_action_add_tag(action, s); }

static int xx_lact_supp_tag(void *p, const char *s)
{ struct action *action = p; return lmap_action_add_suppression_tag(action, s); }

static int xx_lact_state(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_state(action, s); }

static int xx_lact_storage(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_storage(action, s); }

static int xx_lact_invocations(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_invocations(sch, s); }

static int xx_lact_suppressions(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_suppressions(sch, s); }

static int xx_lact_overlaps(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_overlaps(sch, s); }

static int xx_lact_failures(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_failures(sch, s); }

static int xx_lact_last_invocation(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_last_invocation(sch, s); }

static int xx_lact_last_completion(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_last_completion(sch, s); }

static int xx_lact_last_status(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_last_status(action, s); }

static int xx_lact_last_message(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_last_message(action, s); }

static int xx_lact_last_failed_completion(void *p, const char *s)
{ struct action *sch = p; return lmap_action_set_last_failed_completion(sch, s); }

static int xx_lact_last_failed_status(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_last_failed_status(action, s); }

static int xx_lact_last_failed_message(void *p, const char *s)
{ struct action *action = p; return lmap_action_set_last_failed_message(action, s); }

static struct action *
parse_action(json_object *ctx, int what)
{
    struct action *action;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(name,                YANG_KEY,          xx_lact),
	JSONMAP_ENTRY_STRING(task,                YANG_CONFIG_TRUE,  xx_lact),
	JSONMAP_ENTRY_OBJARRAY(option,            YANG_CONFIG_TRUE,  xx_lact),
	JSONMAP_ENTRY_STRARRAY(destination,       YANG_CONFIG_TRUE,  xx_lact),
	JSONMAP_ENTRY_STRARRAY(tag,               YANG_CONFIG_TRUE,  xx_lact),
	JSONMAP_ENTRY_STRARRAY_X(suppression-tag, YANG_CONFIG_TRUE,  xx_lact_supp_tag),
	JSONMAP_ENTRY_STRING(state,               YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_STRING(storage,             YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_INT2STR(invocations,        YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_INT2STR(suppressions,       YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_INT2STR(overlaps,           YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_INT2STR(failures,           YANG_CONFIG_FALSE, xx_lact),
	JSONMAP_ENTRY_STRING_X(last-invocation,   YANG_CONFIG_FALSE, xx_lact_last_invocation),
	JSONMAP_ENTRY_STRING_X(last-completion,   YANG_CONFIG_FALSE, xx_lact_last_completion),
	JSONMAP_ENTRY_INT2STR_X(last-status,      YANG_CONFIG_FALSE, xx_lact_last_status),
	JSONMAP_ENTRY_STRING_X(last-message,      YANG_CONFIG_FALSE, xx_lact_last_message),
	JSONMAP_ENTRY_STRING_X(last-failed-completion, YANG_CONFIG_FALSE, xx_lact_last_failed_completion),
	JSONMAP_ENTRY_INT2STR_X(last-failed-status,    YANG_CONFIG_FALSE, xx_lact_last_failed_status),
	JSONMAP_ENTRY_STRING_X(last-failed-message,    YANG_CONFIG_FALSE, xx_lact_last_failed_message),
	{ .name = NULL }
    };

    if (!ctx)
	return NULL;

    action = lmap_action_new();
    if (!action)
	return NULL;

    if (json_object_is_type(ctx, json_type_object)) {
	json_object_object_foreach(ctx, key, jo) {
	    res = lookup_jsonmap(action, what, key, jo, tab);
	    if (res)
		break;
	}
    }

    if (res) {
	lmap_wrn("invalid action in actions array");
	lmap_action_free(action);
	action = NULL;
    }

    return action;
}

/* ctx is an array item or NULL */
static int
xx_lsch_action(void *p, json_object *ctx, int what)
{
    struct schedule *sch = p;
    struct action *action;
    int res = -1;

    action = parse_action(ctx, what);
    if (action)
	res = lmap_schedule_add_action(sch, action);

    return res;
}

/* lmap-control :: lmap :: schedules */

static int xx_lsch_name(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_name(sch, s); }

static int xx_lsch_start(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_start(sch, s); }

static int xx_lsch_end(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_end(sch, s); }

static int xx_lsch_duration(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_duration(sch, s); }

static int xx_lsch_exec_mode(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_exec_mode(sch, s); }

static int xx_lsch_tag(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_add_tag(sch, s); }

static int xx_lsch_supp_tag(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_add_suppression_tag(sch, s); }

static int xx_lsch_state(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_state(sch, s); }

static int xx_lsch_storage(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_storage(sch, s); }

static int xx_lsch_invocations(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_invocations(sch, s); }

static int xx_lsch_suppressions(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_suppressions(sch, s); }

static int xx_lsch_overlaps(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_overlaps(sch, s); }

static int xx_lsch_failures(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_failures(sch, s); }

static int xx_lsch_last_invocation(void *p, const char *s)
{ struct schedule *sch = p; return lmap_schedule_set_last_invocation(sch, s); }

static struct schedule*
parse_schedule(json_object *ctx, int what)
{
    struct schedule *schedule;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_STRING(name,     YANG_KEY, xx_lsch),
	JSONMAP_ENTRY_STRING(start,    YANG_CONFIG_TRUE, xx_lsch),
	JSONMAP_ENTRY_STRING(end,      YANG_CONFIG_TRUE, xx_lsch),
	JSONMAP_ENTRY_STRING(duration, YANG_CONFIG_TRUE, xx_lsch),
	JSONMAP_ENTRY_STRING_X(execution-mode,    YANG_CONFIG_TRUE, xx_lsch_exec_mode),
	JSONMAP_ENTRY_STRARRAY_X(tag,             YANG_CONFIG_TRUE, xx_lsch_tag),
	JSONMAP_ENTRY_STRARRAY_X(suppression-tag, YANG_CONFIG_TRUE, xx_lsch_supp_tag),
	JSONMAP_ENTRY_OBJARRAY_X(action,          YANG_CONFIG_TRUE, xx_lsch_action),
	JSONMAP_ENTRY_STRING(state,               YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_STRING(storage,             YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_INT2STR(invocations,        YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_INT2STR(suppressions,       YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_INT2STR(overlaps,           YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_INT2STR(failures,           YANG_CONFIG_FALSE, xx_lsch),
	JSONMAP_ENTRY_STRING_X(last-invocation,   YANG_CONFIG_FALSE, xx_lsch_last_invocation),
	{ .name = NULL }
    };

    if (!ctx || !json_object_is_type(ctx, json_type_object))
	return NULL;

    schedule = lmap_schedule_new();
    if (!schedule)
	return NULL;

    json_object_object_foreach(ctx, key, jo) {
	res = lookup_jsonmap(schedule, what, key, jo, tab);
	if (res)
	    break;
    }

    if (res) {
	lmap_schedule_free(schedule);
	schedule = NULL;
    }

    return schedule;
}

static int
xx_lctrl_schedules(void *p, json_object *ctx, int what)
{
    json_object *schedule_obj = NULL;
    struct lmap *lmap = p;
    struct schedule *schedule;
    int res, i;

    /* ctx must be an object with a single field "schedule", which is an array */
    if (!ctx)
	return -1;
    if (!json_object_object_get_ex(ctx, "schedule", &schedule_obj))
	return -1;
    if (json_object_object_length(ctx) != 1
	|| !json_object_is_type(schedule_obj, json_type_array))
	return -1;

    /* iterate the inner "schedule" array */
    for (res = 0, i = 0; !res && i < json_object_array_length(schedule_obj); i++) {
	schedule = parse_schedule(json_object_array_get_idx(schedule_obj, i), what);
	res = (schedule)? 0 : -1;
	if (!res)
	    lmap_add_schedule(lmap, schedule);
    }

    if (res)
	lmap_wrn("invalid schedule in schedules array");

    return res;
}

/* lmap-control :: lmap */

/*
 * what:  PARSER_CONFIG_TRUE when parsing config files,
 *        hardcoded capabilities and state
 *
 *        PARSER_CONFIG_FALSE when parsing hardcoded
 *        capabilities and state
 */
static int
parse_control_doc(struct lmap *lmap, json_object *root, int what)
{
    json_object *control_obj = NULL;
    int res = -1;

    const struct lmap_jsonmap tab[] = {
	JSONMAP_ENTRY_OBJECT(capabilities, YANG_CONFIG_FALSE, xx_lctrl),
	JSONMAP_ENTRY_OBJECT(agent,        0, xx_lctrl),
	JSONMAP_ENTRY_OBJECT(tasks,        0, xx_lctrl),
	JSONMAP_ENTRY_OBJECT(schedules,    0, xx_lctrl),
	JSONMAP_ENTRY_OBJECT(suppressions, 0, xx_lctrl),
	JSONMAP_ENTRY_OBJECT(events,       0, xx_lctrl),
	{ .name = NULL }
    };

    if (!root)
	goto err_exit;

    /* must be an object with a single field "lmap", which is
     * also an object */
    if (!json_object_object_get_ex(root, "ietf-lmap-control:lmap", &control_obj)
	 && !json_object_object_get_ex(root, "lmap", &control_obj))
	goto err_exit;
    if (json_object_object_length(root) != 1
	|| !json_object_is_type(control_obj, json_type_object))
	goto err_exit;

    /* iterate the inner "control" object */
    json_object_object_foreach(control_obj, ctxkey, jo) {
	res = lookup_jsonmap(lmap, what, ctxkey, jo, tab);
	if (res)
	    break;
    }

err_exit:
    if (res)
	lmap_err("could not read config/state data");

    return res;
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

static int
xx_lrst_function(void *p, json_object *ctx, int what)
{
    struct table *tab = p;
    struct registry *registry;
    int res = -1;

    registry = parse_registry(ctx, what);
    if (registry)
	res = lmap_table_add_registry(tab, registry);

    return res;
}

static int
xx_lrst_column(void *p, json_object *ctx, int what)
{
    struct table *tab = p;

    UNUSED(what);

    if (!json_object_is_type(ctx, json_type_string))
	return -1;

    return lmap_table_add_column(tab, json_object_get_string(ctx));
}

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
json_object_objarray_add(json_object *jobj,
			const char *key,
			json_object *val)
{
    if (jobj) {
	if (json_object_is_type(jobj, json_type_object)) {
	    json_object_object_add(jobj, key, val);
	} else if (json_object_is_type(jobj, json_type_array)) {
	    json_object_array_add(jobj, val);
	} /* else "internal error!" */
    }
}

static void
render_empty_leaf(json_object *jobj, char *name)
{
    /* RFC7951 section 6.9 maps YANG empty to [null] */
    json_object *ja = json_object_new_array();
    if (ja) {
	json_object_array_add(ja, NULL);
	json_object_object_add(jobj, name, ja);
    }
}

static void
render_leaf(json_object *jobj, char *name, char *content)
{
    assert(jobj);

    if (name && content) {
	json_object_objarray_add(jobj, name, json_object_new_string(content));
    }
}

static void
render_leaf_boolean(json_object *jobj, char *name, int content)
{
    assert(jobj);

    if (name) {
	json_object_objarray_add(jobj, name,
		json_object_new_boolean(content ? (json_bool)1 : (json_bool)0));
    }
}

static void
render_leaf_int32(json_object *jobj, char *name, int32_t value)
{
    json_object_objarray_add(jobj, name, json_object_new_int(value));
}

static void
render_leaf_uint32(json_object *jobj, char *name, uint32_t value)
{
    /* JSON-C does not know uint32, but int64 can hold an uint32 */
    json_object_objarray_add(jobj, name, json_object_new_int64(value));
}

#if 0
/* I-JSON demands this */
static void
render_leaf_int64(json_object *jobj, char *name, int64_t value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%"PRIi64, value);
    json_object_objarray_add(jobj, name, json_object_new_string(buf));
}
#endif

/* I-JSON demands this */
static void
render_leaf_uint64(json_object *jobj, char *name, uint64_t value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%"PRIu64, value);
    json_object_objarray_add(jobj, name, json_object_new_string(buf));
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
render_leaf_days_of_month(json_object *jobj, char *name, uint32_t days_of_month)
{
    int i;
    json_object *ja;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);

    if (days_of_month == UINT32_MAX) {
	render_leaf(ja, name, "*");
	return;
    }

    for (i = 1; i < 32; i++) {
	if (days_of_month & (1 << i)) {
	    render_leaf_int32(ja, name, i);
	}
    }
}

static void
render_leaf_months(json_object *jobj, char *name, uint16_t months)
{
    json_object *ja;
    int i;
    struct {
	char *name;
	uint16_t value;
    } tab[] = {
	{ "january",	(1 << 0) },
	{ "february",	(1 << 1) },
	{ "march",	(1 << 2) },
	{ "april",	(1 << 3) },
	{ "may",	(1 << 4) },
	{ "june",	(1 << 5) },
	{ "july",	(1 << 6) },
	{ "august",	(1 << 7) },
	{ "september",	(1 << 8) },
	{ "october",	(1 << 9) },
	{ "november",	(1 << 10) },
	{ "december",	(1 << 11) },
	{ NULL, 0 }
    };

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);

    if (months == UINT16_MAX) {
	render_leaf(ja, name, "*");
	return;
    }

    for (i = 0; tab[i].name; i++) {
	if (months & tab[i].value) {
	    render_leaf(ja, name, tab[i].name);
	}
    }
}

static void
render_leaf_days_of_week(json_object *jobj, char *name, uint8_t days_of_week)
{
    json_object *ja;
    int i;
    struct {
	char *name;
	uint8_t value;
    } tab[] = {
	{ "monday",	(1 << 0) },
	{ "tuesday",	(1 << 1) },
	{ "wednesday",	(1 << 2) },
	{ "thursday",	(1 << 3) },
	{ "friday",	(1 << 4) },
	{ "saturday",	(1 << 5) },
	{ "sunday",	(1 << 6) },
	{ NULL, 0 }
    };

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);

    if (days_of_week == UINT8_MAX) {
	render_leaf(ja, name, "*");
	return;
    }

    for (i = 0; tab[i].name; i++) {
	if (days_of_week & tab[i].value) {
	    render_leaf(ja, name, tab[i].name);
	}
    }
}

static void
render_leaf_hours(json_object *jobj, char *name, uint32_t hours)
{
    int i;
    json_object *ja;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);

    if (hours == UINT32_MAX) {
	render_leaf(ja, name, "*");
	return;
    }

    for (i = 0; i < 24; i++) {
	if (hours & (1 << i)) {
	    render_leaf_int32(ja, name, i);
	}
    }
}

static void
render_leaf_minsecs(json_object *jobj, char *name, uint64_t minsecs)
{
    int i;
    json_object *ja;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, name, ja);

    if (minsecs == UINT64_MAX) {
	render_leaf(ja, name, "*");
	return;
    }

    for (i = 0; i < 60; i++) {
	if (minsecs & (1ull << i)) {
	    render_leaf_int32(ja, name, i);
	}
    }
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
render_registries(struct registry *registries, json_object *jobj)
{
    json_object *ja, *jf;
    struct registry *r;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, "function", ja);

    for (r = registries; r; r = r->next) {
	jf = json_object_new_object();
	if (!jf)
	    return;
	json_object_array_add(ja, jf);
	render_leaf(jf, "uri", r->uri);
	render_tags(r->roles, "role", jf);
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
    json_object *robj, *aobj;
    struct row *row;
    struct value *val;

    robj = json_object_new_object();
    if (! robj) {
	return;
    }
    json_object_array_add(jobj, robj);

    if (tab->registries)
        render_registries(tab->registries, robj);

    if (tab->columns) {
	if (!(aobj = json_object_new_array()))
	    return;
	json_object_object_add(robj, "column", aobj);
	for (val = tab->columns; val; val = val->next) {
	    json_object_array_add(aobj, json_object_new_string(val->value ? val->value : ""));
	}
    }

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

static int
render_agent(struct agent *agent, json_object *jobj, int what)
{
    assert(jobj);
    json_object *ja;

    if (!agent)
	return 0;

    ja = json_object_new_object();
    if (!ja)
	return -1;
    json_object_object_add(jobj, "agent", ja);

    if (what & RENDER_CONFIG_TRUE) {
	render_leaf(ja, "agent-id", agent->agent_id);
	render_leaf(ja, "group-id", agent->group_id);
	render_leaf(ja, "measurement-point", agent->measurement_point);
	if (agent->flags & LMAP_AGENT_FLAG_REPORT_AGENT_ID_SET)
	    render_leaf_boolean(ja, "report-agent-id", agent->report_agent_id);
        if (agent->flags & LMAP_AGENT_FLAG_REPORT_GROUP_ID_SET)
	    render_leaf_boolean(ja, "report-group-id", agent->report_group_id);
	if (agent->flags & LMAP_AGENT_FLAG_REPORT_MEASUREMENT_POINT_SET)
	    render_leaf_boolean(ja, "report-measurement-point", agent->report_measurement_point);
	if (agent->flags & LMAP_AGENT_FLAG_CONTROLLER_TIMEOUT_SET)
	    render_leaf_uint32(ja, "controller-timeout", agent->controller_timeout);
    }

    if (what & RENDER_CONFIG_FALSE) {
	if (agent->last_started)
	    render_leaf_datetime(ja, "last-started", &agent->last_started);
    }

    return 0;
}

static void
render_action(struct action *action, json_object *jobj, int what)
{
    if(!action || !action->name)
	return;

    render_leaf(jobj, "name", action->name);
    if (what & RENDER_CONFIG_TRUE) {
	render_leaf(jobj, "task", action->task);
        render_options(action->options, jobj);
	render_tags(action->destinations, "destination", jobj);
	render_tags(action->tags, "tag", jobj);
	render_tags(action->suppression_tags, "suppression-tag", jobj);
    }
    if (what & RENDER_CONFIG_FALSE) {
	char *state = NULL;
	switch (action->state) {
	case LMAP_SCHEDULE_STATE_ENABLED:
	    state = "enabled";
	    break;
	case LMAP_SCHEDULE_STATE_DISABLED:
	    state = "disabled";
	    break;
	case LMAP_SCHEDULE_STATE_RUNNING:
	    state = "running";
	    break;
	case LMAP_SCHEDULE_STATE_SUPPRESSED:
	    state = "suppressed";
	    break;
	}
	render_leaf(jobj, "state", state);

	render_leaf_uint64(jobj, "storage", action->storage);
	render_leaf_uint32(jobj, "invocations", action->cnt_invocations);
	render_leaf_uint32(jobj, "suppressions", action->cnt_suppressions);
	render_leaf_uint32(jobj, "overlaps", action->cnt_overlaps);
	render_leaf_uint32(jobj, "failures", action->cnt_failures);

	if (action->last_invocation)
	    render_leaf_datetime(jobj, "last-invocation", &action->last_invocation);
	if (action->last_completion) {
	    render_leaf_datetime(jobj, "last-completion", &action->last_completion);
	    render_leaf_int32(jobj, "last-status", action->last_status);
	    if (action->last_message)
		render_leaf(jobj, "last-message", action->last_message);
	}
	if (action->last_failed_completion) {
	    render_leaf_datetime(jobj, "last-failed-completion", &action->last_failed_completion);
	    render_leaf_int32(jobj, "last-failed-status", action->last_failed_status);
	    if (action->last_failed_message)
		render_leaf(jobj, "last-failed-message", action->last_failed_message);
	}
    }
}

static void
render_actions(struct action *actions, json_object *jobj, int what)
{
    json_object *ja, *jact;
    struct action *a;

    ja = json_object_new_array();
    if (!ja)
	return;
    json_object_object_add(jobj, "action", ja);

    for (a = actions; a; a = a->next) {
	if (!a->name)
	    continue;

	jact = json_object_new_object();
	if (!jact)
	    return;
	json_object_array_add(ja, jact);
	render_action(a, jact, what);
    }
}


static int
render_schedules(struct schedule *schedule, json_object *jobj, int what)
{
    json_object *jss, *ja, *js;

    if (!schedule)
	return 0;
    if (!jobj)
	return -1;

    jss = json_object_new_object();
    if (!jss)
	return -1;
    json_object_object_add(jobj, "schedules", jss);
    ja = json_object_new_array();
    if (!ja)
	return -1;
    json_object_object_add(jss, "schedule", ja);

    for (; schedule; schedule = schedule->next) {
	if (!schedule->name)
	    continue;

	js = json_object_new_object();
	if (!js)
	    return -1;
	json_object_array_add(ja, js);

	render_leaf(js, "name", schedule->name);
	if (what & RENDER_CONFIG_TRUE) {
	    render_leaf(js, "start", schedule->start);
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_END_SET)
		render_leaf(js, "end", schedule->end);
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET)
		render_leaf_uint64(js, "duration", schedule->duration);
	    if (schedule->flags & LMAP_SCHEDULE_FLAG_EXEC_MODE_SET) {
		char *mode = NULL;
		switch (schedule->mode) {
		case LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL:
		    mode = "sequential";
		    break;
		case LMAP_SCHEDULE_EXEC_MODE_PARALLEL:
		    mode = "parallel";
		    break;
		case LMAP_SCHEDULE_EXEC_MODE_PIPELINED:
		    mode = "pipelined";
		    break;
		}
		render_leaf(js, "execution-mode", mode);
	    }
	    render_tags(schedule->tags, "tag", js);
	    render_tags(schedule->suppression_tags, "suppression-tag", js);
	}
	if (what & RENDER_CONFIG_FALSE) {
	    char *state = NULL;
	    switch (schedule->state) {
	    case LMAP_SCHEDULE_STATE_ENABLED:
		state = "enabled";
		break;
	    case LMAP_SCHEDULE_STATE_DISABLED:
		state = "disabled";
		break;
	    case LMAP_SCHEDULE_STATE_RUNNING:
		state = "running";
	    break;
	    case LMAP_SCHEDULE_STATE_SUPPRESSED:
		state = "suppressed";
		break;
	    }
	    render_leaf(js, "state", state);

	    render_leaf_uint64(js, "storage", schedule->storage);
	    render_leaf_uint32(js, "invocations", schedule->cnt_invocations);
	    render_leaf_uint32(js, "suppressions", schedule->cnt_suppressions);
	    render_leaf_uint32(js, "overlaps", schedule->cnt_overlaps);
	    render_leaf_uint32(js, "failures", schedule->cnt_failures);

	    if (schedule->last_invocation)
		render_leaf_datetime(js, "last-invocation", &schedule->last_invocation);
	}
	render_actions(schedule->actions, js, what);
    }
    return 0;
}

static int
render_suppressions(struct supp *supp, json_object *jobj, int what)
{
    json_object *jss, *ja, *js;

    if (!supp)
	return 0;
    if (!jobj)
	return -1;

    jss = json_object_new_object();
    if (!jss)
	return -1;
    json_object_object_add(jobj, "suppressions", jss);
    ja = json_object_new_array();
    if (!ja)
	return -1;
    json_object_object_add(jss, "suppression", ja);

    for (; supp; supp = supp->next) {
	if (!supp->name)
	    continue;

	js = json_object_new_object();
	if (!js)
	    return -1;
	json_object_array_add(ja, js);

	render_leaf(js, "name", supp->name);
	if (what & RENDER_CONFIG_TRUE) {
	    render_leaf(js, "start", supp->start);
	    render_leaf(js, "end", supp->end);
	    render_tags(supp->match, "match", js);
	    if (supp->flags & LMAP_SUPP_FLAG_STOP_RUNNING_SET)
		render_leaf_boolean(js, "stop-running", supp->stop_running);
	}
	if (what & RENDER_CONFIG_FALSE) {
	    char *state = NULL;
	    switch (supp->state) {
	    case LMAP_SUPP_STATE_ENABLED:
		state = "enabled";
		break;
	    case LMAP_SUPP_STATE_DISABLED:
		state = "disabled";
		break;
	    case LMAP_SUPP_STATE_ACTIVE:
		state = "active";
		break;
	    }
	    render_leaf(js, "state", state);
	}
    }
    return 0;
}

static int
render_tasks(struct task *task, json_object *jobj, int what)
{
    json_object *jtasks, *ja, *jt;

    if (!task || !jobj)
	return 0;

    jtasks = json_object_new_object();
    if (!jtasks)
	return -1;
    json_object_object_add(jobj, "tasks", jtasks);
    ja = json_object_new_array();
    if (!ja)
	return -1;
    json_object_object_add(jtasks, "task", ja);

    for (; task; task = task->next) {
	if (!task->name)
	    continue;

	jt = json_object_new_object();
	if (!jt)
	    return -1;
	json_object_array_add(ja, jt);

	render_leaf(jt, "name", task->name);
	render_registries(task->registries, jt);
	if (what & RENDER_CONFIG_FALSE)
	    render_leaf(jt, "version", task->version);
	render_leaf(jt, "program", task->program);
	if (what & RENDER_CONFIG_TRUE) {
	    render_options(task->options, jt);
	    render_tags(task->tags, "tag", jt);
	}
    }

    return 0;
}

static int
render_capabilities(struct capability *capability, json_object *jobj, int what)
{
    json_object *jo;

    assert(jobj);

    if (!capability
        || !(what & RENDER_CONFIG_FALSE)
	|| (!capability->version && !capability->tags && !capability->tasks))
	return 0;

    jo = json_object_new_object();
    if (!jo)
	return -1;
    json_object_object_add(jobj, "capabilities", jo);
    render_leaf(jo, "version", capability->version);
    render_tags(capability->tags, "tag", jo);

    return render_tasks(capability->tasks, jo, what);
}

static int
render_events(struct event *event, json_object *jobj, int what)
{
    json_object *jevents, *ja, *je, *jsub;

    assert (jobj);

    if (!event)
	return 0;

    jevents = json_object_new_object();
    if (!jevents)
	return -1;
    json_object_object_add(jobj, "events", jevents);
    ja = json_object_new_array();
    if (!ja)
	return -1;
    json_object_object_add(jevents, "event", ja);

    for (; event; event = event->next) {
	if (!event->name)
	    continue;

	je = json_object_new_object();
	if (!je)
	    return -1;
	json_object_array_add(ja, je);

	render_leaf(je, "name", event->name);
	if (what & RENDER_CONFIG_TRUE) {
	    if (event->flags & LMAP_EVENT_FLAG_RANDOM_SPREAD_SET)
		render_leaf_int32(je, "random-spread", event->random_spread);
	    if (event->flags & LMAP_EVENT_FLAG_CYCLE_INTERVAL_SET)
		render_leaf_int32(je, "cycle-interval", event->cycle_interval);
	    switch (event->type) {
	    case LMAP_EVENT_TYPE_PERIODIC:
		if (!(jsub = json_object_new_object()))
		    return -1;
		json_object_object_add(je, "periodic", jsub);
		if (event->flags & LMAP_EVENT_FLAG_INTERVAL_SET)
		    render_leaf_uint32(jsub, "interval", event->interval);
		if (event->flags & LMAP_EVENT_FLAG_START_SET)
		    render_leaf_datetime(jsub, "start", &event->start);
		if (event->flags & LMAP_EVENT_FLAG_END_SET)
		    render_leaf_datetime(jsub, "end", &event->end);
		break;
	    case LMAP_EVENT_TYPE_CALENDAR:
		if (!(jsub = json_object_new_object()))
		    return -1;
		json_object_object_add(je, "calendar", jsub);
		if (event->months)
		    render_leaf_months(jsub, "month", event->months);
		if (event->days_of_month)
		    render_leaf_days_of_month(jsub, "day-of-month", event->days_of_month);
		if (event->days_of_week)
		    render_leaf_days_of_week(jsub, "day-of-week", event->days_of_week);
		if (event->hours)
		    render_leaf_hours(jsub, "hour", event->hours);
		if (event->minutes)
		    render_leaf_minsecs(jsub, "minute", event->minutes);
		if (event->seconds)
		    render_leaf_minsecs(jsub, "second", event->seconds);
		if (event->flags & LMAP_EVENT_FLAG_TIMEZONE_OFFSET_SET) {
		    char buf[42];
		    char c = (event->timezone_offset < 0) ? '-' : '+';
		    int16_t offset = event->timezone_offset;
		    offset = (offset < 0) ? -1 * offset : offset;
		    snprintf(buf, sizeof(buf), "%c%02d:%02d",
			     c, offset / 60, offset % 60);
		    render_leaf(jsub, "timezone-offset", buf);
		}
		if (event->flags & LMAP_EVENT_FLAG_START_SET)
		    render_leaf_datetime(jsub, "start", &event->start);
		if (event->flags & LMAP_EVENT_FLAG_END_SET)
		    render_leaf_datetime(jsub, "end", &event->end);
		break;
	    case LMAP_EVENT_TYPE_ONE_OFF:
		if (!(jsub = json_object_new_object()))
		    return -1;
		json_object_object_add(je, "one-off", jsub);
		if (event->flags & LMAP_EVENT_FLAG_START_SET)
		    render_leaf_datetime(jsub, "time", &event->start);
		break;
	    case LMAP_EVENT_TYPE_STARTUP:
		render_empty_leaf(je, "startup");
		break;
	    case LMAP_EVENT_TYPE_IMMEDIATE:
		render_empty_leaf(je, "immediate");
		break;
	    case LMAP_EVENT_TYPE_CONTROLLER_LOST:
		render_empty_leaf(je, "controller-lost");
		break;
	    case LMAP_EVENT_TYPE_CONTROLLER_CONNECTED:
		render_empty_leaf(je, "controller-connected");
		break;
	    }
	}
    }

    return 0;
}

/**
 * @brief Returns a JSON rendering of the lmap config or state
 *
 * @param lmap The pointer to the lmap config to be rendered.
 * @param what PARSER_CONFIG_* to remove state fields from config
 * @return An JSON document as a string that must be freed by the
 *         caller or NULL on error
 */
static char *
render_control(struct lmap *lmap, int what)
{
    json_object *docobj, *rootobj;
    char *result = NULL;
    const char *doc;

    assert(lmap);

    /* FIXME: we cannot differentiate state from config except by context
     *        or by looking for a CONFIG:FALSE field... */

    if (!(rootobj = json_object_new_object()))
	return NULL;
    if (!(docobj = json_object_new_object()))
	goto err_exit;
    json_object_object_add(rootobj, LMAPC_JSON_NAMESPACE ":lmap", docobj);

    if (!render_capabilities(lmap->capabilities, docobj, what)
		&& !render_agent(lmap->agent, docobj, what)
		&& !render_tasks(lmap->tasks, docobj, what)
		&& !render_schedules(lmap->schedules, docobj, what)
		&& !render_suppressions(lmap->supps, docobj, what)
		&& !render_events(lmap->events, docobj, what)) {
	doc = json_object_to_json_string_ext(rootobj, JSON_C_TO_STRING_PRETTY);
	if (doc)
	    result = strdup(doc);
    }

err_exit:
    json_object_put(rootobj);

    return result;
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
    return render_control(lmap, RENDER_CONFIG_TRUE);
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
    return render_control(lmap, RENDER_CONFIG_TRUE | RENDER_CONFIG_FALSE);
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

#endif /* ifdef WITH_JSON */
