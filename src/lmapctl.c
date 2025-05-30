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

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <ftw.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "pidfile.h"
#include "lmap-io.h"
#include "runner.h"
#include "workspace.h"

static int clean_cmd(int argc, char *argv[]);
static int config_cmd(int argc, char *argv[]);
static int help_cmd(int argc, char *argv[]);
static int reload_cmd(int argc, char *argv[]);
static int report_cmd(int argc, char *argv[]);
static int running_cmd(int argc, char *argv[]);
static int shutdown_cmd(int argc, char *argv[]);
static int status_cmd(int argc, char *argv[]);
static int validate_cmd(int argc, char *argv[]);
static int version_cmd(int argc, char *argv[]);

static const struct
{
    const char * const command;
    const char * const description;
    int (* const func) (int argc, char *argv[]);
} cmds[] = {
    { "clean",    "clean the workspace (be careful!)",      clean_cmd },
    { "config",   "validate and render lmap configuration", config_cmd },
    { "help",     "show brief list of commands",            help_cmd },
    { "reload",   "reload the lmap configuration",          reload_cmd },
    { "report",   "report data",			    report_cmd },
    { "running",  "test if the lmap daemon is running",	    running_cmd },
    { "shutdown", "shutdown the lmap daemon",		    shutdown_cmd },
    { "status",   "show status information",                status_cmd },
    { "validate", "validate lmap configuration",            validate_cmd },
    { "version",  "show version information",	            version_cmd },
    { NULL, NULL, NULL }
};

static struct lmapd *lmapd = NULL;
static int task_input_ft = LMAP_FT_CSV;
static int display_wide = 80; /* 0 == no limit */

static void
atexit_cb(void)
{
    if (lmapd) {
	lmapd_free(lmapd);
    }
}

static void
vlog(int level, const char *func, const char *format, va_list args)
{
    (void) level;
    (void) func;
    fprintf(stderr, "lmapctl: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: %s [-h] [-j|-x] [-q queue] [-c config] [-C dir] [-w [width]] <command> [command arguments]\n"
	    "\t-q path to queue directory\n"
	    "\t-c path to config directory or file (repeat for more paths or files)\n"
	    "\t\t(an argument of \"+\" stands for the built-in/default path)\n"
	    "\t-r path to run directory (pid file and status file)\n"
	    "\t-C path in which the program is executed\n"
#ifdef WITH_JSON
	    "\t-j use json format when generating output\n"
#endif
#ifdef WITH_XML
	    "\t-x use xml format when generating output (default)\n"
#endif
	    "\t-i [json|xml] use structured input for reports\n"
	    "\t-w [<width>] wide output when stdout is a tty\n"
	    "\t\t(use 0 for unlimited. <width> will be 132 if not specified)\n"
	    "\t-h show brief usage information and exit\n",
	    LMAPD_LMAPCTL);
}

static void
help(FILE *f)
{
    int i;

    for (i = 0; cmds[i].command; i++) {
	fprintf(f, "  %-10s  %s\n", cmds[i].command,
		cmds[i].description ? cmds[i].description : "");
    }
}

static const char*
render_datetime_short(time_t *tp)
{
    static char buf[32];
    struct tm *tmp;
    time_t now;

    if (! *tp) {
	return "";
    }

    now = time(NULL);

    tmp = localtime(tp);
    if (now-*tp < 24*60*60) {
	strftime(buf, sizeof(buf), "%H:%M:%S", tmp);
    } else {
	strftime(buf, sizeof(buf), "%Y-%m-%d", tmp);
    }
    return buf;
}

static char*
render_storage(uint64_t storage)
{
    static char buf[127];

    if (storage/1024/1024 > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "G",
		 ((storage/1024/1024)+512)/1014);
    } else if (storage/1024 > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "M",
		 ((storage/1024)+512)/1024);
    } else if (storage > 9999) {
	snprintf(buf, sizeof(buf), "%" PRIu64 "K",
		 (storage+512)/1024);
    } else {
	snprintf(buf, sizeof(buf), "%" PRIu64, storage);
    }
    return buf;
}

static const char*
render_datetime_long(time_t *tp)
{
    static char buf[32];
    struct tm *tmp;

    if (! *tp) {
	return "";
    }

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

    return buf;
}

