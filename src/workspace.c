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

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE 1 /* open(O_DIRECTORY), dirfd() prototype */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>

#include "lmap.h"
#include "lmapd.h"
#include "utils.h"
#include "csv.h"
#include "lmap-io.h"
#include "workspace.h"

/* incoming schedule queue name, must start with _ */
#define LMAPD_QUEUE_INCOMING_NAME "_incoming"

#define UNUSED(x) (void)(x)

static const char delimiter = ';';

/**
 * @brief Create a safe filesystem name
 *
 * Creates a safe filesystem name. Unsafe characters are %-encoded if
 * necessary.  It ensures the filename does not start with [._] to
 * avoid creating hidden files, and to give lmapd a private namespace
 * to work with (anything starting with "_").
 *
 * Note: as a side-effect, does not allow filenames to start with a
 * few other characters, either, and will %-escape them instead.
 *
 * @param name file system name
 * @param buf work buffer, NULL for static buffer
 * @param buflen work buffer length (zero for static buffer)
 * @return pointer to a safe filesystem name
 */

static char*
mksafe(char *buf, size_t buflen, const char *name)
{
    size_t i, j;
    const char safe[] = "-.,_";
    const char hex[] = "0123456789ABCDEF";
    static char save_name[NAME_MAX];

    if (!buf || !buflen) {
	buf = save_name;
	buflen = sizeof(save_name);
    }

    if (!name) {
	buf[0] = '\0';
	return buf;
    }

    for (i = 0, j = 0; name[i] && j < buflen - 1; i++) {
	if (isalnum(name[i]) || (i > 0 && strchr(safe, name[i]))) {
	    buf[j++] = name[i];
	} else {
	    /* %-escape the char if there is enough space left */
	    if (j < NAME_MAX - 4) {
		buf[j++] = '%';
		buf[j++] = hex[(name[i]>>4) & 0x0f];
		buf[j++] = hex[name[i] & 0x0f];;
	    } else {
		break;
	    }
	}
    }
    buf[j] = '\0';
    return buf;
}

/**
 * @brief Callback for removing a file
 *
 * Callback called by nftw when traversing a directory tree.
 *
 * @param fpath path to the file which is to be removed
 * @param sb unused
 * @param typeflag unused
 * @param ftwbuf unused
 * @return 0 on success, -1 on error
 */

static int
remove_cb(const char *fpath, const struct stat *sb,
	  int typeflag, struct FTW *ftwbuf)
{
    (void) sb;
    (void) typeflag;
    (void) ftwbuf;

    if (remove(fpath)) {
	lmap_err("cannot remove %s", fpath);
	/* we continue to remove the rest */
    }
    return 0;
}

/**
 * @brief Recursively removes a directory/file
 *
 * Function to recursively remove a directory/file from the
 * filesystem. A directory can include other files or directories.
 *
 * @param path path to directory or file
 * @return 0 on success, -1 on error
 */

static int
remove_all(char *path)
{
    if (! path) {
	return -1;
    }
    return nftw(path, remove_cb, 12, FTW_DEPTH | FTW_PHYS);
}

/* static unsigned long du_cnt = 0;	unused */
/* static unsigned long du_size = 0;	unused */
static uint64_t du_blocks = 0;

static int
du_cb(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    UNUSED(fpath);
    UNUSED(ftwbuf);

    if (tflag == FTW_F) {
	/* du_size += sb->st_size; */
	du_blocks += (uint64_t)sb->st_blocks;
	/* du_cnt++; */
    }
    return 0;           /* To tell nftw() to continue */
}

static int
du(char *path, uint64_t *storage)
{
    /* du_cnt = 0;  */
    /* du_size = 0; */
    du_blocks = 0;
    if (nftw(path, du_cb, 6, 0) == -1) {
	return -1;
    }
    *storage = du_blocks * 512;
    return 0;
}


