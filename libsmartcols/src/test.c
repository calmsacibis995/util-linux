/*
 * TT - Table or Tree, features:
 * - column width could be defined as absolute or relative to the terminal width
 * - allows to truncate or wrap data in columns
 * - prints tree if parent->child relation is defined
 * - draws the tree by ASCII or UTF8 lines (depends on terminal setting)
 *
 * Copyright (C) 2010-2014 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <getopt.h>

#include "c.h"
#include "nls.h"
#include "procutils.h"
#include "strutils.h"
#include "colors.h"

#include "libsmartcols.h"

static int add_children(struct libscols_table *tb,
			struct libscols_line *ln, int fd);


enum { COL_MODE, COL_SIZE, COL_NAME };

static void set_columns(struct libscols_table *tb, int notree)
{
	if (!scols_table_new_column(tb, "MODE", 0.3, 0))
		goto fail;
	if (!scols_table_new_column(tb, "SIZE", 5, SCOLS_FL_RIGHT))
		goto fail;
	if (!scols_table_new_column(tb, "NAME", 0.5,
			(notree ? 0 : SCOLS_FL_TREE) | SCOLS_FL_NOEXTREMES))
		goto fail;

	return;
fail:
	scols_unref_table(tb);
	err(EXIT_FAILURE, "faild to create output columns");
}

static int add_line_from_stat(struct libscols_table *tb,
			      struct libscols_line *parent,
			      int parent_fd,
			      struct stat *st,
			      const char *name)
{
	struct libscols_line *ln;
	char modbuf[11], *p;
	mode_t mode = st->st_mode;
	int rc = 0;

	ln = scols_table_new_line(tb, parent);
	if (!ln)
		err(EXIT_FAILURE, "failed to create output line");

	/* MODE; local buffer, use scols_line_set_data() that calls strdup() */
	strmode(mode, modbuf);
	if (scols_line_set_data(ln, COL_MODE, modbuf))
		goto fail;

	/* SIZE; already allocated string, use scols_line_refer_data() */
	p = size_to_human_string(0, st->st_size);
	if (!p || scols_line_refer_data(ln, COL_SIZE, p))
		goto fail;

	/* NAME */
	if (scols_line_set_data(ln, COL_NAME, name))
		goto fail;

	/* colors */
	if (scols_table_colors_wanted(tb)) {
		struct libscols_cell *ce = scols_line_get_cell(ln, COL_NAME);

		if (S_ISDIR(mode))
			scols_cell_set_color(ce, "blue");
		else if (S_ISLNK(mode))
			scols_cell_set_color(ce, "cyan");
		else if (S_ISBLK(mode))
			scols_cell_set_color(ce, "magenta");
		else if ((mode & S_IXOTH) || (mode & S_IXGRP) || (mode & S_IXUSR))
			scols_cell_set_color(ce, "green");
	}

	if (S_ISDIR(st->st_mode)) {
		int fd;

		if (parent_fd >= 0)
			fd = openat(parent_fd, name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		else
			fd = open(name, O_RDONLY|O_NONBLOCK|O_DIRECTORY|O_CLOEXEC);
		if (fd >= 0) {
			rc = add_children(tb, ln, fd);
			close(fd);
		}
	}
	return rc;
fail:
	err(EXIT_FAILURE, "failed to create cell data");
	return -1;
}

/* read all entrines from directory @fd */
static int add_children(struct libscols_table *tb,
			struct libscols_line *ln,
			int fd)
{
	DIR *dir;
	struct dirent *d;

	dir = fdopendir(fd);
	if (!dir)
		return -errno;

	while ((d = readdir(dir))) {
		struct stat st;

		if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
			continue;
		if (fstatat(fd, d->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;
		add_line_from_stat(tb, ln, fd, &st, d->d_name);
	}
	closedir(dir);
	return 0;
}

static void add_lines(struct libscols_table *tb, const char *dirname)
{
	struct stat st;

	if (lstat(dirname, &st))
		err(EXIT_FAILURE, "%s", dirname);

	add_line_from_stat(tb, NULL, -1, &st, dirname);
}

static void __attribute__((__noreturn__)) usage(FILE *out)
{
	fputs(USAGE_HEADER, out);
	fprintf(out, _(" %s [options] [<dir> ...]\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -i, --ascii          use ascii characters only\n"), out);
	fputs(_(" -l, --list           use list format output\n"), out);
	fputs(_(" -n, --noheadings     don't print headings\n"), out);
	fputs(_(" -p, --pairs          use key=\"value\" output format\n"), out);
	fputs(_(" -r, --raw            use raw output format\n"), out);

	fputs(USAGE_SEPARATOR, out);
	fputs(USAGE_HELP, out);
	fputs(USAGE_VERSION, out);

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	struct libscols_table *tb;
	int c, notree = 0, clonetb = 0;

	static const struct option longopts[] = {
		{ "help",       0, 0, 'h' },
		{ "noheadings",	0, 0, 'n' },
		{ "list",       0, 0, 'l' },
		{ "ascii",	0, 0, 'i' },
		{ "raw",        0, 0, 'r' },
		{ "pairs",      0, 0, 'p' },
		{ "clone",      0, 0, 'C' },
		{ NULL, 0, 0, 0 },
	};

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	scols_init_debug(0);

	tb = scols_new_table();
	if (!tb)
		err(EXIT_FAILURE, "faild to create output table");

	while((c = getopt_long(argc, argv, "nlirpC", longopts, NULL)) != -1) {
		switch(c) {
		case 'h':
			usage(stdout);
			break;
		case 'l':
			notree = 1;
			break;
		case 'n':
			scols_table_enable_noheadings(tb, 1);
			break;
		case 'p':
			scols_table_enable_export(tb, 1);
			notree = 1;
			break;
		case 'i':
			scols_table_enable_ascii(tb, 1);
			break;
		case 'r':
			scols_table_enable_raw(tb, 1);
			notree = 1;
			break;
		case 'C':
			clonetb = 1;
		default:
			usage(stderr);
		}
	}

	scols_table_enable_colors(tb, 1);
	set_columns(tb, notree);

	while (optind < argc)
		add_lines(tb, argv[optind++]);

	if (clonetb) {
		struct libscols_table *xtb = scols_copy_table(tb);

		scols_print_table(xtb);
		scols_unref_table(xtb);
	} else
		scols_print_table(tb);

	scols_unref_table(tb);

	return EXIT_SUCCESS;
}