#if 0
static char*
render_uint32(uint32_t num)
{
    static char buf[32];

    if (num > 1000*1000*1000) {
	snprintf(buf, sizeof(buf), "%uG", ((num/1000/1000)+500)/1000);
    } else if (num > 1000*1000) {
	snprintf(buf, sizeof(buf), "%uM", ((num/1000)+500)/1000);
    } else if (num > 1000) {
	snprintf(buf, sizeof(buf), "%uk", (num+500)/1000);
    } else {
	snprintf(buf, sizeof(buf), "%u", num);
    }

    return buf;
}
#endif

/**
 * @brief Reads the XML config file
 *
 * Function to read the XML config file and initialize the
 * coresponding data structures with data.
 *
 * @param lmapd pointer to the lmapd struct
 * @return 0 on success -1 or error
 */

static int
read_config(struct lmapd *a_lmapd)
{
    struct paths *paths;

    a_lmapd->lmap = lmap_new();
    if (! a_lmapd->lmap) {
	return -1;
    }

    paths = a_lmapd->config_paths;
    while (paths && paths->path) {
	if (lmap_io_parse_config_path(a_lmapd->lmap, paths->path)) {
	    lmap_free(a_lmapd->lmap);
	    a_lmapd->lmap = NULL;
	    return -1;
	}
	paths = paths->next;
    }

    return 0;
}

/**
 * @brief Reads the XML state file
 *
 * Function to read the XML state file and initialize the
 * coresponding data structures with data.
 *
 * @param lmapd pointer to the lmapd struct
 * @return 0 on success -1 or error
 */

static int
read_state(struct lmapd *a_lmapd)
{
    char statefile[PATH_MAX];
    const char *ext;

    ext = lmap_io_engine_ext();
    snprintf(statefile, sizeof(statefile), "%s/%s%s",
	     a_lmapd->run_path, LMAPD_STATUS_FILE, ext);

    a_lmapd->lmap = lmap_new();
    if (! a_lmapd->lmap) {
	return -1;
    }

    if (lmap_io_parse_state_file(a_lmapd->lmap, statefile)) {
	lmap_free(a_lmapd->lmap);
	a_lmapd->lmap = NULL;
	return -1;
    }

    return 0;
}

static int
clean_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGUSR2) == -1) {
	lmap_err("failed to send SIGUSR2 to process %d", pid);
	return 1;
    }

    return 0;
}

static int
config_cmd(int argc, char *argv[])
{
    char *doc;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }

    doc = lmap_io_render_config(lmapd->lmap);
    if (! doc) {
	return 1;
    }
    fputs(doc, stdout);
    free(doc);
    return 0;
}

static int
help_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    help(stdout);
    return 0;
}

static int
reload_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGHUP) == -1) {
	lmap_err("failed to send SIGHUP to process %d", pid);
	return 1;
    }

    return 0;
}

static int
report_cmd(int argc, char *argv[])
{
    char *report = NULL;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }

    if (lmapd->lmap->agent && ! lmapd->lmap->agent->report_date) {
	lmapd->lmap->agent->report_date = time(NULL);
    }

    /*
     * Setup the paths into the workspaces and then load the results
     * found in the current directory.
     */

    lmapd_workspace_init(lmapd);
    if (lmapd_workspace_read_results(lmapd, task_input_ft)) {
	return 0;
    }

    report = lmap_io_render_report(lmapd->lmap);
    if (! report) {
	return 1;
    }
    fputs(report, stdout);
    free(report);
    return 0;
}

static int
running_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	return 1;
    }

    return 0;
}

static int
shutdown_cmd(int argc, char *argv[])
{
    pid_t pid;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGTERM) == -1) {
	lmap_err("failed to send SIGTERM to process %d", pid);
	return 1;
    }

    return 0;
}

