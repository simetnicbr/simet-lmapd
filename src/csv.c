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

#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "utils.h"
#include "csv.h"

/**
 * xrealloc() - reliable realloc()
 *
 * Unlike standard realloc, buf = xrealloc(buf, size) will not leak
 * memory on failure.  On failure, xrealloc() returns buf with errno
 * set to EINVAL.  On success, xrealloc() has the same semantics
 * as realloc(), and sets errno to 0.
 */
static void *
xrealloc(void *ptr, size_t size, const char *func)
{
    char *p = realloc(ptr, size);
    if (!p) {
        lmap_log(LOG_ERR, func, "failed to allocate memory");
	errno = ENOMEM;
	return ptr;
    }
    errno = 0;
    return p;
}

#define xfree(p) free(p)

static void
append(FILE *file, const char delimiter, const char *field)
{
    int i, need_quote = 0;
    const char quote = '"';

    if (! field) {
	fprintf(file, "\n");
	return;
    }

    for (i = 0; field[i]; i++) {
	if ((delimiter && field[i] == delimiter)
	    || field[i] == quote || isspace(field[i])) {
	    need_quote = 1;
	    break;
	}
    }

    if (need_quote) {
	fputc(quote, file);
	for (i = 0; field[i]; i++) {
	    fputc(field[i], file);
	    if (field[i] == quote) {
		fputc(field[i], file);
	    }
	}
	fputc(quote, file);
    } else {
	fputs(field, file);
    }
}

/**
 * @brief Append a field to a CSV file
 *
 * Appends a string value as a field to a CSV file. The string is
 * quoted if the string value contains the delimiter or white space or
 * the quote character. See RFC 4180 for more details.
 *
 * If the delimiter is 0, then no delimiter will be printed (use this
 * only at the beginning of a record). If the string s is NULL, then
 * the current record (aka line) will be terminated.
 *
 * @param file pointer to the open FILE stream
 * @param delimiter delimiter character or 0 (no delimiter)
 * @param field value of the field to append or NULL (end of record)
 */

void
csv_append(FILE *file, const char delimiter, const char *field)
{
    fputc(delimiter, file);
    append(file, delimiter, field);
}

void
csv_start(FILE *file, const char delimiter, const char *field)
{
    append(file, delimiter, field);
}

void
csv_end(FILE *file)
{
    append(file, 0, NULL);
}

void
csv_append_key_value(FILE *file, const char delimiter,
		     const char *key, const char *value)
{
    if (key && value) {
	csv_start(file, delimiter, key);
	csv_append(file, delimiter, value);
	csv_end(file);
    }
}

char*
csv_next(FILE *file, const char delimiter)
{
    int ic, quoted = 0;
    size_t i, size = 0;
    char *buf = NULL;
    const char quote = '"';
    char c;

    i = 0;
    while ((ic = fgetc(file)) != EOF) {
	c = (char)ic;

	if (!quoted && c == delimiter) {
	    break;
	}
	if (c == '\n') {
	    if (i == 0) {
		xfree(buf);
		errno = 0;
		return NULL;
	    } else {
		ungetc(c, file);
		break;
	    }
	}
	if (i == 0 && !quoted && isspace(c)) {
	    continue;
	}
	if (i == 0 && c == quote) {
	    quoted = 1;
	    continue;
	}
	if (i >= size) {
	    size += 64;
	    buf = xrealloc(buf, size, __FUNCTION__);
	    if (!buf || errno) {
		xfree(buf);
		errno = ENOMEM;
		return NULL;
	    }
	}
	if (c == quote) {
	    if (quoted) {
		if ((ic = fgetc(file)) == EOF) {
		    break;
		}
		c = (char)ic;
		if (c == delimiter) {
		    break;
		}
		if (c == '\n') {
		    break;
		}
		buf[i] = c;
	    } else {
		buf[i] = c;
	    }
	} else {
	    buf[i] = c;
	}
	i++;
    }

    if (buf) {
	if (i >= size) {
	    size += 2;
	    buf = xrealloc(buf, size, __FUNCTION__);
	    if (!buf || errno) {
		xfree(buf);
		errno = ENOMEM;
		return NULL;
	    }
	}
	buf[i] = 0;
    }
    errno = 0;
    return buf;
}

void
csv_next_key_value(FILE *file, const char delimiter, char **key, char **value)
{
    char *s;
    char *v;

    if (key) {
	*key = NULL;
    }
    if (value) {
	*value = NULL;
    }
    if (feof(file)) {
	errno = 0;
	return;
    }

    while ((s = csv_next(file, delimiter)) == NULL) {
	if (errno) {
	    return;
	}
	if (feof(file)) {
	    errno = 0;
	    return;
	}
    }

    v = csv_next(file, delimiter);
    if (!v && errno) {
	xfree(s);
	return;
    }

    if (key) {
	*key = s;
    } else {
	xfree(s);
    }
    if (value) {
	*value = v;
    } else {
	xfree(v);
    }
    errno = 0;
}