/**
 * @brief Clean the complete workspace (aka queue) directory
 *
 * Function to clean the queue directory by removing everything in the
 * queue directory.
 *
 * @param lmapd pointer to the struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_clean(struct lmapd *lmapd)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(lmapd);

    (void) remove_all(NULL);

    if (!lmapd->queue_path) {
	return 0;
    }

    dfd = opendir(lmapd->queue_path);
    if (!dfd) {
	lmap_err("failed to open queue directory '%s'", lmapd->queue_path);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
	    continue;
	}
	(void) snprintf(filepath, sizeof(filepath), "%s/%s",
			lmapd->queue_path, dp->d_name);
	if (remove_all(filepath) != 0) {
	    lmap_err("failed to remove '%s'", filepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

int
lmapd_workspace_update(struct lmapd *lmapd)
{
    int ret = 0;
    struct schedule *sched;
    struct action *act;

    if (!lmapd || !lmapd->lmap) {
	return 0;
    }

    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	if (du(sched->workspace, &sched->storage) == -1) {
	    ret = -1;
	}
	for (act = sched->actions; act; act = act->next) {
	    if (du(act->workspace, &act->storage) == -1) {
		ret = -1;
	    }
	}
    }

    return ret;
}

/**
 * @brief Clean the workspace of an schedule
 *
 * Function to clean the workspace of an schedule, by removing
 * the processing queue (all files in the base schedule directory),
 * it leaves directories and files starting with "_" untouched.
 *
 * @param lmapd pointer to the struct lmapd
 * @param schedule pointer to the struct schedule
 * @return 0 on success, -1 on error
 */
