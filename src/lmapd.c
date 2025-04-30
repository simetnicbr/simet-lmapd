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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <errno.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "pidfile.h"
#include "lmap-io.h"
#include "runner.h"
#include "workspace.h"

static struct lmapd *lmapd = NULL;

static void
atexit_cb(void)
{
    if (lmapd) {
	if (lmapd_pid_check(lmapd)) {
	    lmapd_pid_remove(lmapd);
	}

	lmapd_free(lmapd);
    }
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: %s [-j|-x] [-f] [-n] [-s] [-z] [-v] [-h] [-q queue] [-c config] [-s status]\n"
	    "\t-f fork (daemonize)\n"
	    "\t-n parse config and dump config and exit\n"
	    "\t-s parse config and dump state and exit\n"
	    "\t-z clean the workspace before starting\n"
	    "\t-q path to queue directory\n"
	    "\t-c path to config directory or file (repeat for more paths or files)\n"
	    "\t\t(an argument of \"+\" stands for the built-in/default path)\n"
	    "\t-b path to capability directory or file\n"
	    "\t-r path to run directory (pid file and status file)\n"
	    "\t-v show version information and exit\n"
#ifdef WITH_JSON
	    "\t-j use JSON for config and reports\n"
#endif
#ifdef WITH_XML
	    "\t-x use XML for config and reports (default)\n"
#endif
	    "\t-h show brief usage information and exit\n",
	    LMAPD_LMAPD);
}

static int
devnull(const int newfd, const mode_t mode)
{
    int oldfd = open("/dev/null", mode, 0);
    if (oldfd == -1) {
	return -1;
    }
    if (dup2(oldfd, newfd) == -1) {
	int serr = errno;
	(void) close(oldfd);
	errno = serr;
	return -1;
    }
    if (oldfd != newfd) {
	(void) close(oldfd);
    }
    return 0;
}

/**
 * @brief Daemonizes the process
 *
 * Deamonize the process by detaching from the parent, starting a new
 * session, changing to the root directory, and attaching stdin,
 * stdout and stderr to /dev/null.
 */

static void
daemonize(void)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
	lmap_err("fork() failed");
	exit(EXIT_FAILURE);
    }

    if (pid > 0) {
	exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
	lmap_err("setsid() failed");
	exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
	lmap_err("fork() failed");
	exit(EXIT_FAILURE);
    }

    if (pid > 0) {
	exit(EXIT_SUCCESS);
    }

    if (chdir("/") < 0) {
	lmap_err("chdir() failed");
	exit(EXIT_FAILURE);
    }

    closelog();

    /* note: do not assume FDs 0,1,2 are already open. */
    if (devnull(STDIN_FILENO, O_RDONLY) ||
	    devnull(STDOUT_FILENO, O_WRONLY) ||
	    dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
	lmap_err("failed to redirect stdin/stdout/stderr to /dev/null");
	exit(EXIT_FAILURE);
    }

#if defined(HAVE_CLOSEFROM)
    /* glibc: uses close_range() in Linux, with a smart fallback to
     *        iterating /proc/<pid>/fd.
     * MUSL:  no support (yet?)
     * BSD:   has closefrom() if new enough */
    closefrom(3);
#else
    /* in the absense of an small ulimit this could take forever, so
     * limit it to MIN(4096, _SC_OPEN_MAX) */
    {
	const long maxfd = sysconf(_SC_OPEN_MAX);
	int cfd;
	for (cfd = (maxfd < 4096)? (int)maxfd : 4096; cfd > 2; cfd--) {
	    (void) close(cfd);
	}
    }