static int
status_cmd(int argc, char *argv[])
{
    struct lmap *lmap = NULL;
    pid_t pid;
    struct timespec tp = { .tv_sec = 0, .tv_nsec = 87654321 };
    int name_width = 15;

    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    pid = lmapd_pid_read(lmapd);
    if (! pid) {
	lmap_err("failed to obtain PID of lmapd");
	return 1;
    }

    if (kill(pid, SIGUSR1) == -1) {
	lmap_err("failed to send SIGUSR1 to process %d", pid);
	return 1;
    }
    /*
     * I should do something more intelligent here, e.g., wait unti
     * the state file is available with a matching touch date and
     * nobody is writing it (i.e., obtain an exclusing open).
     */
    (void) nanosleep(&tp, NULL);

    if (read_state(lmapd) != 0) {
	return 1;
    }

    if (lmapd->lmap) {
	lmap = lmapd->lmap;
    }

    if (lmap && lmap->agent) {
	struct agent *agent = lmap->agent;
	struct capability *cap = lmap->capabilities;
	printf("agent-id:     %s\n", agent->agent_id);
	printf("version:      %s\n", cap ? cap->version : "<?>");
	if (cap && cap->tags) {
	    struct tag *tag;
	    printf("tags:         "); /* 14 chars */

	    const int ll_max = (display_wide) ? display_wide - 14 : 0;
	    int ll = ll_max;
	    for (tag = cap->tags; tag; tag = tag->next) {
		if (display_wide > 0) {
		    int tl = (int)strlen(tag->tag) + 2; /* ", " */
		    ll -= tl;
		    if (ll <= 1 && tag != cap->tags) {
			printf("\n              ");
			ll = ll_max - tl;
		    }
		}
		printf("%s%s", tag->tag, (tag->next)? ", " : "");
	    }
	    printf("\n");
	}
	printf("last-started: %s\n",
	       render_datetime_long(&agent->last_started));
	printf("\n");
    }

    /* calculate schedule and action name column width */
    if (display_wide >= 0) {
	size_t nw_max = 2;
	int nw;

	if (lmap && lmap->schedules) {
	    struct schedule *schedule;
	    struct action *action;
	    size_t anw = 0;

	    for (schedule = lmap->schedules; schedule; schedule = schedule->next) {
		anw = (schedule->name) ? strlen(schedule->name) : 0;
		if (anw > nw_max)
		    nw_max = anw;

		for (action = schedule->actions; action; action = action->next) {
		    /* actions are indented 1 space */
		    anw = (action->name) ? strlen(action->name) + 1 : 0;
		    if (anw > nw_max)
			nw_max = anw;
		}
	    }
	}
	/* punish insanity, damage-limit bugs */
	nw = (nw_max < INT_MAX - 10) ? (int)nw_max : name_width;  /* verified, nw_max < INT_MAX */

	/* we need 65 characters for the rest of the columns unless unlimited */
	nw = (display_wide > 0 && (nw + 65 > display_wide)) ? display_wide - 65 : nw;

	/* we never shrink name_width */
	name_width = (nw > name_width) ? nw : name_width;
    }

    printf("%-*.*s %-1s %3.3s %3.3s %3.3s %3.3s %5.5s %3s %3s %-10s %-10s %s\n",
	   name_width, name_width,
	   "SCHEDULE/ACTION", "S", "IN%", "SU%", "OV%", "ER%", " STOR",
	   "LST", "LFS", "L-INVOKE", "L-COMPLETE", "L-FAILURE");

    if (lmap && lmap->schedules) {
	const char *state;
	struct schedule *schedule;
	struct action *action;
	uint32_t total_attempts;

	for (schedule = lmap->schedules; schedule; schedule = schedule->next) {
	    switch (schedule->state) {
	    case LMAP_SCHEDULE_STATE_ENABLED:
		state = "E";
		break;
	    case LMAP_SCHEDULE_STATE_DISABLED:
		state = "D";
		break;
	    case LMAP_SCHEDULE_STATE_RUNNING:
		state = "R";
		break;
	    case LMAP_SCHEDULE_STATE_SUPPRESSED:
		state = "S";
		break;
	    default:
		state = "?";
	    }

	    total_attempts = schedule->cnt_invocations
		+ schedule->cnt_suppressions + schedule->cnt_overlaps;
	    printf("%-*.*s ", name_width, name_width, schedule->name ? schedule->name : "???");
	    printf("%-1s ", state);
	    printf("%3u %3u %3u %3u ",
		   (unsigned int)(total_attempts ? schedule->cnt_invocations*100/total_attempts : 0),
		   (unsigned int)(total_attempts ? schedule->cnt_suppressions*100/total_attempts : 0),
		   (unsigned int)(total_attempts ? schedule->cnt_overlaps*100/total_attempts : 0),
		   (unsigned int)(schedule->cnt_invocations ? schedule->cnt_failures*100/schedule->cnt_invocations : 0));

	    printf("%5.5s ", render_storage(schedule->storage));

	    if (schedule->last_invocation) {
		printf("%8.8s%s", "",
		       render_datetime_short(&schedule->last_invocation));
	    }

	    printf("\n");

	    for (action = schedule->actions; action; action = action->next) {
		switch(action->state) {
		case LMAP_ACTION_STATE_ENABLED:
		    state = "E";
		    break;
		case LMAP_ACTION_STATE_DISABLED:
		    state = "D";
		    break;
		case LMAP_ACTION_STATE_RUNNING:
		    state = "R";
		    break;
		case LMAP_ACTION_STATE_SUPPRESSED:
		    state = "S";
		    break;
		default:
		    state = "?";
		}

		total_attempts = action->cnt_invocations
		    + action->cnt_suppressions + action->cnt_overlaps;
		printf(" %-*.*s ", name_width-1, name_width-1, action->name ? action->name : "???");
		printf("%-1s ", state);
		printf("%3u %3u %3u %3u ",
		       (unsigned int)(total_attempts ? action->cnt_invocations*100/total_attempts : 0),
		       (unsigned int)(total_attempts ? action->cnt_suppressions*100/total_attempts : 0),
		       (unsigned int)(total_attempts ? action->cnt_overlaps*100/total_attempts : 0),
		       (unsigned int)(action->cnt_invocations ? action->cnt_failures*100/action->cnt_invocations : 0));

		printf("%5.5s ", render_storage(action->storage));

		printf("%3d %3d ", action->last_status,
		       action->last_failed_status);

		printf("%-10s ", action->last_invocation
		       ? render_datetime_short(&action->last_invocation) : "");

		printf("%-10s ", action->last_completion
		       ? render_datetime_short(&action->last_completion) : "");

		if (action->last_failed_completion) {
		    printf("%s", render_datetime_short(&action->last_failed_completion));
		}
		printf("\n");
	    }
	}
    }

    printf("\n");
    printf("%-15.15s %-1s\n",
	   "SUPPRESSION", "S");

    if (lmap && lmap->supps) {
	const char *state;
	struct supp *supp;

	for (supp = lmap->supps; supp; supp = supp->next) {
	    switch (supp->state) {
	    case LMAP_SUPP_STATE_ENABLED:
		state = "E";
		break;
	    case LMAP_SUPP_STATE_DISABLED:
		state = "D";
		break;
	    case LMAP_SUPP_STATE_ACTIVE:
		state = "A";
		break;
	    default:
		state = "?";
		break;
	    }

	    printf("%-15.15s ", supp->name ? supp->name : "???");
	    printf("%-1s ", state);

	    printf("\n");
	}
    }
    return 0;
}