int
lmapd_workspace_schedule_clean(struct lmapd *lmapd, struct schedule *schedule)
{
    int ret = 0;
    struct dirent *dp;
    struct stat st;
    DIR *dfd;

    assert(lmapd);
    (void) lmapd;

    if (!schedule || !schedule->workspace) {
	return 0;
    }

    dfd = opendir(schedule->workspace);
    if (!dfd) {
	lmap_err("failed to open directory '%s'", schedule->workspace);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (dp->d_name[0] == '_') {
	    continue;
	}
	if (fstatat(dirfd(dfd), dp->d_name, &st, AT_SYMLINK_NOFOLLOW)
		|| S_ISDIR(st.st_mode)) {
	    continue;
	}
	if (unlinkat(dirfd(dfd), dp->d_name, 0)) {
	    lmap_err("failed to remove '%s/%s'", schedule->workspace, dp->d_name);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

/**
 * @brief Move the workspace incoming queue of an schedule
 *
 * Function to move the contents of the incoming special
 * queue of an schedule to the active input queue.
 *
 * Only complete queue entries (i.e. those with both .data
 * and .meta files) are moved.
 *
 * @param lmapd pointer to the struct lmapd
 * @param schedule pointer to the struct schedule
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_schedule_move(struct lmapd *lmapd, struct schedule *schedule)
{
    int ret = -1;
    char oldfilepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;
    struct stat st;

    int dirfd_dest = -1;
    char *sdata = NULL, *s;

    assert(lmapd);
    (void) lmapd;

    if (!schedule || !schedule->workspace) {
	return 0;
    }

    const char * const newfilepath = schedule->workspace;

    errno = 0;
    do {
	dirfd_dest = open(newfilepath, O_DIRECTORY | O_RDONLY);
    } while (dirfd_dest == -1 && (errno == EAGAIN || errno == EINTR));
    if (dirfd_dest == -1) {
	lmap_err("failed to open directory '%s': %s",
		 newfilepath, strerror(errno));
	return -1;
    }

    snprintf(oldfilepath, sizeof(oldfilepath), "%s/" LMAPD_QUEUE_INCOMING_NAME,
	     schedule->workspace);
    dfd = opendir(oldfilepath);
    if (!dfd) {
	lmap_err("failed to open directory '%s': %s",
		 oldfilepath, strerror(errno));
	goto err_exit;
    }

    sdata = NULL;
    while ((dp = readdir(dfd)) != NULL) {
	/* skip ., .., hidden files/directories */
	if (dp->d_name[0] == '.') {
	    continue;
	}
	/* is it the .meta file ? */
	s = strrchr(dp->d_name, '.');
	if (s && !strcmp(".meta", s)) {
	    free(sdata);
	    sdata = strdup(dp->d_name);
	    if (!sdata)
		break; /* abort scan */
	    s = strrchr(sdata, '.');
	    if (!s)
		break; /* should *NEVER* happen */
	    s++;
	    strcpy(s, "data"); /* strlen("data") == strlen("meta") */

	    if (fstatat(dirfd(dfd), dp->d_name, &st, AT_SYMLINK_NOFOLLOW)
		    || !S_ISREG(st.st_mode)) {
		continue; /* "meta" is not a regular file? skip this pair */
	    }
	    /* "meta" *is* there, "data" might not be */
	    if (fstatat(dirfd(dfd), sdata, &st, AT_SYMLINK_NOFOLLOW)
		    || !S_ISREG(st.st_mode)) {
		continue; /* "data" is not a regular file, or not there yet, skip this pair */
	    }
	    if (linkat(dirfd(dfd), sdata, dirfd_dest, sdata, 0)) {
		lmap_err("failed to move %s from %s to %s: %s",
			sdata, oldfilepath, newfilepath, strerror(errno));
		continue;
	    }
	    if (linkat(dirfd(dfd), dp->d_name, dirfd_dest, dp->d_name, 0)) {
		lmap_err("failed to move %s from %s to %s: %s",
			dp->d_name, oldfilepath, newfilepath, strerror(errno));
		/* rollback first linkat() */
		if (unlinkat(dirfd_dest, sdata, 0))
		    lmap_err("Could not rollback move of '%s/%s': %s",
			    oldfilepath, sdata, strerror(errno));
		break;
	    }
	    /* unlink from source dir to complete the move operation, do not
	     * short-circuit -- if we unlink either one, we already avoid
	     * double processing them */
	    if (unlinkat(dirfd(dfd), dp->d_name, 0)) {
		    lmap_wrn("failed to unlink %s from incoming queue: %s",
			    dp->d_name, strerror(errno));
	    }
	    if (unlinkat(dirfd(dfd), sdata, 0)) {
		    lmap_wrn("failed to unlink %s from incoming queue: %s",
			    sdata, strerror(errno));
	    }
	}
    }
    ret = 0;

err_exit:
    free(sdata);
    if (dfd) {
	(void) closedir(dfd);
    }
    if (dirfd_dest != -1) {
	(void) close(dirfd_dest);
    }

    return ret;
}

/**
 * @brief Clean the workspace of an action
 *
 * Function to clean the workspace of an action by removing everything
 * in the workspace directory.
 *
 * We preserve files and direct subdirectories (but not files or
 * directories inside subdirectories) starting with "_", so that actions
 * can have a private namespace for work that, while not guaranteed to
 * last across lmapd config updates and restarts, keeps state from one
 * schedule execution to the next.
 *
 * @param lmapd pointer to the struct lmapd
 * @param action pointer to the struct action
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_action_clean(struct lmapd *lmapd, struct action *action)
{
    int ret = 0;
    char filepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;

    assert(lmapd);
    (void) lmapd;

    if (!action || !action->workspace) {
	return 0;
    }

    dfd = opendir(action->workspace);
    if (!dfd) {
	lmap_err("failed to open '%s'", action->workspace);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
	    continue;
	}
	if (dp->d_name[0] == '_') {
	    continue;
	}
	snprintf(filepath, sizeof(filepath), "%s/%s",
		 action->workspace, dp->d_name);
	if (remove_all(filepath) != 0) {
	    lmap_err("failed to remove '%s'", filepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

/**
 * @brief Move the workspace of an action
 *
 * Function to move the workspace of an action to a destination
 * schedule.  The action output files are moved to the destination
 * schedule's incoming special folder.
 *
 * Note: all files in the action workspace are moved, except for
 * hidden files, or those starting with "_".
 *
 * Should the destination schedule be the action's own schedule,
 * move its output files to its own schedule's *active* (processing)
 * incoming queue instead, where it will be immediately available
 * for consumption (e.g. by the next action in a sequential execution
 * mode).
 *
 * The "defer" parameter can be used to move the results of all
 * actions of a schedule only after the schedule finishes running,
 * while still immediately moving the output of actions directed
 * to their own schedule (see above).
 *
 * @param lmapd pointer to the struct lmapd
 * @param schedule pointer to the struct schedule
 * @param action pointer to the struct action
 * @param destination pointer to the struct schedule
 * @param defer defer moves destined to a different schedule
 * @return 0 on success, -1 on error, 1 if the move was deferred
 */

int
lmapd_workspace_action_move(struct lmapd *lmapd, struct schedule *schedule,
		            struct action *action, struct schedule *destination,
			    int defer)
{
    int ret = 0;
    char oldfilepath[PATH_MAX];
    char newfilepath[PATH_MAX];
    struct dirent *dp;
    DIR *dfd;
    struct stat st;

    assert(lmapd);
    (void) lmapd;

    if (!schedule || !schedule->name
	|| !action || !action->workspace || !action->name
	|| !destination || !destination->workspace) {
	return 0;
    }

    /* we want to process moves to the action's own schedule
     * immediately, but we might want to defer moves to any other
     * schedule to happen when the schedule finishes (i.e. all
     * actions have finished). */
    if (defer && destination != schedule)
	return 1;

    dfd = opendir(action->workspace);
    if (!dfd) {
	lmap_err("failed to open '%s'", action->workspace);
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	/* we only "move" files, never directories or other inode types */
	if (fstatat(dirfd(dfd), dp->d_name, &st, AT_SYMLINK_NOFOLLOW)
		|| !S_ISREG(st.st_mode)) {
	    continue;
	}
	if (dp->d_name[0] == '_' || dp->d_name[0] == '.') {
	    continue;
	}
	/* we don't need to special case . and .. directories due
	 * to the above */
	snprintf(oldfilepath, sizeof(oldfilepath), "%s/%s",
		 action->workspace, dp->d_name);

	if (destination != schedule) {
	    snprintf(newfilepath, sizeof(newfilepath),
			 "%s/" LMAPD_QUEUE_INCOMING_NAME "/%s",
			 destination->workspace, dp->d_name);
	} else {
	    /* Special case: action moving its result to its own schedule */
	    snprintf(newfilepath, sizeof(newfilepath), "%s/%s",
			 destination->workspace, dp->d_name);
	}
	if (link(oldfilepath, newfilepath) < 0) {
	    lmap_err("failed to move '%s' to '%s'", oldfilepath, newfilepath);
	    ret = -1;
	}
    }
    (void) closedir(dfd);

    return ret;
}

/**
 * @brief Create workspace folders for schedules and their actions
 *
 * Function to create the workspace folders for schedules and their
 * actions. Actions store results before in their workspace sending
 * them to the destination schedule's incoming special folder.
 *
 * @param lmapd pointer to struct lmapd
 * @return 0 on success, -1 on error
 */

int
lmapd_workspace_init(struct lmapd *lmapd)
{
    int ret = 0;
    struct schedule *sched;
    struct action *act;
    char filepath[PATH_MAX];

    assert(lmapd);

    if (!lmapd->lmap || !lmapd->queue_path) {
	return 0;
    }

    for (sched = lmapd->lmap->schedules; sched; sched = sched->next) {
	if (! sched->name) {
	    continue;
	}
	snprintf(filepath, sizeof(filepath), "%s/%s",
		 lmapd->queue_path, mksafe(NULL, 0, sched->name));
	if (mkdir(filepath, 0700) < 0 && errno != EEXIST) {
	    lmap_err("failed to mkdir '%s'", filepath);
	    ret = -1;
	}
	lmap_schedule_set_workspace(sched, filepath);

	for (act = sched->actions; act; act = act->next) {
	    if (! act->name) {
		continue;
	    }
	    snprintf(filepath, sizeof(filepath), "%s/%s",
		     sched->workspace, mksafe(NULL, 0, act->name));
	    if (mkdir(filepath, 0700) < 0 && errno != EEXIST) {
		lmap_err("failed to mkdir '%s'", filepath);
		ret = -1;
		continue;
	    }
	    lmap_action_set_workspace(act, filepath);
	}

	/* create incoming directory */
	snprintf(filepath, sizeof(filepath), "%s/" LMAPD_QUEUE_INCOMING_NAME,
		sched->workspace);
	if (mkdir(filepath, 0700) < 0 && errno != EEXIST) {
	    lmap_err("failed to mkdir '%s'", filepath);
	    ret = -1;
	}
    }

    return ret;
}

int
lmapd_workspace_action_meta_add_start(struct schedule *schedule, struct action *action, struct task *task)
{
    int fd;
    FILE *f;
    char buf[128];
    struct option *option;
    struct tag *tag;

    assert(action && action->name && action->workspace);

    fd = lmapd_workspace_action_open_meta(schedule, action,
					  O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == -1) {
	return -1;
    }
    f = fdopen(fd, "w");
    if (! f) {
	lmap_err("failed to create meta file stream for action '%s'",
		 action->name);
	(void) close(fd);
	return -1;
    }

    snprintf(buf, sizeof(buf), "%s version %d.%d.%d", LMAPD_LMAPD,
	     LMAP_VERSION_MAJOR, LMAP_VERSION_MINOR, LMAP_VERSION_PATCH);
    csv_append_key_value(f, delimiter, "magic", buf);
    csv_append_key_value(f, delimiter, "schedule", schedule->name);
    csv_append_key_value(f, delimiter, "action", action->name);
    csv_append_key_value(f, delimiter, "task", task->name);
    for (option = task->options; option; option = option->next) {
	csv_append_key_value(f, delimiter, "option-id", option->id);
	csv_append_key_value(f, delimiter, "option-name", option->name);
	csv_append_key_value(f, delimiter, "option-value", option->value);
    }
    for (option = action->options; option; option = option->next) {
	csv_append_key_value(f, delimiter, "option-id", option->id);
	csv_append_key_value(f, delimiter, "option-name", option->name);
	csv_append_key_value(f, delimiter, "option-value", option->value);
    }
    for (tag = task->tags; tag; tag = tag->next) {
	csv_append_key_value(f, delimiter, "tag", tag->tag);
    }
    for (tag = schedule->tags; tag; tag = tag->next) {
	csv_append_key_value(f, delimiter, "tag", tag->tag);
    }
    for (tag = action->tags; tag; tag = tag->next) {
	csv_append_key_value(f, delimiter, "tag", tag->tag);
    }
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)schedule->last_invocation);
    csv_append_key_value(f, delimiter, "event", buf);
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)action->last_invocation);
    csv_append_key_value(f, delimiter, "start", buf);
    if (schedule->cycle_number) {
	struct tm *tmp;
	tmp = gmtime(&schedule->cycle_number);
	strftime(buf, sizeof(buf), "%Y%m%d.%H%M%S", tmp);
	csv_append_key_value(f, delimiter, "cycle-number", buf);
    }
    /* TODO conflict */
    (void) fclose(f);
    return 0;
}