#endif
    openlog("lmapd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

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
read_config(struct lmapd *lmapd)
{
    int ret = 0;
    struct paths *paths;

    lmapd->lmap = lmap_new();
    if (! lmapd->lmap) {
	return -1;
    }

    paths = lmapd->config_paths;
    while(paths && paths->path) {
	ret = lmap_io_parse_config_path(lmapd->lmap, paths->path);
	if (ret != 0) {
	    lmap_free(lmapd->lmap);
	    lmapd->lmap = NULL;
	    return -1;
	}
	paths = paths->next;
    }

    if (lmapd->lmap->agent) {
	lmapd->lmap->agent->last_started = time(NULL);
    }

    ret = lmap_io_parse_state_path(lmapd->lmap, lmapd->capability_path);
    if (ret != 0) {
	lmap_free(lmapd->lmap);
	lmapd->lmap = NULL;
	return -1;
    }

    if (!lmapd->lmap->capabilities) {
	lmapd->lmap->capabilities = lmap_capability_new();
    }
    if (lmapd->lmap->capabilities) {
	char buf[256];
	snprintf(buf, sizeof(buf), "%s version %d.%d.%d", LMAPD_LMAPD,
		 LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
	lmap_capability_set_version(lmapd->lmap->capabilities, buf);
	lmap_capability_add_system_tags(lmapd->lmap->capabilities);
    }

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

int
main(int argc, char *argv[])
{
    int opt, daemon = 0, noop = 0, state = 0, zap = 0, valid = 0, ret = 0;
    char *capability_path = NULL;
    char *queue_path = NULL;
    char *run_path = NULL;
    pid_t pid;

    lmapd = lmapd_new();
    if (! lmapd) {
	exit(EXIT_FAILURE);
    }

    atexit(atexit_cb);

    while ((opt = getopt(argc, argv, "fnszq:c:b:r:vhjx")) != -1) {
	switch (opt) {
	case 'f':
	    daemon = 1;
	    break;
	case 'n':
	    noop = 1;
	    break;
	case 's':
	    state = 1;
	    break;
	case 'z':
	    zap = 1;
	    break;
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
	case 'b':
	    capability_path = optarg;
	    break;
	case 'r':
	    run_path = optarg;
	    break;
	case 'v':
	    printf("%s version %d.%d.%d\n", LMAPD_LMAPD,
		   LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
	    exit(EXIT_SUCCESS);
	case 'h':
	    usage(stdout);
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
	default:
	    usage(stderr);
	    exit(EXIT_FAILURE);
	}
    }

    openlog("lmapd", LOG_PID | LOG_NDELAY, LOG_DAEMON);

    if (! lmapd->config_paths && add_default_config_path())
	exit(EXIT_FAILURE);

    (void) lmapd_set_capability_path(lmapd,
		capability_path ? capability_path : LMAPD_CAPABILITY_DIR);

    if (noop || state) {
	if (read_config(lmapd) != 0) {
	    exit(EXIT_FAILURE);
	}
	valid = lmap_valid(lmapd->lmap);
	if (valid && noop) {
	    char *doc = lmap_io_render_config(lmapd->lmap);
	    if (! doc) {
		exit(EXIT_FAILURE);
	    }
	    fputs(doc, stdout);
	    free(doc);
	}
	if (valid && state) {
	    char *doc = lmap_io_render_state(lmapd->lmap);
	    if (! doc) {
		exit(EXIT_FAILURE);
	    }
	    fputs(doc, stdout);
	    free(doc);
	}
	if (fflush(stdout) == EOF) {
	    lmap_err("flushing stdout failed");
	    exit(EXIT_FAILURE);
	}
	exit(valid ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    (void) lmapd_set_queue_path(lmapd,
				queue_path ? queue_path : LMAPD_QUEUE_DIR);
    (void) lmapd_set_run_path(lmapd,
			      run_path ? run_path : LMAPD_RUN_DIR);
    if (!lmapd->queue_path || !lmapd->run_path) {
	exit(EXIT_FAILURE);
    }

    if (zap) {
	(void) lmapd_workspace_clean(lmapd);
    }

    if (daemon) {
	daemonize();
    }

    /*
     * Initialize the random number generator. Since random numbers
     * are only used to calculate random spreads, using time() might
     * only cause real issues if a large number of MAs boot up at the
     * same time. Well, perhaps that is even possible after the power
     * outage? I will fix it later when the power is back. ;-)
     */

    srand(time(NULL));

    pid = lmapd_pid_read(lmapd);
    if (pid) {
	lmap_err("%s already running (pid %d)?", LMAPD_LMAPD, pid);
	exit(EXIT_FAILURE);
    }
    lmapd_pid_write(lmapd);

    do {
	if (read_config(lmapd) != 0) {
	    exit(EXIT_FAILURE);
	}
	valid = lmap_valid(lmapd->lmap);
	if (! valid) {
	    lmap_err("configuration is invalid - exiting...");
	    exit(EXIT_FAILURE);
	}

	(void) lmapd_workspace_init(lmapd);
	ret = lmapd_run(lmapd);

	/*
	 * Sleep one second just in case we get into a failure loop so
	 * as to avoid getting into a crazy tight loop.
	 */

	if (lmapd->flags & LMAPD_FLAG_RESTART) {
	    (void) sleep(1);
	    lmap_free(lmapd->lmap);
	    lmapd->lmap = NULL;
	}
    } while (lmapd->flags & LMAPD_FLAG_RESTART);

    closelog();
    exit(ret == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
}