static int
validate_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    if (read_config(lmapd) != 0) {
	return 1;
    }
    if (! lmap_valid(lmapd->lmap)) {
	return 1;
    }
    return 0;
}

static int
version_cmd(int argc, char *argv[])
{
    if (argc != 1) {
	printf("%s: wrong # of args: should be '%s'\n",
	       LMAPD_LMAPCTL, argv[0]);
	return 1;
    }

    printf("%s version %d.%d.%d\n", LMAPD_LMAPCTL,
	   LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
    return 0;
}

static int
add_default_config_path(void)
{
    char cfgpath[] = LMAPD_CONFIG_DIR;
    char *p = cfgpath;

    while(p && *p) {
	char *q = strchr(p, ':');
	if (q) {
	    *q = '\0';
	    q++;
	}
	if (lmapd_add_config_path(lmapd, p))
	    return -1;
	p = q;
    }
    return 0;
}

static int
getint(const char *s)
{
    intmax_t i;
    char *end;

    i = strtoimax(s, &end, 10);
    if (*end || i > INT32_MAX || i < INT32_MIN) {
	lmap_err("illegal int32 value '%s'", s);
	exit(EXIT_FAILURE);
    }
    return (int32_t)i; /* verified: INT32_MIN <= i <= INT32_MAX */
}

static int
is_valid_fd(const int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

static void
fix_fds(const int fd, const int fl)
{
    int nfd;

    if (is_valid_fd(fd))
        return;

    nfd = open("/dev/null", fl);
    if (nfd == -1 || dup2(nfd, fd) == -1) {
	lmap_err("failed to redirect invalid FD %d to /dev/null: %s",
		 fd, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (nfd != fd) {
        close(nfd);
    }
}

static void
sanitize_std_fds(void)
{
    /* do it in file descriptor numerical order! */
    fix_fds(STDIN_FILENO,  O_RDONLY);
    fix_fds(STDOUT_FILENO, O_WRONLY);
    fix_fds(STDERR_FILENO, O_RDWR);
}

int
main(int argc, char *argv[])
{
    int i, opt;
    char *queue_path = NULL;
    char *run_path = NULL;

    /* Ensure POSIX environment is valid for FDs 0-2 */
    sanitize_std_fds();

    lmap_set_log_handler(vlog);

    lmapd = lmapd_new();
    if (! lmapd) {
	exit(EXIT_FAILURE);
    }

    atexit(atexit_cb);

    /* default to unlimited columns display mode on non-tty */
    errno = 0;
    if (!isatty(STDOUT_FILENO) && errno != EBADF) {
	display_wide = 0;
    }

    /* glibc, MUSL, uclibc, uclibc-ng, openbsd and freebsd grok :: */
    while ((opt = getopt(argc, argv, "q:c:r:C:i:hjxw::")) != -1) {
	switch (opt) {
	case 'q':
	    queue_path = optarg;
	    break;
	case 'c':
	    if (optarg && optarg[0] == '+' && optarg[1] == '\0') {
		if (add_default_config_path()) {
		    exit(EXIT_FAILURE);
		}
	    } else {
		if (lmapd_add_config_path(lmapd, optarg)) {
		    exit(EXIT_FAILURE);
		}
	    }
	    break;
	case 'r':
	    run_path = optarg;
	    break;
	case 'C':
	    if (chdir(optarg) == -1) {
		lmap_err("failed to change directory to '%s'", optarg);
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'h':
	    usage(stdout);
	    fprintf(stdout, "\ncommands:\n");
	    help(stdout);
	    exit(EXIT_SUCCESS);
	case 'j':
	    if (lmap_io_set_engine(LMAP_IO_JSON)) {
		lmap_err("JSON IO engine unavailable");
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'x':
	    if (lmap_io_set_engine(LMAP_IO_XML)) {
		lmap_err("XML IO engine unavailable");
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'i':
	    /* FIXME: whitespace trim this, for "-i foo" as a single arg */
	    if (!strncasecmp(optarg, "json", 5)) {
#ifdef WITH_JSON
		task_input_ft = LMAP_FT_JSON;
		break;
#else
		lmap_err("JSON IO engine unavailable");
		exit(EXIT_FAILURE);
#endif
	    } else if (!strncasecmp(optarg, "xml", 4)) {
#ifdef WITH_XML
		task_input_ft = LMAP_FT_XML;
		break;
#else
		lmap_err("XML IO engine unavailable");
		exit(EXIT_FAILURE);
#endif
	    } else {
		lmap_err("unknown structured input format for reports: %s", optarg);
		exit(EXIT_FAILURE);
	    }
	case 'w':
	    display_wide = (optarg) ? getint(optarg) : -1;
	    if (display_wide < 0) {
		display_wide = isatty(STDOUT_FILENO) ? 132 : 0;
	    } else if (display_wide && display_wide < 80) {
		display_wide = 80;
	    }

	    break;

	default:
	    usage(stderr);
	    exit(EXIT_FAILURE);
	}
    }

    if (optind >= argc) {
	lmap_err("expected command argument after options");
	exit(EXIT_FAILURE);
    }

    if (! lmapd->config_paths && add_default_config_path())
	exit(EXIT_FAILURE);

    (void) lmapd_set_queue_path(lmapd,
				queue_path ? queue_path : LMAPD_QUEUE_DIR);
    (void) lmapd_set_run_path(lmapd,
			      run_path ? run_path : LMAPD_RUN_DIR);
    if (!lmapd->config_paths || !lmapd->queue_path || !lmapd->run_path) {
	exit(EXIT_FAILURE);
    }

    for (i = 0; cmds[i].command; i++) {
	if (! strcmp(cmds[i].command, argv[optind])) {
	    if (cmds[i].func(argc - optind, argv + optind) == 0) {
		exit(EXIT_SUCCESS);
	    } else {
		exit(EXIT_FAILURE);
	    }
	}
    }

    if (! cmds[i].command) {
	lmap_err("unknown command '%s' - valid commands are:", argv[optind]);
	help(stderr);
	exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