int
lmapd_workspace_action_meta_add_end(struct schedule *schedule, struct action *action)
{
    int fd;
    FILE *f;
    char buf[128];

    assert(action && action->name && action->workspace);

    fd = lmapd_workspace_action_open_meta(schedule, action,
					  O_WRONLY | O_APPEND);
    if (fd == -1) {
	return -1;
    }
    f = fdopen(fd, "a");
    if (! f) {
	lmap_err("failed to append meta file stream for action '%s'", action->name);
	(void) close(fd);
	return -1;
    }

    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)action->last_completion);
    csv_append_key_value(f, delimiter, "end", buf);
    snprintf(buf, sizeof(buf), "%d", action->last_status);
    csv_append_key_value(f, delimiter, "status", buf);
    /* TODO conflicts */
    (void) fclose(f);
    return 0;
}

int
lmapd_workspace_action_open_data(struct schedule *schedule,
				 struct action *action, int flags)
{
    int fd;
    char filepath[PATH_MAX];
    char b1[NAME_MAX];

    snprintf(filepath, sizeof(filepath), "%s/%llu-%s-%s.data",
	     action->workspace,
	     (unsigned long long)action->last_invocation,
	     mksafe(NULL, 0, schedule->name),
	     mksafe(b1, sizeof(b1), action->name));
    fd = open(filepath, flags, 0600);
    if (fd == -1) {
	lmap_err("failed to open '%s'", filepath);
    }
    return fd;
}

