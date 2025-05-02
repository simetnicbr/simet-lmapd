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

#include <stdlib.h>
#include <stdio.h>
#include <check.h>
#include <inttypes.h>

#include "lmap.h"
#include "utils.h"
#include "xml-io.h"
#include "json-io.h"
#include "csv.h"

static const char testdata_dir[] = "test/data";

static char last_error_msg[1024];

static void vlog(int level, const char *func, const char *format, va_list args)
{
    (void) level;
    (void) func;
    (void) vsnprintf(last_error_msg, sizeof(last_error_msg), format, args);
#if 0
    fprintf(stderr, "%s\n", last_error_msg);
#endif
}

void setup(void)
{
    /* start from a clean (global) state */
    last_error_msg[0] = '\0';
}

void teardown(void)
{

}

START_TEST(test_lmap_agent)
{
    const char *uuid = "550e8400-e29b-41d4-a716-446655440000";
    struct agent *agent;

    agent = lmap_agent_new();
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 1);
    ck_assert_int_eq(lmap_agent_set_agent_id(agent, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal uuid value 'foo'");
    ck_assert_int_eq(lmap_agent_set_agent_id(agent, uuid), 0);
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 1);
    ck_assert_str_eq(agent->agent_id, uuid);
    ck_assert_int_eq(lmap_agent_set_report_agent_id(agent, "true"), 0);
    ck_assert_int_eq(lmap_agent_set_report_agent_id(agent, "no"), -1);
    ck_assert_str_eq(last_error_msg, "illegal boolean value 'no'");
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 1);
    ck_assert_int_eq(lmap_agent_set_agent_id(agent, NULL), 0);
    ck_assert_ptr_eq(agent->agent_id, NULL);
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 0);
    ck_assert_str_eq(last_error_msg, "report-agent-id requires an agent-id");
    ck_assert_int_eq(lmap_agent_set_agent_id(agent, uuid), 0);
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 1);
    ck_assert_int_eq(lmap_agent_set_report_measurement_point(agent, "true"), 0);
    ck_assert_int_eq(lmap_agent_set_report_measurement_point(agent, "yes"), -1);
    ck_assert_str_eq(last_error_msg, "illegal boolean value 'yes'");
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 0);
    ck_assert_str_eq(last_error_msg, "report-measurement-point requires a measurement-point");
    ck_assert_int_eq(lmap_agent_set_measurement_point(agent, "bar"), 0);
    ck_assert_int_eq(lmap_agent_valid(NULL, agent), 1);
    ck_assert_int_eq(lmap_agent_set_controller_timeout(agent, "42"), 0);
    ck_assert_int_eq(agent->controller_timeout, 42);
    ck_assert_int_eq(lmap_agent_set_group_id(agent, "foo"), 0);
    ck_assert_int_eq(lmap_agent_set_group_id(agent, "bar"), 0);
    ck_assert_str_eq(agent->group_id, "bar");
    lmap_agent_free(agent);
}
END_TEST

START_TEST(test_lmap_localtime_handling)
{
    struct agent *agent = lmap_agent_new();

    setenv("TZ", "UTC+00:00", 1);
    tzset();
    ck_assert_int_eq(timezone, 0);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T14:48:19-01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119299);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T16:48:23+01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119303);

    setenv("TZ", "XYZ-11:15", 1);
    tzset();
    ck_assert_int_eq(timezone, -40500);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T14:48:19-01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119299);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T16:48:23+01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119303);

    setenv("TZ", "XYZ+03:45", 1);
    tzset();
    ck_assert_int_eq(timezone, 13500);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T14:48:19-01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119299);
    ck_assert_int_eq(lmap_agent_set_last_started(agent, "2016-02-10T16:48:23+01:00"), 0);
    ck_assert_int_eq(agent->last_started, 1455119303);

    lmap_agent_free(agent);
}
END_TEST

START_TEST(test_lmap_option)
{
    struct option *option;

    option = lmap_option_new();
    ck_assert_int_eq(lmap_option_valid(NULL, option), 0);
    ck_assert_str_eq(last_error_msg, "option requires an id");
    ck_assert_int_eq(lmap_option_set_value(option, "bar"), 0);
    ck_assert_int_eq(lmap_option_valid(NULL, option), 0);
    ck_assert_str_eq(last_error_msg, "option requires an id");
    ck_assert_int_eq(lmap_option_set_name(option, "foo"), 0);
    ck_assert_int_eq(lmap_option_valid(NULL, option), 0);
    ck_assert_str_eq(last_error_msg, "option requires an id");
    ck_assert_int_eq(lmap_option_set_id(option, ""), -1);
    ck_assert_str_eq(last_error_msg, "illegal lmap-identifier value ''");
    ck_assert_int_eq(lmap_option_set_id(option, "_.-"), 0);
    ck_assert_int_eq(lmap_option_valid(NULL, option), 1);
    ck_assert_int_eq(lmap_option_set_id(option, NULL), 0);
    ck_assert_int_eq(lmap_option_valid(NULL, option), 0);
    ck_assert_str_eq(last_error_msg, "option requires an id");
    lmap_option_free(option);
}
END_TEST

START_TEST(test_lmap_registry)
{
    struct registry *registry;

    registry = lmap_registry_new();
    ck_assert_int_eq(lmap_registry_valid(NULL, registry), 0);
    ck_assert_str_eq(last_error_msg, "registry requires a uri");
    ck_assert_int_eq(lmap_registry_set_uri(registry, "uri:example"), 0);
    ck_assert_int_eq(lmap_registry_valid(NULL, registry), 1);
    ck_assert_int_eq(lmap_registry_add_role(registry, "foo"), 0);
    ck_assert_int_eq(lmap_registry_valid(NULL, registry), 1);
    ck_assert_int_eq(lmap_registry_add_role(registry, "bar"), 0);
    ck_assert_int_eq(lmap_registry_valid(NULL, registry), 1);
    lmap_registry_free(registry);
}
END_TEST

START_TEST(test_lmap_tag)
{
    struct tag *tag = lmap_tag_new();
    ck_assert_int_eq(lmap_tag_valid(NULL, tag), 0);
    ck_assert_str_eq(last_error_msg, "tag requires a value");
    ck_assert_int_eq(lmap_tag_set_tag(tag, "bar"), 0);
    ck_assert_int_eq(lmap_tag_valid(NULL, tag), 1);
    ck_assert_int_eq(lmap_tag_set_tag(tag, ""), -1);
    ck_assert_str_eq(last_error_msg, "illegal zero-length tag ''");
    ck_assert_int_eq(lmap_tag_set_tag(tag, NULL), 0);
    ck_assert_int_eq(lmap_tag_valid(NULL, tag), 0);
    ck_assert_str_eq(last_error_msg, "tag requires a value");
    lmap_tag_free(tag);
}
END_TEST