int
lmapd_workspace_action_open_meta(struct schedule *schedule,
				 struct action *action, int flags)
{
    int fd;
    char filepath[PATH_MAX];
    char b1[NAME_MAX];

    snprintf(filepath, sizeof(filepath), "%s/%llu-%s-%s.meta",
	     action->workspace,
	     (unsigned long long)action->last_invocation,
	     mksafe(NULL, 0, schedule->name),
	     mksafe(b1, sizeof(b1), action->name));
    fd = open(filepath, flags, 0600);
    if (fd == -1) {
	lmap_err("failed to open '%s'", filepath);
    }
    return fd;
}

static struct table *
read_table(int fd)
{
    int inrow = 0;
    FILE *file;
    struct table *tab;
    struct row *row = NULL;
    struct value *val;

    file = fdopen(fd, "r");
    if (! file) {
	lmap_err("failed to create file stream: %s", strerror(errno));
	return NULL;
    }

    tab = lmap_table_new();
    if (! tab) {
	(void) fclose(file);
	return NULL;
    }

    while (!feof(file)) {
	char *s = csv_next(file, delimiter);
	if (! s) {
	    if (errno) {
		lmap_err("failed to parse csv file: %s", strerror(errno));
		goto error_exit;
	    }
	    if (feof(file)) {
		break;
	    }
	    inrow = 0;
	    continue;
	}
	if (!inrow) {
	    row = lmap_row_new();
	    if (! row) {
		free(s);
		goto error_exit;
	    }
	    lmap_table_add_row(tab, row);
	    inrow++;
	}
	val = lmap_value_new();
	if (! val) {
	    free(s);
	    goto error_exit;
	}
	lmap_value_set_value(val, s);
	lmap_row_add_value(row, val);
	free(s);
    }

    (void) fclose(file);
    return tab;

error_exit:
    lmap_table_free(tab);
    (void) fclose(file);
    return NULL;
}

static struct result *
read_result(int fd)
{
    FILE *file;
    struct result *res;
    char *key, *value;
    struct option *opt = NULL;

    file = fdopen(fd, "r");
    if (! file) {
	lmap_err("failed to create file stream: %s", strerror(errno));
	return NULL;
    }

    res = lmap_result_new();
    if (! res) {
	(void) fclose(file);
	return NULL;
    }

    while (!feof(file)) {
	key = NULL;
	value = NULL;
	csv_next_key_value(file, delimiter, &key, &value);
	if (errno) {
		lmap_err("failed to read csv file stream: %s", strerror(errno));
		lmap_result_free(res);
		(void) fclose(file);
		free(key);
		free(value);
		return NULL;
	}
	if (key && value) {
	    if (! strcmp(key, "schedule")) {
		lmap_result_set_schedule(res, value);
	    }
	    if (! strcmp(key, "action")) {
		lmap_result_set_action(res, value);
	    }
	    if (! strcmp(key, "task")) {
		lmap_result_set_task(res, value);
	    }
	    if (! strcmp(key, "option-id")) {
		if (opt) {
		    lmap_result_add_option(res, opt);
		}
		opt = lmap_option_new();
		if (opt) lmap_option_set_id(opt, value);
	    }
	    if (! strcmp(key, "option-name")) {
		if (opt) lmap_option_set_name(opt, value);
	    }
	    if (! strcmp(key, "option-value")) {
		if (opt) lmap_option_set_value(opt, value);
	    }
	    if (! strcmp(key, "tag")) {
		lmap_result_add_tag(res, value);
	    }
	    if (! strcmp(key, "event")) {
		lmap_result_set_event_epoch(res, value);
	    }
	    if (! strcmp(key, "start")) {
		lmap_result_set_start_epoch(res, value);
	    }
	    if (! strcmp(key, "end")) {
		lmap_result_set_end_epoch(res, value);
	    }
	    if (! strcmp(key, "cycle-number")) {
		lmap_result_set_cycle_number(res, value);
	    }
	    if (! strcmp(key, "status")) {
		lmap_result_set_status(res, value);
	    }
	}
	free(key);
	free(value);
    }
    if (opt) {
	lmap_result_add_option(res, opt);
    }

    (void) fclose(file);
    return res;
}

int
lmapd_workspace_read_results(struct lmapd *lmapd, const int filetype)
{
    char *p;
    struct dirent *dp;
    DIR *dfd;
    struct table *tab;
    struct result *res;

    int had_errors = 0;
    int valid_report = 0;
    int err;

    dfd = opendir(".");
    if (!dfd) {
	lmap_err("failed to open workspace directory '%s'", ".");
	return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
	if (strlen(dp->d_name) < 5) {
	    continue;
	}
	p = strrchr(dp->d_name, '.');
	if (! p) {
	    continue;
	}

	/* note: security issue if len("meta") != len("data") */
	if (!strcmp(p, ".meta")) {
	    int mfd, datafd;
	    mfd = open(dp->d_name, O_RDONLY);
	    if (mfd == -1) {
		lmap_err("failed to open meta file '%s': %s",
			 dp->d_name, strerror(errno));
		had_errors = 1;
		continue;
	    }
	    strcpy(p, ".data");
	    datafd = open(dp->d_name, O_RDONLY);
	    if (datafd == -1) {
		lmap_err("failed to open data file '%s': %s",
			 dp->d_name, strerror(errno));
		(void) close(mfd);
		had_errors = 1;
		continue;
	    }
	    res = read_result(mfd);
	    if (res) {
		err = 1;

		if (filetype == LMAP_FT_CSV) {
		    tab = read_table(datafd);
		    if (tab) {
			lmap_result_add_table(res, tab);
			err = 0;
		    }
		} else {
		    if (!lmap_io_parse_task_results_fd(datafd, filetype, res)) {
			err = 0;
		    }
		}

		if (!err && lmap_result_valid(lmapd->lmap, res)) {
		    lmap_add_result(lmapd->lmap, res);
		    valid_report = 1;
		} else {
		    lmap_err("failed to read data file '%s'", dp->d_name);
		    had_errors = 1;

		    lmap_result_free(res);
		    res = NULL;
		}
	    }
	    (void) close(mfd);
	    (void) close(datafd);
	}
    }
    (void) closedir(dfd);

    if (had_errors && !valid_report)
	return -1;
    return 0;
}