START_TEST(test_lmap_suppression)
{
    const char *now = "now";
    const char *tomorrow = "tomorrow";
    int c;
    struct tag *tag;
    struct supp *supp;

    supp = lmap_supp_new();
    ck_assert_int_eq(lmap_supp_valid(NULL, supp), 0);
    ck_assert_str_eq(last_error_msg, "suppression requires a name");
    ck_assert_int_eq(lmap_supp_set_name(supp, "name"), 0);
    ck_assert_int_eq(lmap_supp_valid(NULL, supp), 1);
    ck_assert_int_eq(lmap_supp_set_start(supp, now), 0);
    ck_assert_str_eq(supp->start, now);
    ck_assert_int_eq(lmap_supp_valid(NULL, supp), 0);
    ck_assert_str_eq(last_error_msg, "suppression 'name' refers to undefined start event 'now'");
    ck_assert_int_eq(lmap_supp_set_end(supp, tomorrow), 0);
    ck_assert_int_eq(lmap_supp_valid(NULL, supp), 0);
    ck_assert_str_eq(last_error_msg, "suppression 'name' refers to undefined end event 'tomorrow'");
    ck_assert_str_eq(supp->end, tomorrow);
    ck_assert_int_eq(lmap_supp_set_stop_running(supp, "true"), 0);
    ck_assert_int_eq(supp->stop_running, 1);
    ck_assert_int_eq(lmap_supp_set_stop_running(supp, "random"), -1);
    ck_assert_str_eq(last_error_msg, "illegal boolean value 'random'");
    ck_assert_int_eq(supp->stop_running, 1);
    ck_assert_int_eq(lmap_supp_set_stop_running(supp, "false"), 0);
    ck_assert_int_eq(supp->stop_running, 0);
    ck_assert_int_eq(lmap_supp_valid(NULL, supp), 0);
    ck_assert_int_eq(lmap_supp_add_match(supp, "a"), 0);
    ck_assert_int_eq(lmap_supp_add_match(supp, "b"), 0);
    ck_assert_int_eq(lmap_supp_add_match(supp, "b"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'b'");
    ck_assert_int_eq(lmap_supp_add_match(supp, "a"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'a'");
    ck_assert_int_eq(lmap_supp_add_match(supp, "x"), 0);
    for (c = 0, tag = supp->match; tag; c++, tag = tag->next) {
    }
    ck_assert_int_eq(c, 3);
    lmap_supp_free(supp);
}
END_TEST

START_TEST(test_lmap_event)
{
    const char *date1 = "2016-03-14T07:45:19+01:00";
    const char *date2 = "2016-03-14T07:45:22+01:00";

    struct event *event = lmap_event_new();
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event requires a type");
    ck_assert_int_eq(lmap_event_set_type(event, "startup"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event requires a name");
    ck_assert_int_eq(lmap_event_set_name(event, "bang"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    ck_assert_int_eq(lmap_event_set_type(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "unknown event type 'foo'");
    ck_assert_int_eq(lmap_event_set_name(event, NULL), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event requires a name");
    ck_assert_int_eq(lmap_event_set_name(event, "bang"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);

    ck_assert_int_eq(lmap_event_set_start(event, date1), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    ck_assert_int_eq(lmap_event_set_end(event, date2), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);

    ck_assert_int_eq(lmap_event_set_start(event, date2), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    ck_assert_int_eq(lmap_event_set_end(event, date1), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'bang' ends before it starts");

    lmap_event_free(event);
}
END_TEST

START_TEST(test_lmap_event_periodic)
{
    struct event *event = lmap_event_new();
    ck_assert_int_eq(lmap_event_set_name(event, "periodic"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'periodic' requires a type");
    ck_assert_int_eq(lmap_event_set_type(event, "periodic"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'periodic' requires an interval");
    ck_assert_int_eq(lmap_event_set_interval(event, "42"), 0);
    ck_assert_int_eq(event->interval, 42);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    ck_assert_int_eq(lmap_event_set_start(event, "2016-02-10T16:48:19+01:00"), 0);
    ck_assert_int_eq(event->start, 1455119299);
    ck_assert_int_eq(lmap_event_set_end(event, "2016-02-10T16:48:23+01:00"), 0);
    ck_assert_int_eq(event->end, 1455119303);
    lmap_event_free(event);
}
END_TEST

START_TEST(test_lmap_event_calendar)
{
    struct event *event = lmap_event_new();
    ck_assert_int_eq(lmap_event_set_name(event, "calendar"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a type");
    ck_assert_int_eq(lmap_event_set_type(event, "calendar"), 0);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a second");
    ck_assert_int_eq(lmap_event_add_second(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal second value 'foo'");
    ck_assert_int_eq(lmap_event_add_second(event, "60"), -1);
    ck_assert_str_eq(last_error_msg, "illegal second value '60'");
    ck_assert_int_eq(lmap_event_add_second(event, "*"), 0);
    ck_assert_int_eq(event->seconds - UINT64_MAX, 0);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a minute");
    ck_assert_int_eq(lmap_event_add_minute(event, "0"), 0);
    ck_assert_int_eq(lmap_event_add_minute(event, "1"), 0);
    ck_assert_int_eq(lmap_event_add_minute(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal minute value 'foo'");
    ck_assert_int_eq(lmap_event_add_minute(event, "60"), -1);
    ck_assert_str_eq(last_error_msg, "illegal minute value '60'");
    ck_assert_int_eq(event->minutes, 3);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires an hour");
    ck_assert_int_eq(lmap_event_add_hour(event, "0"), 0);
    ck_assert_int_eq(lmap_event_add_hour(event, "1"), 0);
    ck_assert_int_eq(lmap_event_add_hour(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal hour value 'foo'");
    ck_assert_int_eq(lmap_event_add_hour(event, "24"), -1);
    ck_assert_str_eq(last_error_msg, "illegal hour value '24'");
    ck_assert_int_eq(event->hours, 3);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a day of week");
    ck_assert_int_eq(lmap_event_add_day_of_week(event, "monday"), 0);
    ck_assert_int_eq(lmap_event_add_day_of_week(event, "wednesday"), 0);
    ck_assert_int_eq(lmap_event_add_day_of_week(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal day of week value 'foo'");
    ck_assert_int_eq(event->days_of_week, 5);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a day of month");
    ck_assert_int_eq(lmap_event_add_day_of_month(event, "1"), 0);
    ck_assert_int_eq(lmap_event_add_day_of_month(event, "2"), 0);
    ck_assert_int_eq(lmap_event_add_day_of_month(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal day of month value 'foo'");
    ck_assert_int_eq(lmap_event_add_day_of_month(event, "32"), -1);
    ck_assert_str_eq(last_error_msg, "illegal day of month value '32'");
    ck_assert_int_eq(lmap_event_add_day_of_month(event, "0"), -1);
    ck_assert_str_eq(last_error_msg, "illegal day of month value '0'");
    ck_assert_int_eq(event->days_of_month, 6);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'calendar' requires a month");
    ck_assert_int_eq(lmap_event_add_month(event, "february"), 0);
    ck_assert_int_eq(lmap_event_add_month(event, "march"), 0);
    ck_assert_int_eq(lmap_event_add_month(event, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal month value 'foo'");
    ck_assert_int_eq(event->months, 6);

    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    ck_assert_int_eq(lmap_event_set_timezone_offset(event, "+01:11"), 0);
    ck_assert_int_eq(event->timezone_offset, 1*60+11);
    ck_assert_int_eq(lmap_event_set_timezone_offset(event, "-00:42"), 0);
    ck_assert_int_eq(event->timezone_offset, -42);
    ck_assert_int_eq(lmap_event_set_start(event, "2016-02-10T16:48:19+01:00"), 0);
    ck_assert_int_eq(event->start, 1455119299);
    ck_assert_int_eq(lmap_event_set_end(event, "2016-02-10T16:48:23+01:00"), 0);
    ck_assert_int_eq(event->end, 1455119303);
    lmap_event_free(event);
}
END_TEST

START_TEST(test_lmap_event_one_off)
{
    struct event *event = lmap_event_new();
    ck_assert_int_eq(lmap_event_set_name(event, "one-off"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'one-off' requires a type");
    ck_assert_int_eq(lmap_event_set_type(event, "one-off"), 0);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 0);
    ck_assert_str_eq(last_error_msg, "event 'one-off' requires a time");
    ck_assert_int_eq(lmap_event_set_start(event, "2016-02-10T16:48:19+01:00"), 0);

    ck_assert_int_eq(event->start, 1455119299);
    ck_assert_int_eq(lmap_event_set_start(event, "2016-02-10T14:48:23+00:00"), 0);
    ck_assert_int_eq(event->start, 1455115703);
    ck_assert_int_eq(lmap_event_set_start(event, "2016-02-10T14:48:23Z"), 0);
    ck_assert_int_eq(event->start, 1455115703);
    ck_assert_int_eq(lmap_event_valid(NULL, event), 1);
    lmap_event_free(event);
}
END_TEST

START_TEST(test_lmap_task)
{
    int c;
    struct tag *tag;
    struct task *task;
    struct registry *registry;
    struct option *option;

    task = lmap_task_new();
    ck_assert_int_eq(lmap_task_valid(NULL, task), 0);
    ck_assert_str_eq(last_error_msg, "task requires a program");
    ck_assert_int_eq(lmap_task_set_program(task, "noop"), 0);
    ck_assert_int_eq(lmap_task_valid(NULL, task), 0);
    ck_assert_str_eq(last_error_msg, "task requires a name");
    ck_assert_int_eq(lmap_task_set_name(task, "name"), 0);
    ck_assert_int_eq(lmap_task_valid(NULL, task), 1);
    ck_assert_int_eq(lmap_task_add_tag(task, "a"), 0);
    ck_assert_int_eq(lmap_task_add_tag(task, "b"), 0);
    ck_assert_int_eq(lmap_task_add_tag(task, "b"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'b'");
    ck_assert_int_eq(lmap_task_add_tag(task, "a"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'a'");
    ck_assert_int_eq(lmap_task_add_tag(task, "x"), 0);
    for (c = 0, tag = task->tags; tag; c++, tag = tag->next) {
    }
    ck_assert_int_eq(c, 3);

    registry = lmap_registry_new();
    ck_assert_int_eq(lmap_registry_set_uri(registry, "urn:example"), 0);
    ck_assert_int_eq(lmap_task_add_registry(task, registry), 0);
    ck_assert_int_eq(lmap_task_valid(NULL, task), 1);

    option = lmap_option_new();
    ck_assert_int_eq(lmap_option_set_id(option, "idx"), 0);
    ck_assert_int_eq(lmap_task_add_option(task, option), 0);
    ck_assert_int_eq(lmap_task_valid(NULL, task), 1);

    lmap_task_free(task);
}
END_TEST

START_TEST(test_lmap_schedule)
{
    int c;
    struct tag *tag;
    struct schedule *schedule;

    schedule = lmap_schedule_new();
    ck_assert_int_eq(lmap_schedule_valid(NULL, schedule), 0);
    ck_assert_str_eq(last_error_msg, "schedule requires a start event");
    ck_assert_int_eq(lmap_schedule_set_start(schedule, "now"), 0);
    ck_assert_int_eq(lmap_schedule_valid(NULL, schedule), 0);
    ck_assert_str_eq(last_error_msg, "schedule refers to undefined start event 'now'");
    ck_assert_int_eq(lmap_schedule_set_name(schedule, "name"), 0);
    ck_assert_int_eq(lmap_schedule_set_end(schedule, "tomorrow"), 0);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_END_SET,
		     LMAP_SCHEDULE_FLAG_END_SET);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET, 0);
    ck_assert_int_eq(lmap_schedule_set_duration(schedule, "1234"), 0);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET,
		     LMAP_SCHEDULE_FLAG_DURATION_SET);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_END_SET, 0);
    ck_assert_int_eq(lmap_schedule_set_end(schedule, "tomorrow"), 0);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_END_SET,
		     LMAP_SCHEDULE_FLAG_END_SET);
    ck_assert_int_eq(schedule->flags & LMAP_SCHEDULE_FLAG_DURATION_SET, 0);
    ck_assert_int_eq(schedule->mode, LMAP_SCHEDULE_EXEC_MODE_PIPELINED);
    ck_assert_int_eq(lmap_schedule_set_exec_mode(schedule, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "illegal execution mode 'foo'");
    ck_assert_int_eq(lmap_schedule_set_exec_mode(schedule, "sequential"), 0);
    ck_assert_int_eq(schedule->mode, LMAP_SCHEDULE_EXEC_MODE_SEQUENTIAL);
    ck_assert_int_eq(lmap_schedule_set_exec_mode(schedule, "parallel"), 0);
    ck_assert_int_eq(schedule->mode, LMAP_SCHEDULE_EXEC_MODE_PARALLEL);
    ck_assert_int_eq(lmap_schedule_set_exec_mode(schedule, "pipelined"), 0);
    ck_assert_int_eq(schedule->mode, LMAP_SCHEDULE_EXEC_MODE_PIPELINED);
    ck_assert_int_eq(lmap_schedule_add_tag(schedule, "a"), 0);
    ck_assert_int_eq(lmap_schedule_add_tag(schedule, "b"), 0);
    ck_assert_int_eq(lmap_schedule_add_tag(schedule, "b"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'b'");
    ck_assert_int_eq(lmap_schedule_add_tag(schedule, "a"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'a'");
    ck_assert_int_eq(lmap_schedule_add_tag(schedule, "x"), 0);
    for (c = 0, tag = schedule->tags; tag; c++, tag = tag->next) {
    }
    ck_assert_int_eq(c, 3);

    lmap_schedule_free(schedule);
}
END_TEST

START_TEST(test_lmap_action)
{
    int c;
    struct tag *tag;
    struct action *action;

    action = lmap_action_new();
    ck_assert_int_eq(lmap_action_valid(NULL, action), 0);
    ck_assert_str_eq(last_error_msg, "action requires a task");
    ck_assert_int_eq(lmap_action_set_task(action, "task"), 0);
    ck_assert_int_eq(lmap_action_valid(NULL, action), 0);
    ck_assert_str_eq(last_error_msg, "action refers to undefined task 'task'");
    ck_assert_int_eq(lmap_action_set_name(action, "name"), 0);
    ck_assert_int_eq(lmap_action_valid(NULL, action), 0);
    ck_assert_str_eq(last_error_msg, "action 'name' refers to undefined task 'task'");
    ck_assert_int_eq(lmap_action_add_destination(action, "nowhere"), 0);
    ck_assert_int_eq(lmap_action_valid(NULL, action), 0);
    ck_assert_str_eq(last_error_msg, "action 'name' refers to undefined destination 'nowhere'");
    ck_assert_int_eq(lmap_action_add_tag(action, "a"), 0);
    ck_assert_int_eq(lmap_action_add_tag(action, "b"), 0);
    ck_assert_int_eq(lmap_action_add_tag(action, "b"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'b'");
    ck_assert_int_eq(lmap_action_add_tag(action, "a"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'a'");
    ck_assert_int_eq(lmap_action_add_tag(action, "x"), 0);
    for (c = 0, tag = action->tags; tag; c++, tag = tag->next) {
    }
    ck_assert_int_eq(c, 3);

    lmap_action_free(action);
}
END_TEST

START_TEST(test_lmap_lmap)
{
    struct lmap *lmap;
    struct supp *supp_a, *supp_b;
    struct event *event_a, *event_b;

    lmap = lmap_new();
    ck_assert_ptr_ne(lmap, NULL);
    ck_assert_int_eq(lmap_valid(lmap), 1);

    supp_a = lmap_supp_new();
    supp_b = lmap_supp_new();
    ck_assert_int_eq(lmap_supp_set_name(supp_a, "abcde"), 0);

    ck_assert_int_eq(lmap_add_supp(lmap, supp_a), 0);
    ck_assert_int_eq(lmap_add_supp(lmap, supp_b), -1);
    ck_assert_int_eq(lmap_add_supp(lmap, supp_a), -1);

    event_a = lmap_event_new();
    event_b = lmap_event_new();
    ck_assert_int_eq(lmap_event_set_name(event_a, "bingo"), 0);

    ck_assert_int_eq(lmap_add_event(lmap, event_a), 0);
    ck_assert_int_eq(lmap_add_event(lmap, event_b), -1);
    ck_assert_int_eq(lmap_add_event(lmap, event_a), -1);

    lmap_event_free(event_b);
    lmap_supp_free(supp_b);
    lmap_free(lmap);
}
END_TEST

START_TEST(test_lmap_val)
{
    struct value *val = lmap_value_new();
    ck_assert_int_eq(lmap_value_valid(NULL, val), 0);
    ck_assert_str_eq(last_error_msg, "val requires a value");
    ck_assert_int_eq(lmap_value_set_value(val, "bar"), 0);
    ck_assert_int_eq(lmap_value_valid(NULL, val), 1);
    ck_assert_str_eq("bar", val->value);
    ck_assert_int_eq(lmap_value_set_value(val, NULL), 0);
    ck_assert_int_eq(lmap_value_valid(NULL, val), 0);
    ck_assert_str_eq(last_error_msg, "val requires a value");
    lmap_value_free(val);
}
END_TEST

START_TEST(test_lmap_row)
{
    int i;
    char *vals[] = { "foo", "bar", " b a z ", NULL };
    struct value *val;

    struct row *row = lmap_row_new();
    for (i = 0; vals[i]; i++) {
	val = lmap_value_new();
	lmap_value_set_value(val, vals[i]);
	ck_assert_int_eq(lmap_row_add_value(row, val), 0);
    }
    for (i = 0, val = row->values; vals[i]; i++, val = val->next) {
	ck_assert_str_eq(vals[i], val->value);
    }
    ck_assert_int_eq(lmap_row_valid(NULL, row), 1);
    lmap_row_free(row);
}
END_TEST

START_TEST(test_lmap_table)
{
    struct row *row;
    struct value *val;
    struct registry *reg;

    struct table *tab = lmap_table_new();

    /* an empty table is valid */
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 1);

    /* a table with only rows is valid */
    row = lmap_row_new();
    val = lmap_value_new();
    lmap_value_set_value(val, "42");
    lmap_row_add_value(row, val);
    lmap_table_add_row(tab, row);
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 1);

    /* the number of row contents and columns must match */
    lmap_table_add_column(tab, "column0");
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 1);
    lmap_table_add_column(tab, "column1");
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 0);
    val = lmap_value_new();
    lmap_value_set_value(val, "42");
    lmap_row_add_value(row, val); /* yeah. not nice */
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 1);

    /* only accept registry entries with an uri */
    reg = lmap_registry_new();
    lmap_registry_add_role(reg, "foo");
    ck_assert_int_ne(lmap_table_add_registry(tab, reg), 0);
    lmap_registry_set_uri(reg, "uri:example");
    ck_assert_int_eq(lmap_table_add_registry(tab, reg), 0);
    /* must refuse duplicate registry entries (same uri) */
    reg = lmap_registry_new();
    lmap_registry_add_role(reg, "bar");
    lmap_registry_set_uri(reg, "uri:example");
    ck_assert_int_ne(lmap_table_add_registry(tab, reg), 0);
    lmap_registry_free(reg);
    ck_assert_int_eq(lmap_table_valid(NULL, tab), 1);

    lmap_table_free(tab);
}
END_TEST

START_TEST(test_lmap_result)
{
    struct result *res;

    res = lmap_result_new();
    ck_assert_ptr_ne(res, NULL);
    lmap_result_set_schedule(res, "schedule");
    lmap_result_set_action(res, "action");
    ck_assert_int_eq(lmap_result_valid(NULL, res), 1);
    lmap_result_set_task(res, "task");
    ck_assert_int_eq(lmap_result_add_tag(res, "foo"), 0);
    ck_assert_int_eq(lmap_result_add_tag(res, "bar"), 0);
    ck_assert_int_eq(lmap_result_add_tag(res, "foo"), -1);
    ck_assert_str_eq(last_error_msg, "ignoring duplicate tag 'foo'");
    lmap_result_free(res);
}
END_TEST

/*
 * Roundtrip-style (parse-render-parse-render) test
 *
 * Document "b" should be exactly what the render would output
 */
typedef int (parser_func)(struct lmap *, const char *);
typedef char *(render_func)(struct lmap *);

static void xx_create_doc_and_roundtrip_test(struct lmap ** const plmap, char ** const pstr,
	const char * const doc, parser_func* const parse, render_func * const render)
{
    char *str_a;
    struct lmap *lmap_a = NULL;

    lmap_a = lmap_new();
    ck_assert_ptr_ne(lmap_a, NULL);
    ck_assert_int_eq((* parse)(lmap_a, doc), 0);
    ck_assert_str_eq(last_error_msg, "");

    str_a = (* render)(lmap_a);
    ck_assert_ptr_ne(str_a, NULL);
    ck_assert_str_eq(last_error_msg, "");

    if (plmap)
	(*plmap) = lmap_a;
    else
	lmap_free(lmap_a);

    if (pstr)
	(*pstr) = str_a;
    else
	free(str_a);
}

static void xx_test_roundtrip(const char *doc1_a, const char *doc1_b,
		const char *doc2_a, const char *doc2_b,
		parser_func * const parse1, render_func * const render1,
		parser_func * const parse2, render_func * const render2)
{
    char *str1_a = NULL, *str1_c = NULL, *str1_d = NULL;
    char *str2_a = NULL, *str2_c = NULL, *str2_d = NULL;
    struct lmap *lmap1_a = NULL, *lmap1_b = NULL;
    struct lmap *lmap2_a = NULL, *lmap2_b = NULL;

    xx_create_doc_and_roundtrip_test(&lmap1_a, &str1_a, doc1_a, parse1, render1);
    xx_create_doc_and_roundtrip_test(&lmap1_b, &str1_c, str1_a, parse1, render1);
    ck_assert_str_eq(str1_a, str1_c);
    ck_assert_str_eq(str1_c, doc1_b);

    xx_create_doc_and_roundtrip_test(&lmap2_a, &str2_a, doc2_a, parse2, render2);
    xx_create_doc_and_roundtrip_test(&lmap2_b, &str2_c, str2_a, parse2, render2);
    ck_assert_str_eq(str2_a, str2_c);
    ck_assert_str_eq(str2_c, doc2_b);

    /* cross-check data model contents */
    str1_d = (* render1)(lmap2_a);
    ck_assert_ptr_ne(str1_d, NULL);
    ck_assert_str_eq(str1_a, str1_d);
    ck_assert_str_eq(last_error_msg, "");

    str2_d = (* render2)(lmap1_a);
    ck_assert_ptr_ne(str2_d, NULL);
    ck_assert_str_eq(str2_a, str2_d);
    ck_assert_str_eq(last_error_msg, "");

    lmap_free(lmap1_a); lmap_free(lmap1_b); lmap_free(lmap2_a); lmap_free(lmap2_b);
    free(str1_a); free(str1_c); free(str1_d);
    free(str2_a); free(str2_c); free(str2_d);
}

static void xx_test_roundtrip_config(const char *xml_a, const char *xml_b,
			      const char *json_a, const char *json_b)
{
    xx_test_roundtrip(xml_a, xml_b, json_a, json_b,
	lmap_xml_parse_config_string, lmap_xml_render_config,
	lmap_json_parse_config_string, lmap_json_render_config);
}

static void xx_test_roundtrip_state(const char *xml_a, const char *xml_b,
			      const char *json_a, const char *json_b)
{
    xx_test_roundtrip(xml_a, xml_b, json_a, json_b,
	lmap_xml_parse_state_string, lmap_xml_render_state,
	lmap_json_parse_state_string, lmap_json_render_state);
}

static void xx_test_roundtrip_report(const char *xml_a, const char *xml_b,
			      const char *json_a, const char *json_b)
{
    xx_test_roundtrip(xml_a, xml_b, json_a, json_b,
	lmap_xml_parse_report_string, lmap_xml_render_report,
	lmap_json_parse_report_string, lmap_json_render_report);
}

START_TEST(test_parser_config_agent)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:agent>"
        "      <lmapc:report-agent-id>true</lmapc:report-agent-id>"
        "      <lmapc:report-group-id>false</lmapc:report-group-id>"
        "      <lmapc:report-measurement-point>false</lmapc:report-measurement-point>"
        "      <lmapc:controller-timeout>42</lmapc:controller-timeout>"
        "    </lmapc:agent>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:agent>\n"
        "      <lmapc:report-agent-id>true</lmapc:report-agent-id>\n"
        "      <lmapc:report-group-id>false</lmapc:report-group-id>\n"
        "      <lmapc:report-measurement-point>false</lmapc:report-measurement-point>\n"
        "      <lmapc:controller-timeout>42</lmapc:controller-timeout>\n"
        "    </lmapc:agent>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\"ietf-lmap-control:lmap\":{\"agent\":{\"report-agent-id\":true,\"report-group-id\":false,\n"
	"\"report-measurement-point\": false,\n\n \"controller-timeout\": 42\n  } } }\n   \n";
    const char *jx =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"agent\":{\n"
	"      \"report-agent-id\":true,\n"
	"      \"report-group-id\":false,\n"
	"      \"report-measurement-point\":false,\n"
	"      \"controller-timeout\":42\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, jx);
}
END_TEST

START_TEST(test_parser_config_suppressions)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:suppressions>"
        "      <lmapc:suppression>"
        "       <lmapc:name>foo</lmapc:name>"
        "       <name>bar</name>"
        "       <x:name>baz</x:name>"
        "       <lmapc:match>red</lmapc:match>"
        "       <lmapc:match>blue</lmapc:match>"
        "       <lmapc:stop-running>true</lmapc:stop-running>"
        "      </lmapc:suppression>"
        "    </lmapc:suppressions>"
        " </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:suppressions>\n"
        "      <lmapc:suppression>\n"
        "        <lmapc:name>foo</lmapc:name>\n"
        "        <lmapc:match>red</lmapc:match>\n"
        "        <lmapc:match>blue</lmapc:match>\n"
        "        <lmapc:stop-running>true</lmapc:stop-running>\n"
        "      </lmapc:suppression>\n"
        "    </lmapc:suppressions>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"suppressions\":{\n"
	"      \"suppression\":[\n"
	"        {\n"
	"          \"name\":\"foo\",\n"
	"          \"match\":[\n"
	"            \"red\",\n"
	"            \"blue\"\n"
	"          ],\n"
	"          \"stop-running\":true\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_tasks)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:tasks>"
        "      <lmapc:task>"
        "        <lmapc:name>foo</lmapc:name>"
        "        <name>bar</name>"
        "        <x:name>baz</x:name>"
	"        <lmapc:function>"
	"          <lmapc:uri>urn:example</lmapc:uri>"
	"          <lmapc:role>client</lmapc:role>"
	"          <lmapc:role>server</lmapc:role>"
	"        </lmapc:function>"
	"        <lmapc:program>noop</lmapc:program>"
	"        <lmapc:option>"
	"          <lmapc:id>numeric</lmapc:id>"
	"          <lmapc:name>-n</lmapc:name>"
	"        </lmapc:option>"
	"        <lmapc:option>"
	"          <lmapc:id>target</lmapc:id>"
	"          <lmapc:value>www.example.com</lmapc:value>"
	"        </lmapc:option>"
        "        <lmapc:tag>red</lmapc:tag>"
        "        <lmapc:tag>blue</lmapc:tag>"
        "      </lmapc:task>"
        "    </lmapc:tasks>"
        " </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:tasks>\n"
        "      <lmapc:task>\n"
        "        <lmapc:name>foo</lmapc:name>\n"
	"        <lmapc:function>\n"
	"          <lmapc:uri>urn:example</lmapc:uri>\n"
	"          <lmapc:role>client</lmapc:role>\n"
	"          <lmapc:role>server</lmapc:role>\n"
	"        </lmapc:function>\n"
        "        <lmapc:program>noop</lmapc:program>\n"
	"        <lmapc:option>\n"
	"          <lmapc:id>numeric</lmapc:id>\n"
	"          <lmapc:name>-n</lmapc:name>\n"
	"        </lmapc:option>\n"
	"        <lmapc:option>\n"
	"          <lmapc:id>target</lmapc:id>\n"
	"          <lmapc:value>www.example.com</lmapc:value>\n"
	"        </lmapc:option>\n"
        "        <lmapc:tag>red</lmapc:tag>\n"
        "        <lmapc:tag>blue</lmapc:tag>\n"
        "      </lmapc:task>\n"
        "    </lmapc:tasks>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"tasks\":{\n"
	"      \"task\":[\n"
	"        {\n"
	"          \"name\":\"foo\",\n"
	"          \"function\":[\n"
	"            {\n"
	"              \"uri\":\"urn:example\",\n"
	"              \"role\":[\n"
	"                \"client\",\n"
	"                \"server\"\n"
	"              ]\n"
	"            }\n"
	"          ],\n"
	"          \"program\":\"noop\",\n"
	"          \"option\":[\n"
	"            {\n"
	"              \"id\":\"numeric\",\n"
	"              \"name\":\"-n\"\n"
	"            },\n"
	"            {\n"
	"              \"id\":\"target\",\n"
	"              \"value\":\"www.example.com\"\n"
	"            }\n"
	"          ],\n"
	"          \"tag\":[\n"
	"            \"red\",\n"
	"            \"blue\"\n"
	"          ]\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_events)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>foo</lmapc:name>"
        "        <name>bar</name>"
        "        <x:name>baz</x:name>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>periodic</lmapc:name>"
        "        <lmapc:random-spread>300000</lmapc:random-spread>"
        "        <lmapc:periodic>"
        "          <lmapc:interval>4321</lmapc:interval>"
        "          <lmapc:start>2015-02-01T17:44:21+02:00</lmapc:start>"
        "          <lmapc:end>2015-03-01T00:00:00+00:00</lmapc:end>"
        "        </lmapc:periodic>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>once</lmapc:name>"
        "        <lmapc:one-off>"
        "          <lmapc:time>2015-02-01T17:44:21+02:00</lmapc:time>"
        "        </lmapc:one-off>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>startup</lmapc:name>"
        "        <lmapc:startup/>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>immediate</lmapc:name>"
        "        <lmapc:immediate/>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>controller-lost</lmapc:name>"
        "        <lmapc:controller-lost/>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>controller-connected</lmapc:name>"
        "        <lmapc:controller-connected/>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>foo</lmapc:name>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>periodic</lmapc:name>\n"
        "        <lmapc:random-spread>300000</lmapc:random-spread>\n"
        "        <lmapc:periodic>\n"
        "          <lmapc:interval>4321</lmapc:interval>\n"
        "          <lmapc:start>2015-02-01T15:44:21+00:00</lmapc:start>\n"
        "          <lmapc:end>2015-03-01T00:00:00+00:00</lmapc:end>\n"
        "        </lmapc:periodic>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>once</lmapc:name>\n"
        "        <lmapc:one-off>\n"
        "          <lmapc:time>2015-02-01T15:44:21+00:00</lmapc:time>\n"
        "        </lmapc:one-off>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>startup</lmapc:name>\n"
        "        <lmapc:startup/>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>immediate</lmapc:name>\n"
        "        <lmapc:immediate/>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>controller-lost</lmapc:name>\n"
        "        <lmapc:controller-lost/>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>controller-connected</lmapc:name>\n"
        "        <lmapc:controller-connected/>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"events\":{\n"
	"      \"event\":[\n"
	"        {\n"
	"          \"name\":\"foo\"\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"periodic\",\n"
	"          \"random-spread\":300000,\n"
	"          \"periodic\":{\n"
	"            \"interval\":4321,\n"
	"            \"start\":\"2015-02-01T15:44:21+00:00\",\n"
	"            \"end\":\"2015-03-01T00:00:00+00:00\"\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"once\",\n"
	"          \"one-off\":{\n"
	"            \"time\":\"2015-02-01T15:44:21+00:00\"\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"startup\",\n"
	"          \"startup\":[\n"
	"            null\n"
	"          ]\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"immediate\",\n"
	"          \"immediate\":[\n"
	"            null\n"
	"          ]\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"controller-lost\",\n"
	"          \"controller-lost\":[\n"
	"            null\n"
	"          ]\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"controller-connected\",\n"
	"          \"controller-connected\":[\n"
	"            null\n"
	"          ]\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_events_calendar0)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>monthly</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-month>1</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>0</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "          <lmapc:timezone-offset>+00:00</lmapc:timezone-offset>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>weekly</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>"
        "          <lmapc:day-of-week>monday</lmapc:day-of-week>"
        "          <lmapc:hour>0</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "          <lmapc:timezone-offset>-01:00</lmapc:timezone-offset>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>monthly</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>1</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>0</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "          <lmapc:timezone-offset>+00:00</lmapc:timezone-offset>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>weekly</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>monday</lmapc:day-of-week>\n"
        "          <lmapc:hour>0</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "          <lmapc:timezone-offset>-01:00</lmapc:timezone-offset>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"events\":{\n"
	"      \"event\":[\n"
	"        {\n"
	"          \"name\":\"monthly\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              1\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              0\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ],\n"
	"            \"timezone-offset\":\"+00:00\"\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"weekly\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"monday\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              0\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ],\n"
	"            \"timezone-offset\":\"-01:00\"\n"
	"          }\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_events_calendar1)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>daily</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>0</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "          <lmapc:timezone-offset>+01:00</lmapc:timezone-offset>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>hourly</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>*</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "          <lmapc:timezone-offset>-01:30</lmapc:timezone-offset>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>daily</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>0</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "          <lmapc:timezone-offset>+01:00</lmapc:timezone-offset>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>hourly</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>*</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "          <lmapc:timezone-offset>-01:30</lmapc:timezone-offset>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"events\":{\n"
	"      \"event\":[\n"
	"        {\n"
	"          \"name\":\"daily\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              0\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ],\n"
	"            \"timezone-offset\":\"+01:00\"\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"hourly\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ],\n"
	"            \"timezone-offset\":\"-01:30\"\n"
	"          }\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_events_calendar2)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>hourly-on-weekends</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-week>saturday</lmapc:day-of-week>"
        "          <lmapc:day-of-week>sunday</lmapc:day-of-week>"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>"
        "          <lmapc:hour>*</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>once-every-six-hours</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>*</lmapc:month>"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>0</lmapc:hour>"
        "          <lmapc:hour>6</lmapc:hour>"
        "          <lmapc:hour>12</lmapc:hour>"
        "          <lmapc:hour>18</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "          <lmapc:end>2014-09-30T00:00:00+02:00</lmapc:end>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>hourly-on-weekends</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>saturday</lmapc:day-of-week>\n"
        "          <lmapc:day-of-week>sunday</lmapc:day-of-week>\n"
        "          <lmapc:hour>*</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>once-every-six-hours</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>*</lmapc:month>\n"
        "          <lmapc:day-of-month>*</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>0</lmapc:hour>\n"
        "          <lmapc:hour>6</lmapc:hour>\n"
        "          <lmapc:hour>12</lmapc:hour>\n"
        "          <lmapc:hour>18</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "          <lmapc:end>2014-09-29T22:00:00+00:00</lmapc:end>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"events\":{\n"
	"      \"event\":[\n"
	"        {\n"
	"          \"name\":\"hourly-on-weekends\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"saturday\",\n"
	"              \"sunday\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ]\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"once-every-six-hours\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              0,\n"
	"              6,\n"
	"              12,\n"
	"              18\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ],\n"
	"            \"end\":\"2014-09-29T22:00:00+00:00\"\n"
	"          }\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_events_calendar3)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>dec-31-11.00</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>december</lmapc:month>"
        "          <lmapc:day-of-month>31</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>11</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "      <lmapc:event>"
        "        <lmapc:name>jan-01-15.00</lmapc:name>"
        "        <lmapc:calendar>"
        "          <lmapc:month>january</lmapc:month>"
        "          <lmapc:day-of-month>1</lmapc:day-of-month>"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>"
        "          <lmapc:hour>15</lmapc:hour>"
        "          <lmapc:minute>0</lmapc:minute>"
        "          <lmapc:second>0</lmapc:second>"
        "        </lmapc:calendar>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>dec-31-11.00</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>december</lmapc:month>\n"
        "          <lmapc:day-of-month>31</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>11</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>jan-01-15.00</lmapc:name>\n"
        "        <lmapc:calendar>\n"
        "          <lmapc:month>january</lmapc:month>\n"
        "          <lmapc:day-of-month>1</lmapc:day-of-month>\n"
        "          <lmapc:day-of-week>*</lmapc:day-of-week>\n"
        "          <lmapc:hour>15</lmapc:hour>\n"
        "          <lmapc:minute>0</lmapc:minute>\n"
        "          <lmapc:second>0</lmapc:second>\n"
        "        </lmapc:calendar>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"events\":{\n"
	"      \"event\":[\n"
	"        {\n"
	"          \"name\":\"dec-31-11.00\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"december\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              31\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              11\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ]\n"
	"          }\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"jan-01-15.00\",\n"
	"          \"calendar\":{\n"
	"            \"month\":[\n"
	"              \"january\"\n"
	"            ],\n"
	"            \"day-of-month\":[\n"
	"              1\n"
	"            ],\n"
	"            \"day-of-week\":[\n"
	"              \"*\"\n"
	"            ],\n"
	"            \"hour\":[\n"
	"              15\n"
	"            ],\n"
	"            \"minute\":[\n"
	"              0\n"
	"            ],\n"
	"            \"second\":[\n"
	"              0\n"
	"            ]\n"
	"          }\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_schedules)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:schedules>"
        "      <lmapc:schedule>"
        "        <lmapc:name>foo</lmapc:name>"
        "        <name>bar</name>"
        "        <x:name>baz</x:name>"
        "      </lmapc:schedule>"
        "      <lmapc:schedule>"
        "        <lmapc:name>bar</lmapc:name>"
        "        <lmapc:start>now</lmapc:start>"
        "        <lmapc:execution-mode>sequential</lmapc:execution-mode>"
        "      </lmapc:schedule>"
        "      <lmapc:schedule>"
        "        <lmapc:name>baz</lmapc:name>"
        "        <lmapc:start>now</lmapc:start>"
        "        <lmapc:end>tomorrow</lmapc:end>"
        "        <lmapc:execution-mode>parallel</lmapc:execution-mode>"
        "      </lmapc:schedule>"
        "      <lmapc:schedule>"
        "        <lmapc:name>qux</lmapc:name>"
        "        <lmapc:start>now</lmapc:start>"
        "        <lmapc:end>tomorrow</lmapc:end>"
        "        <lmapc:duration>42</lmapc:duration>"
        "        <lmapc:execution-mode>pipelined</lmapc:execution-mode>"
        "      </lmapc:schedule>"
        "      <lmapc:schedule>"
        "        <lmapc:name>tag</lmapc:name>"
        "        <lmapc:start>now</lmapc:start>"
        "        <lmapc:tag>red</lmapc:tag>"
        "        <lmapc:suppression-tag>blue</lmapc:suppression-tag>"
        "      </lmapc:schedule>"
        "    </lmapc:schedules>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:schedules>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>foo</lmapc:name>\n"
        "      </lmapc:schedule>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>bar</lmapc:name>\n"
        "        <lmapc:start>now</lmapc:start>\n"
        "        <lmapc:execution-mode>sequential</lmapc:execution-mode>\n"
        "      </lmapc:schedule>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>baz</lmapc:name>\n"
        "        <lmapc:start>now</lmapc:start>\n"
        "        <lmapc:end>tomorrow</lmapc:end>\n"
        "        <lmapc:execution-mode>parallel</lmapc:execution-mode>\n"
        "      </lmapc:schedule>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>qux</lmapc:name>\n"
        "        <lmapc:start>now</lmapc:start>\n"
        "        <lmapc:duration>42</lmapc:duration>\n"
        "        <lmapc:execution-mode>pipelined</lmapc:execution-mode>\n"
        "      </lmapc:schedule>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>tag</lmapc:name>\n"
        "        <lmapc:start>now</lmapc:start>\n"
        "        <lmapc:tag>red</lmapc:tag>\n"
        "        <lmapc:suppression-tag>blue</lmapc:suppression-tag>\n"
        "      </lmapc:schedule>\n"
        "    </lmapc:schedules>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"schedules\":{\n"
	"      \"schedule\":[\n"
	"        {\n"
	"          \"name\":\"foo\"\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"bar\",\n"
	"          \"start\":\"now\",\n"
	"          \"execution-mode\":\"sequential\"\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"baz\",\n"
	"          \"start\":\"now\",\n"
	"          \"end\":\"tomorrow\",\n"
	"          \"execution-mode\":\"parallel\"\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"qux\",\n"
	"          \"start\":\"now\",\n"
	"          \"duration\":\"42\",\n"
	"          \"execution-mode\":\"pipelined\"\n"
	"        },\n"
	"        {\n"
	"          \"name\":\"tag\",\n"
	"          \"start\":\"now\",\n"
	"          \"tag\":[\n"
	"            \"red\"\n"
	"          ],\n"
	"          \"suppression-tag\":[\n"
	"            \"blue\"\n"
	"          ]\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_actions)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:schedules>"
        "      <lmapc:schedule>"
        "        <lmapc:name>foo</lmapc:name>"
        "        <lmapc:start>now</lmapc:start>"
        "        <lmapc:action>"
        "          <lmapc:name>foo</lmapc:name>"
        "          <name>bar</name>"
        "          <x:name>baz</x:name>"
        "        </lmapc:action>"
        "        <lmapc:action>"
        "          <lmapc:name>bar</lmapc:name>"
	"          <lmapc:option>"
	"            <lmapc:id>a</lmapc:id>"
	"            <lmapc:value>v</lmapc:value>"
	"          </lmapc:option>"
	"          <lmapc:option>"
	"            <lmapc:id>b</lmapc:id>"
	"            <lmapc:name>n</lmapc:name>"
	"          </lmapc:option>"
	"          <lmapc:option>"
	"            <lmapc:id>c</lmapc:id>"
	"            <lmapc:name>n</lmapc:name>"
	"            <lmapc:value>n</lmapc:value>"
	"          </lmapc:option>"
        "        </lmapc:action>"
        "      </lmapc:schedule>"
        "    </lmapc:schedules>"
        "  </lmapc:lmap>"
        "</config>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:schedules>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>foo</lmapc:name>\n"
        "        <lmapc:start>now</lmapc:start>\n"
        "        <lmapc:action>\n"
        "          <lmapc:name>foo</lmapc:name>\n"
        "        </lmapc:action>\n"
        "        <lmapc:action>\n"
        "          <lmapc:name>bar</lmapc:name>\n"
	"          <lmapc:option>\n"
	"            <lmapc:id>a</lmapc:id>\n"
	"            <lmapc:value>v</lmapc:value>\n"
	"          </lmapc:option>\n"
	"          <lmapc:option>\n"
	"            <lmapc:id>b</lmapc:id>\n"
	"            <lmapc:name>n</lmapc:name>\n"
	"          </lmapc:option>\n"
	"          <lmapc:option>\n"
	"            <lmapc:id>c</lmapc:id>\n"
	"            <lmapc:name>n</lmapc:name>\n"
	"            <lmapc:value>n</lmapc:value>\n"
	"          </lmapc:option>\n"
        "        </lmapc:action>\n"
        "      </lmapc:schedule>\n"
        "    </lmapc:schedules>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"schedules\":{\n"
	"      \"schedule\":[\n"
	"        {\n"
	"          \"name\":\"foo\",\n"
	"          \"start\":\"now\",\n"
	"          \"action\":[\n"
	"            {\n"
	"              \"name\":\"foo\"\n"
	"            },\n"
	"            {\n"
	"              \"name\":\"bar\",\n"
	"              \"option\":[\n"
	"                {\n"
	"                  \"id\":\"a\",\n"
	"                  \"value\":\"v\"\n"
	"                },\n"
	"                {\n"
	"                  \"id\":\"b\",\n"
	"                  \"name\":\"n\"\n"
	"                },\n"
	"                {\n"
	"                  \"id\":\"c\",\n"
	"                  \"name\":\"n\",\n"
	"                  \"value\":\"n\"\n"
	"                }\n"
	"              ]\n"
	"            }\n"
	"          ]\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_config(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_config_merge)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:agent>"
        "      <lmapc:controller-timeout>42</lmapc:controller-timeout>"
        "    </lmapc:agent>"
        "  </lmapc:lmap>"
        "</config>";

    const char *b =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:suppressions>"
        "      <lmapc:suppression>"
        "       <lmapc:name>suppression</lmapc:name>"
        "       <lmapc:match>*</lmapc:match>"
        "      </lmapc:suppression>"
        "    </lmapc:suppressions>"
        "  </lmapc:lmap>\n"
        "</config>\n";

    const char *c =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:events>"
        "      <lmapc:event>"
        "        <lmapc:name>periodic</lmapc:name>"
        "        <lmapc:random-spread>300000</lmapc:random-spread>"
        "        <lmapc:periodic>"
        "          <lmapc:interval>4321</lmapc:interval>"
        "          <lmapc:start>2015-02-01T17:44:21+02:00</lmapc:start>"
        "          <lmapc:end>2015-03-01T00:00:00+00:00</lmapc:end>"
        "        </lmapc:periodic>"
        "      </lmapc:event>"
        "    </lmapc:events>"
        "  </lmapc:lmap>"
        "</config>";

    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:agent>\n"
        "      <lmapc:controller-timeout>42</lmapc:controller-timeout>\n"
        "    </lmapc:agent>\n"
        "    <lmapc:suppressions>\n"
        "      <lmapc:suppression>\n"
        "        <lmapc:name>suppression</lmapc:name>\n"
        "        <lmapc:match>*</lmapc:match>\n"
        "      </lmapc:suppression>\n"
        "    </lmapc:suppressions>\n"
        "    <lmapc:events>\n"
        "      <lmapc:event>\n"
        "        <lmapc:name>periodic</lmapc:name>\n"
        "        <lmapc:random-spread>300000</lmapc:random-spread>\n"
        "        <lmapc:periodic>\n"
        "          <lmapc:interval>4321</lmapc:interval>\n"
        "          <lmapc:start>2015-02-01T15:44:21+00:00</lmapc:start>\n"
        "          <lmapc:end>2015-03-01T00:00:00+00:00</lmapc:end>\n"
        "        </lmapc:periodic>\n"
        "      </lmapc:event>\n"
        "    </lmapc:events>\n"
        "  </lmapc:lmap>\n"
        "</config>\n";
    char *d, *e;
    struct lmap *lmapa = NULL, *lmapb = NULL;

    lmapa = lmap_new();
    ck_assert_ptr_ne(lmapa, NULL);
    ck_assert_int_eq(lmap_xml_parse_config_string(lmapa, a), 0);
    ck_assert_int_eq(lmap_xml_parse_config_string(lmapa, b), 0);
    ck_assert_int_eq(lmap_xml_parse_config_string(lmapa, c), 0);
    d = lmap_xml_render_config(lmapa);
    ck_assert_ptr_ne(b, NULL);

    lmapb = lmap_new();
    ck_assert_ptr_ne(lmapb, NULL);
    ck_assert_int_eq(lmap_xml_parse_config_string(lmapb, d), 0);
    e = lmap_xml_render_config(lmapa);
    ck_assert_ptr_ne(c, NULL);

    ck_assert_str_eq(d, e);
    ck_assert_str_eq(e, x);

    ck_assert_str_eq(last_error_msg, "");

    lmap_free(lmapa); lmap_free(lmapb);
    free(d); free(e);
}
END_TEST

START_TEST(test_parser_state_agent)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:agent>"
	"      <lmapc:agent-id>550e8400-e29b-41d4-a716-446655440000</lmapc:agent-id>"
	"      <lmapc:agent-id>550e8400-e29b-41d4-a716-446655440000</lmapc:agent-id>"
	"      <lmapc:last-started>2016-02-21T22:13:40+01:00</lmapc:last-started>"
        "    </lmapc:agent>"
        "  </lmapc:lmap>"
        "</data>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:agent>\n"
	"      <lmapc:agent-id>550e8400-e29b-41d4-a716-446655440000</lmapc:agent-id>\n"
	"      <lmapc:last-started>2016-02-21T21:13:40+00:00</lmapc:last-started>\n"
        "    </lmapc:agent>\n"
        "  </lmapc:lmap>\n"
        "</data>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"agent\":{\n"
	"      \"agent-id\":\"550e8400-e29b-41d4-a716-446655440000\",\n"
	"      \"last-started\":\"2016-02-21T21:13:40+00:00\"\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_state(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_state_capabilities)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:capabilities>"
	"      <lmapc:version>lmap version 0.3</lmapc:version>"
	"      <x:version>xxxx version 0.0</x:version>"
	"      <lmapc:tag>system:IPv4 Capable</lmapc:tag>"
	"      <lmapc:tag>system:IPv4 Works</lmapc:tag>"
	"      <lmapc:tag>system:IPv6 Capable</lmapc:tag>"
        "    </lmapc:capabilities>"
        "  </lmapc:lmap>"
        "</data>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:capabilities>\n"
	"      <lmapc:version>lmap version 0.3</lmapc:version>\n"
	"      <lmapc:tag>system:IPv4 Capable</lmapc:tag>\n"
	"      <lmapc:tag>system:IPv4 Works</lmapc:tag>\n"
	"      <lmapc:tag>system:IPv6 Capable</lmapc:tag>\n"
        "    </lmapc:capabilities>\n"
        "  </lmapc:lmap>\n"
        "</data>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"capabilities\":{\n"
	"      \"version\":\"lmap version 0.3\",\n"
	"      \"tag\":[\n"
	"        \"system:IPv4 Capable\",\n"
	"        \"system:IPv4 Works\",\n"
	"        \"system:IPv6 Capable\"\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_state(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_state_capability_tasks)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap xmlns:x=\"urn:example\">"
        "    <lmapc:capabilities>"
	"      <lmapc:tasks>"
	"        <lmapc:task>"
	"          <lmapc:name>mtr</lmapc:name>"
	"          <lmapc:version>0.85</lmapc:version>"
	"          <lmapc:program>/usr/bin/mtr</lmapc:program>"
	"        </lmapc:task>"
	"      </lmapc:tasks>"
        "    </lmapc:capabilities>"
        "  </lmapc:lmap>"
        "</data>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:capabilities>\n"
	"      <lmapc:tasks>\n"
	"        <lmapc:task>\n"
	"          <lmapc:name>mtr</lmapc:name>\n"
	"          <lmapc:version>0.85</lmapc:version>\n"
	"          <lmapc:program>/usr/bin/mtr</lmapc:program>\n"
	"        </lmapc:task>\n"
	"      </lmapc:tasks>\n"
        "    </lmapc:capabilities>\n"
        "  </lmapc:lmap>\n"
        "</data>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"capabilities\":{\n"
	"      \"tasks\":{\n"
	"        \"task\":[\n"
	"          {\n"
	"            \"name\":\"mtr\",\n"
	"            \"version\":\"0.85\",\n"
	"            \"program\":\"\\/usr\\/bin\\/mtr\"\n"
	"          }\n"
	"        ]\n"
	"      }\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_state(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_state_schedules)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:schedules>"
        "      <lmapc:schedule>"
        "        <lmapc:name>demo</lmapc:name>"
        "        <lmapc:state>enabled</lmapc:state>"
        "        <lmapc:storage>42</lmapc:storage>"
        "        <lmapc:invocations>2</lmapc:invocations>"
        "        <lmapc:suppressions>8</lmapc:suppressions>"
        "        <lmapc:overlaps>1</lmapc:overlaps>"
        "        <lmapc:failures>2</lmapc:failures>"
        "        <lmapc:last-invocation>2016-02-23T14:31:45+01:00</lmapc:last-invocation>"
	"      </lmapc:schedule>"
	"    </lmapc:schedules>"
        "  </lmapc:lmap>"
        "</data>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:schedules>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>demo</lmapc:name>\n"
        "        <lmapc:state>enabled</lmapc:state>\n"
        "        <lmapc:storage>42</lmapc:storage>\n"
        "        <lmapc:invocations>2</lmapc:invocations>\n"
        "        <lmapc:suppressions>8</lmapc:suppressions>\n"
        "        <lmapc:overlaps>1</lmapc:overlaps>\n"
        "        <lmapc:failures>2</lmapc:failures>\n"
        "        <lmapc:last-invocation>2016-02-23T13:31:45+00:00</lmapc:last-invocation>\n"
	"      </lmapc:schedule>\n"
	"    </lmapc:schedules>\n"
        "  </lmapc:lmap>\n"
        "</data>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"schedules\":{\n"
	"      \"schedule\":[\n"
	"        {\n"
	"          \"name\":\"demo\",\n"
	"          \"state\":\"enabled\",\n"
	"          \"storage\":\"42\",\n"
	"          \"invocations\":2,\n"
	"          \"suppressions\":8,\n"
	"          \"overlaps\":1,\n"
	"          \"failures\":2,\n"
	"          \"last-invocation\":\"2016-02-23T13:31:45+00:00\"\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_state(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_state_actions)
{
    const char *a =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">"
        "  <lmapc:lmap>"
        "    <lmapc:schedules>"
        "      <lmapc:schedule>"
        "        <lmapc:name>demo</lmapc:name>"
        "        <lmapc:action>"
        "          <lmapc:name>mtr</lmapc:name>"
        "          <lmapc:state>enabled</lmapc:state>"
        "          <lmapc:invocations>2</lmapc:invocations>"
        "          <lmapc:suppressions>0</lmapc:suppressions>"
        "          <lmapc:overlaps>0</lmapc:overlaps>"
        "          <lmapc:failures>0</lmapc:failures>"
        "          <lmapc:last-invocation>2016-02-23T14:31:45+01:00</lmapc:last-invocation>"
        "          <lmapc:last-completion>2016-02-23T14:31:52+01:00</lmapc:last-completion>"
        "          <lmapc:last-status>0</lmapc:last-status>"
        "        </lmapc:action>"
        "        <lmapc:action>"
        "          <lmapc:name>happy</lmapc:name>"
        "          <lmapc:state>enabled</lmapc:state>"
        "          <lmapc:invocations>2</lmapc:invocations>"
        "          <lmapc:suppressions>0</lmapc:suppressions>"
        "          <lmapc:overlaps>0</lmapc:overlaps>"
        "          <lmapc:failures>2</lmapc:failures>"
        "          <lmapc:last-invocation>2016-02-23T14:31:52+01:00</lmapc:last-invocation>"
        "          <lmapc:last-completion>2016-02-23T14:31:53+01:00</lmapc:last-completion>"
        "          <lmapc:last-status>1</lmapc:last-status>"
        "          <lmapc:last-failed-completion>2016-02-23T14:31:53+01:00</lmapc:last-failed-completion>"
        "          <lmapc:last-failed-status>1</lmapc:last-failed-status>"
        "        </lmapc:action>"
	"      </lmapc:schedule>"
	"    </lmapc:schedules>"
        "  </lmapc:lmap>"
        "</data>";
    const char *x =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<data xmlns:lmapc=\"urn:ietf:params:xml:ns:yang:ietf-lmap-control\">\n"
        "  <lmapc:lmap>\n"
        "    <lmapc:schedules>\n"
        "      <lmapc:schedule>\n"
        "        <lmapc:name>demo</lmapc:name>\n"
        "        <lmapc:state>enabled</lmapc:state>\n"
        "        <lmapc:storage>0</lmapc:storage>\n"
        "        <lmapc:invocations>0</lmapc:invocations>\n"
        "        <lmapc:suppressions>0</lmapc:suppressions>\n"
        "        <lmapc:overlaps>0</lmapc:overlaps>\n"
        "        <lmapc:failures>0</lmapc:failures>\n"
	"        <lmapc:action>\n"
        "          <lmapc:name>mtr</lmapc:name>\n"
        "          <lmapc:state>enabled</lmapc:state>\n"
        "          <lmapc:storage>0</lmapc:storage>\n"
        "          <lmapc:invocations>2</lmapc:invocations>\n"
        "          <lmapc:suppressions>0</lmapc:suppressions>\n"
        "          <lmapc:overlaps>0</lmapc:overlaps>\n"
        "          <lmapc:failures>0</lmapc:failures>\n"
        "          <lmapc:last-invocation>2016-02-23T13:31:45+00:00</lmapc:last-invocation>\n"
        "          <lmapc:last-completion>2016-02-23T13:31:52+00:00</lmapc:last-completion>\n"
        "          <lmapc:last-status>0</lmapc:last-status>\n"
        "        </lmapc:action>\n"
        "        <lmapc:action>\n"
        "          <lmapc:name>happy</lmapc:name>\n"
        "          <lmapc:state>enabled</lmapc:state>\n"
        "          <lmapc:storage>0</lmapc:storage>\n"
        "          <lmapc:invocations>2</lmapc:invocations>\n"
        "          <lmapc:suppressions>0</lmapc:suppressions>\n"
        "          <lmapc:overlaps>0</lmapc:overlaps>\n"
        "          <lmapc:failures>2</lmapc:failures>\n"
        "          <lmapc:last-invocation>2016-02-23T13:31:52+00:00</lmapc:last-invocation>\n"
        "          <lmapc:last-completion>2016-02-23T13:31:53+00:00</lmapc:last-completion>\n"
        "          <lmapc:last-status>1</lmapc:last-status>\n"
        "          <lmapc:last-failed-completion>2016-02-23T13:31:53+00:00</lmapc:last-failed-completion>\n"
        "          <lmapc:last-failed-status>1</lmapc:last-failed-status>\n"
        "        </lmapc:action>\n"
	"      </lmapc:schedule>\n"
	"    </lmapc:schedules>\n"
        "  </lmapc:lmap>\n"
        "</data>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-control:lmap\":{\n"
	"    \"schedules\":{\n"
	"      \"schedule\":[\n"
	"        {\n"
	"          \"name\":\"demo\",\n"
	"          \"state\":\"enabled\",\n"
	"          \"storage\":\"0\",\n"
	"          \"invocations\":0,\n"
	"          \"suppressions\":0,\n"
	"          \"overlaps\":0,\n"
	"          \"failures\":0,\n"
	"          \"action\":[\n"
	"            {\n"
	"              \"name\":\"mtr\",\n"
	"              \"state\":\"enabled\",\n"
	"              \"storage\":\"0\",\n"
	"              \"invocations\":2,\n"
	"              \"suppressions\":0,\n"
	"              \"overlaps\":0,\n"
	"              \"failures\":0,\n"
	"              \"last-invocation\":\"2016-02-23T13:31:45+00:00\",\n"
	"              \"last-completion\":\"2016-02-23T13:31:52+00:00\",\n"
	"              \"last-status\":0\n"
	"            },\n"
	"            {\n"
	"              \"name\":\"happy\",\n"
	"              \"state\":\"enabled\",\n"
	"              \"storage\":\"0\",\n"
	"              \"invocations\":2,\n"
	"              \"suppressions\":0,\n"
	"              \"overlaps\":0,\n"
	"              \"failures\":2,\n"
	"              \"last-invocation\":\"2016-02-23T13:31:52+00:00\",\n"
	"              \"last-completion\":\"2016-02-23T13:31:53+00:00\",\n"
	"              \"last-status\":1,\n"
	"              \"last-failed-completion\":\"2016-02-23T13:31:53+00:00\",\n"
	"              \"last-failed-status\":1\n"
	"            }\n"
	"          ]\n"
	"        }\n"
	"      ]\n"
	"    }\n"
	"  }\n"
	"}";

    xx_test_roundtrip_state(a, x, ja, ja);
}
END_TEST

START_TEST(test_parser_report)
{
    const char *a =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<rpc xmlns:lmapr=\"urn:ietf:params:xml:ns:yang:ietf-lmap-report\">\n"
	"  <lmapr:report>\n"
	"    <lmapr:date>2016-12-25T16:33:02+00:00</lmapr:date>\n"
	"    <lmapr:agent-id>550e8400-e29b-41d4-a716-446655440000</lmapr:agent-id>\n"
	"    <lmapr:result>\n"
	"      <lmapr:schedule>demo</lmapr:schedule>\n"
	"      <lmapr:action>mtr-search-sites</lmapr:action>\n"
	"      <lmapr:task>mtr</lmapr:task>\n"
	"      <lmapr:option>\n"
	"        <lmapr:id>numeric</lmapr:id>\n"
	"        <lmapr:name>--no-dns</lmapr:name>\n"
	"      </lmapr:option>\n"
	"      <lmapr:option>\n"
	"        <lmapr:id>csv</lmapr:id>\n"
	"        <lmapr:name>--csv</lmapr:name>\n"
	"      </lmapr:option>\n"
	"      <lmapr:option>\n"
	"        <lmapr:id>lookup-AS-numbers</lmapr:id>\n"
	"        <lmapr:name>-z</lmapr:name>\n"
	"      </lmapr:option>\n"
	"      <lmapr:option>\n"
	"        <lmapr:id>one-cycle</lmapr:id>\n"
	"        <lmapr:name>--report-cycles</lmapr:name>\n"
	"        <lmapr:value>3</lmapr:value>\n"
	"      </lmapr:option>\n"
	"      <lmapr:option>\n"
	"        <lmapr:id>www.google.com</lmapr:id>\n"
	"        <lmapr:value>www.google.com</lmapr:value>\n"
	"      </lmapr:option>\n"
	"      <lmapr:tag>task-mtr-tag</lmapr:tag>\n"
	"      <lmapr:tag>schedule-demo-tag</lmapr:tag>\n"
	"      <lmapr:event>2016-12-20T09:16:30+00:00</lmapr:event>\n"
	"      <lmapr:start>2016-12-20T09:16:30+00:00</lmapr:start>\n"
	"      <lmapr:end>2016-12-20T09:16:38+00:00</lmapr:end>\n"
	"      <lmapr:cycle-number>20161220.081700</lmapr:cycle-number>\n"
        "      <lmapr:status>0</lmapr:status>\n"
	"      <lmapr:table>\n"
	"        <lmapr:row>\n"
	"          <lmapr:value>MTR.0.85</lmapr:value>\n"
	"          <lmapr:value>1482221851</lmapr:value>\n"
	"          <lmapr:value>OK</lmapr:value>\n"
	"          <lmapr:value>www.google.com</lmapr:value>\n"
	"          <lmapr:value>1</lmapr:value>\n"
	"          <lmapr:value>178.254.52.1</lmapr:value>\n"
	"          <lmapr:value>AS42730</lmapr:value>\n"
	"          <lmapr:value>1883</lmapr:value>\n"
	"        </lmapr:row>\n"
	"        <lmapr:row>\n"
	"          <lmapr:value>MTR.0.85</lmapr:value>\n"
	"          <lmapr:value>1482221851</lmapr:value>\n"
	"          <lmapr:value>OK</lmapr:value>\n"
	"          <lmapr:value>www.google.com</lmapr:value>\n"
	"          <lmapr:value>2</lmapr:value>\n"
	"          <lmapr:value>178.254.16.29</lmapr:value>\n"
	"          <lmapr:value>AS42730</lmapr:value>\n"
	"          <lmapr:value>425</lmapr:value>\n"
	"        </lmapr:row>\n"
	"        <lmapr:row>\n"
	"          <lmapr:value>MTR.0.85</lmapr:value>\n"
	"          <lmapr:value>1482221851</lmapr:value>\n"
	"          <lmapr:value>OK</lmapr:value>\n"
	"          <lmapr:value>www.google.com</lmapr:value>\n"
	"          <lmapr:value>12</lmapr:value>\n"
	"          <lmapr:value>216.58.213.228</lmapr:value>\n"
	"          <lmapr:value>AS15169</lmapr:value>\n"
	"          <lmapr:value>14173</lmapr:value>\n"
	"        </lmapr:row>\n"
	"      </lmapr:table>\n"
	"    </lmapr:result>\n"
	"  </lmapr:report>\n"
	"</rpc>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-report:report\":{\n"
	"    \"date\":\"2016-12-25T16:33:02+00:00\",\n"
	"    \"agent-id\":\"550e8400-e29b-41d4-a716-446655440000\",\n"
	"    \"result\":[\n"
	"      {\n"
	"        \"schedule\":\"demo\",\n"
	"        \"action\":\"mtr-search-sites\",\n"
	"        \"task\":\"mtr\",\n"
	"        \"option\":[\n"
	"          {\n"
	"            \"id\":\"numeric\",\n"
	"            \"name\":\"--no-dns\"\n"
	"          },\n"
	"          {\n"
	"            \"id\":\"csv\",\n"
	"            \"name\":\"--csv\"\n"
	"          },\n"
	"          {\n"
	"            \"id\":\"lookup-AS-numbers\",\n"
	"            \"name\":\"-z\"\n"
	"          },\n"
	"          {\n"
	"            \"id\":\"one-cycle\",\n"
	"            \"name\":\"--report-cycles\",\n"
	"            \"value\":\"3\"\n"
	"          },\n"
	"          {\n"
	"            \"id\":\"www.google.com\",\n"
	"            \"value\":\"www.google.com\"\n"
	"          }\n"
	"        ],\n"
	"        \"tag\":[\n"
	"          \"task-mtr-tag\",\n"
	"          \"schedule-demo-tag\"\n"
	"        ],\n"
	"        \"event\":\"2016-12-20T09:16:30+00:00\",\n"
	"        \"start\":\"2016-12-20T09:16:30+00:00\",\n"
	"        \"end\":\"2016-12-20T09:16:38+00:00\",\n"
	"        \"cycle-number\":\"20161220.081700\",\n"
	"        \"status\":0,\n"
	"        \"table\":[\n"
	"          {\n"
	"            \"row\":[\n"
	"              {\n"
	"                \"value\":[\n"
	"                  \"MTR.0.85\",\n"
	"                  \"1482221851\",\n"
	"                  \"OK\",\n"
	"                  \"www.google.com\",\n"
	"                  \"1\",\n"
	"                  \"178.254.52.1\",\n"
	"                  \"AS42730\",\n"
	"                  \"1883\"\n"
	"                ]\n"
	"              },\n"
	"              {\n"
	"                \"value\":[\n"
	"                  \"MTR.0.85\",\n"
	"                  \"1482221851\",\n"
	"                  \"OK\",\n"
	"                  \"www.google.com\",\n"
	"                  \"2\",\n"
	"                  \"178.254.16.29\",\n"
	"                  \"AS42730\",\n"
	"                  \"425\"\n"
	"                ]\n"
	"              },\n"
	"              {\n"
	"                \"value\":[\n"
	"                  \"MTR.0.85\",\n"
	"                  \"1482221851\",\n"
	"                  \"OK\",\n"
	"                  \"www.google.com\",\n"
	"                  \"12\",\n"
	"                  \"216.58.213.228\",\n"
	"                  \"AS15169\",\n"
	"                  \"14173\"\n"
	"                ]\n"
	"              }\n"
	"            ]\n"
	"          }\n"
	"        ]\n"
	"      }\n"
	"    ]\n"
	"  }\n"
	"}";

    xx_test_roundtrip_report(a, a, ja, ja);
}
END_TEST

START_TEST(test_parser_report_table)
{
    const char *a =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<rpc xmlns:lmapr=\"urn:ietf:params:xml:ns:yang:ietf-lmap-report\">\n"
	"  <lmapr:report>\n"
	"    <lmapr:date>2016-12-25T16:33:02+00:00</lmapr:date>\n"
	"    <lmapr:result>\n"
	"      <lmapr:event>2016-12-20T09:16:30+00:00</lmapr:event>\n"
	"      <lmapr:start>2016-12-20T09:16:30+00:00</lmapr:start>\n"
	"      <lmapr:status>0</lmapr:status>\n"
	"      <lmapr:table>\n"
	"        <lmapr:function>\n"
	"          <lmapr:uri>urn:example</lmapr:uri>\n"
	"          <lmapr:role>client</lmapr:role>\n"
	"          <lmapr:role>server</lmapr:role>\n"
	"        </lmapr:function>\n"
	"        <lmapr:function>\n"
	"          <lmapr:uri>urn:example2</lmapr:uri>\n"
	"        </lmapr:function>\n"
	"        <lmapr:column>program</lmapr:column>\n"
	"        <lmapr:column>time</lmapr:column>\n"
	"        <lmapr:column>status</lmapr:column>\n"
	"        <lmapr:column>destination</lmapr:column>\n"
	"        <lmapr:column>hop_number</lmapr:column>\n"
	"        <lmapr:column>hop_address</lmapr:column>\n"
	"        <lmapr:column>hop_ASN</lmapr:column>\n"
	"        <lmapr:column>hop_rtt_ms</lmapr:column>\n"
	"        <lmapr:row>\n"
	"          <lmapr:value>MTR.0.85</lmapr:value>\n"
	"          <lmapr:value>1482221851</lmapr:value>\n"
	"          <lmapr:value>OK</lmapr:value>\n"
	"          <lmapr:value>www.google.com</lmapr:value>\n"
	"          <lmapr:value>1</lmapr:value>\n"
	"          <lmapr:value>178.254.52.1</lmapr:value>\n"
	"          <lmapr:value>AS42730</lmapr:value>\n"
	"          <lmapr:value>1883</lmapr:value>\n"
	"        </lmapr:row>\n"
	"        <lmapr:row>\n"
	"          <lmapr:value>MTR.0.85</lmapr:value>\n"
	"          <lmapr:value>1482221851</lmapr:value>\n"
	"          <lmapr:value>OK</lmapr:value>\n"
	"          <lmapr:value>www.google.com</lmapr:value>\n"
	"          <lmapr:value>2</lmapr:value>\n"
	"          <lmapr:value>178.254.16.29</lmapr:value>\n"
	"          <lmapr:value>AS42730</lmapr:value>\n"
	"          <lmapr:value>425</lmapr:value>\n"
	"        </lmapr:row>\n"
	"      </lmapr:table>\n"
	"    </lmapr:result>\n"
	"  </lmapr:report>\n"
	"</rpc>\n";
    const char *ja =
	"{\n"
	"  \"ietf-lmap-report:report\":{\n"
	"    \"date\":\"2016-12-25T16:33:02+00:00\",\n"
	"    \"result\":[\n"
	"      {\n"
	"        \"event\":\"2016-12-20T09:16:30+00:00\",\n"
	"        \"start\":\"2016-12-20T09:16:30+00:00\",\n"
	"        \"status\":0,\n"
	"        \"table\":[\n"
	"          {\n"
	"            \"function\":[\n"
	"              {\n"
	"                \"uri\":\"urn:example\",\n"
	"                \"role\":[\n"
	"                  \"client\",\n"
	"                  \"server\"\n"
	"                ]\n"
	"              },\n"
	"              {\n"
	"                \"uri\":\"urn:example2\"\n"
	"              }\n"
	"            ],\n"
	"            \"column\":[\n"
	"              \"program\",\n"
	"              \"time\",\n"
	"              \"status\",\n"
	"              \"destination\",\n"
	"              \"hop_number\",\n"
	"              \"hop_address\",\n"
	"              \"hop_ASN\",\n"
	"              \"hop_rtt_ms\"\n"
	"            ],\n"
	"            \"row\":[\n"
	"              {\n"
	"                \"value\":[\n"
	"                  \"MTR.0.85\",\n"
	"                  \"1482221851\",\n"
	"                  \"OK\",\n"
	"                  \"www.google.com\",\n"
	"                  \"1\",\n"
	"                  \"178.254.52.1\",\n"
	"                  \"AS42730\",\n"
	"                  \"1883\"\n"
	"                ]\n"
	"              },\n"
	"              {\n"
	"                \"value\":[\n"
	"                  \"MTR.0.85\",\n"
	"                  \"1482221851\",\n"
	"                  \"OK\",\n"
	"                  \"www.google.com\",\n"
	"                  \"2\",\n"
	"                  \"178.254.16.29\",\n"
	"                  \"AS42730\",\n"
	"                  \"425\"\n"
	"                ]\n"
	"              }\n"
	"            ]\n"
	"          }\n"
	"        ]\n"
	"      }\n"
	"    ]\n"
	"  }\n"
	"}";

    xx_test_roundtrip_report(a, a, ja, ja);
}
END_TEST

START_TEST(test_csv)
{
    FILE *f;
    const char delimiter = 'x';
    const char *msg =
	"This message is something rather long including funny characters; "
	"such as ' or . or ? and then even more;\"";
    char buf[256];
    char *s;

    f = tmpfile();
    ck_assert_ptr_ne(f, NULL);
    csv_start(f, delimiter, "0");
    csv_append(f, delimiter, "1");
    csv_append(f, delimiter, "2");
    csv_end(f);
    csv_start(f, delimiter, "3");
    csv_append(f, delimiter, "4");
    csv_append(f, delimiter, "5");
    csv_append(f, delimiter, "6");
    csv_end(f);
    csv_start(f, delimiter, msg);
    csv_end(f);
    rewind(f);
    s = fgets(buf, sizeof(buf), f);
    ck_assert_ptr_ne(s, NULL);
    ck_assert_str_eq(buf, "0x1x2\n");
    s = fgets(buf, sizeof(buf), f);
    ck_assert_ptr_ne(s, NULL);
    ck_assert_str_eq(buf, "3x4x5x6\n");
    s = fgets(buf, sizeof(buf), f);
    ck_assert_ptr_ne(s, NULL);
    if (strlen(buf) > 3) {
	buf[strlen(buf)-3] = 0;
    }
    ck_assert_str_eq(buf+1, msg);
    fclose(f);
}
END_TEST

START_TEST(test_csv_key_value)
{
    FILE *f;
    const char delimiter = ';';
    const char *hello = "hel;lo";
    const char *world = "wo\"rld";
    char *key, *value;

    f = tmpfile();
    ck_assert_ptr_ne(f, NULL);
    csv_append_key_value(f, delimiter, hello, world);
    csv_append_key_value(f, delimiter, world, hello);
    rewind(f);
    csv_next_key_value(f, delimiter, &key, &value);
    ck_assert_str_eq(key, hello);
    ck_assert_str_eq(value, world);
    csv_next_key_value(f, delimiter, &key, &value);
    ck_assert_str_eq(key, world);
    ck_assert_str_eq(value, hello);
    csv_next_key_value(f, delimiter, &key, &value);
    ck_assert_ptr_eq(key, NULL);
    ck_assert_ptr_eq(value, NULL);
    rewind(f);
    csv_next_key_value(f, delimiter, NULL, &value);
    ck_assert_str_eq(value, world);
    rewind(f);
    csv_next_key_value(f, delimiter, &key, NULL);
    ck_assert_str_eq(key, hello);
    fclose(f);
}
END_TEST

START_TEST(test_load_config_xml)
{
    struct lmap *lmap;

    lmap = lmap_new();
    ck_assert_ptr_ne(lmap, NULL);

    ck_assert_int_eq(lmap_xml_parse_config_path(lmap, testdata_dir), 0);
    ck_assert_ptr_ne(lmap->agent, NULL);
    ck_assert_int_eq(lmap_valid(lmap), 1);

    lmap_free(lmap);
}
END_TEST

START_TEST(test_load_config_json)
{
    struct lmap *lmap;

    lmap = lmap_new();
    ck_assert_ptr_ne(lmap, NULL);

    ck_assert_int_eq(lmap_json_parse_config_path(lmap, testdata_dir), 0);
    ck_assert_ptr_ne(lmap->agent, NULL);
    ck_assert_int_eq(lmap_valid(lmap), 1);

    lmap_free(lmap);
}
END_TEST

Suite * lmap_suite(void)
{
    Suite *s;
    TCase *tc_core, *tc_parser, *tc_csv, *tc_file;

    s = suite_create("lmap");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_lmap_agent);
    tcase_add_test(tc_core, test_lmap_localtime_handling);
    tcase_add_test(tc_core, test_lmap_registry);
    tcase_add_test(tc_core, test_lmap_option);
    tcase_add_test(tc_core, test_lmap_tag);
    tcase_add_test(tc_core, test_lmap_suppression);
    tcase_add_test(tc_core, test_lmap_event);
    tcase_add_test(tc_core, test_lmap_event_periodic);
    tcase_add_test(tc_core, test_lmap_event_calendar);
    tcase_add_test(tc_core, test_lmap_event_one_off);
    tcase_add_test(tc_core, test_lmap_task);
    tcase_add_test(tc_core, test_lmap_schedule);
    tcase_add_test(tc_core, test_lmap_action);
    tcase_add_test(tc_core, test_lmap_lmap);
    tcase_add_test(tc_core, test_lmap_val);
    tcase_add_test(tc_core, test_lmap_row);
    tcase_add_test(tc_core, test_lmap_table);
    tcase_add_test(tc_core, test_lmap_result);
    suite_add_tcase(s, tc_core);

    /* Parser test case */
    tc_parser = tcase_create("Parser");
    tcase_add_test(tc_parser, test_parser_config_agent);
    tcase_add_test(tc_parser, test_parser_config_suppressions);
    tcase_add_test(tc_parser, test_parser_config_tasks);
    tcase_add_test(tc_parser, test_parser_config_events);
    tcase_add_test(tc_parser, test_parser_config_events_calendar0);
    tcase_add_test(tc_parser, test_parser_config_events_calendar1);
    tcase_add_test(tc_parser, test_parser_config_events_calendar2);
    tcase_add_test(tc_parser, test_parser_config_events_calendar3);
    tcase_add_test(tc_parser, test_parser_config_schedules);
    tcase_add_test(tc_parser, test_parser_config_actions);
    tcase_add_test(tc_parser, test_parser_config_merge);
    tcase_add_test(tc_parser, test_parser_state_agent);
    tcase_add_test(tc_parser, test_parser_state_capabilities);
    tcase_add_test(tc_parser, test_parser_state_capability_tasks);
    tcase_add_test(tc_parser, test_parser_state_schedules);
    tcase_add_test(tc_parser, test_parser_state_actions);
    tcase_add_test(tc_parser, test_parser_report);
    tcase_add_test(tc_parser, test_parser_report_table);
    suite_add_tcase(s, tc_parser);

    tc_csv = tcase_create("Csv");
    tcase_add_test(tc_csv, test_csv);
    tcase_add_test(tc_csv, test_csv_key_value);
    suite_add_tcase(s, tc_csv);

    /* Other I/O test case */
    tc_file = tcase_create("File I/O");
    tcase_add_test(tc_file, test_load_config_xml);
    tcase_add_test(tc_file, test_load_config_json);
    suite_add_tcase(s, tc_file);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    setenv("TZ", "GMT", 1);

    lmap_set_log_handler(vlog);

    s = lmap_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
