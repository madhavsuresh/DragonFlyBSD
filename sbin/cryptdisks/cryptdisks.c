/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <libcryptsetup.h>

#include "safe_mem.h"

#define _iswhitespace(X)	((((X) == ' ') || ((X) == '\t'))?1:0)

#define CRYPTDISKS_START	1
#define CRYPTDISKS_STOP		2

static void syntax_error(const char *, ...) __printflike(1, 2);

static int line_no = 1;

static int iswhitespace(char c)
{
	return _iswhitespace(c);
}

static int iscomma(char c)
{
	return (c == ',');
}

static int yesDialog(char *msg __unused)
{
	return 1;
}

static void cmdLineLog(int level __unused, char *msg)
{
	printf("%s", msg);
}

static struct interface_callbacks cmd_icb = {
	.yesDialog = yesDialog,
	.log = cmdLineLog,
};

static void
syntax_error(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	errx(1, "crypttab: syntax error on line %d: %s\n", line_no, buf);
}


static int
entry_check_num_args(char **tokens, int num)
{
	int i;

	for (i = 0; tokens[i] != NULL; i++)
		;

	if (i < num) {
		syntax_error("at least %d tokens were expected but only %d "
		    "were found", num, i);
		return 1;
	}
	return 0;
}

static int
line_tokenize(char *buffer, int (*is_sep)(char), char comment_char, char **tokens)
{
	int c, n, i;
	int quote = 0;

	i = strlen(buffer) + 1;
	c = 0;

	/* Skip leading white-space */
	while ((_iswhitespace(buffer[c])) && (c < i)) c++;

	/*
	 * If this line effectively (after indentation) begins with the comment
	 * character, we ignore the rest of the line.
	 */
	if (buffer[c] == comment_char)
		return 0;

	tokens[0] = &buffer[c];
	for (n = 1; c < i; c++) {
		if (buffer[c] == '"') {
			quote = !quote;
			if (quote) {
				if ((c >= 1) && (&buffer[c] != tokens[n-1])) {
#if 0
					syntax_error("stray opening quote not "
					    "at beginning of token");
					/* NOTREACHED */
#endif
				} else {
					tokens[n-1] = &buffer[c+1];
				}
			} else {
				if ((c < i-1) && (!is_sep(buffer[c+1]))) {
#if 0
					syntax_error("stray closing quote not "
					    "at end of token");
					/* NOTREACHED */
#endif
				} else {
					buffer[c] = '\0';
				}
			}
		}

		if (quote) {
			continue;
		}

		if (is_sep(buffer[c])) {
			buffer[c++] = '\0';
			while ((_iswhitespace(buffer[c])) && (c < i)) c++;
			tokens[n++] = &buffer[c--];
		}
	}
	tokens[n] = NULL;

	if (quote) {
		tokens[0] = NULL;
		return 0;
	}

	return n;
}

static int
parse_crypt_options(struct crypt_options *co, char *option)
{
	char	*parameter, *endptr;
	char	*buf;
	long	lval;
	unsigned long long ullval;
	int	noparam = 0;
	FILE	*fd;

	parameter = strchr(option, '=');
	noparam = (parameter == NULL);
	if (!noparam)
	{
		*parameter = '\0';
		++parameter;
	}

	if (strcmp(option, "tries") == 0) {
		if (noparam)
			syntax_error("The option 'tries' needs a parameter");
			/* NOTREACHED */

		lval = strtol(parameter, &endptr, 10);
		if (*endptr != '\0')
			syntax_error("The option 'tries' expects an integer "
			    "parameter, not '%s'", parameter);
			/* NOTREACHED */

		co->tries = (int)lval;
	} else if (strcmp(option, "timeout") == 0) {
		if (noparam)
			syntax_error("The option 'timeout' needs a parameter");
			/* NOTREACHED */

		ullval = strtoull(parameter, &endptr, 10);
		if (*endptr != '\0')
			syntax_error("The option 'timeout' expects an integer "
			    "parameter, not '%s'", parameter);
			/* NOTREACHED */

		co->timeout = ullval;
	} else if (strcmp(option, "keyscript") == 0) {
		if (noparam)
			syntax_error("The option 'keyscript' needs a parameter");
			/* NOTREACHED */

		/* Allocate safe key memory */
		buf = alloc_safe_mem(8192);

		fd = popen(parameter, "r");
		if ((fread(buf, 1, sizeof(buf), fd)) == 0)
			syntax_error("The 'keyscript' program failed");
			/* NOTREACHED */
		pclose(fd);

		/* Get rid of trailing new-line */
		if ((endptr = strrchr(buf, '\n')) != NULL)
			*endptr = '\0';

		co->passphrase = buf;
	} else if (strcmp(option, "none") == 0) {
		/* Valid option, does nothing */
	} else {
		syntax_error("Unknown option: %s", option);
		/* NOTREACHED */
	}

	return 0;
}

static int
entry_parser(char **tokens, char **options, int type)
{
	struct crypt_options co;
	int r, i, error;

	if (entry_check_num_args(tokens, 2) != 0)
		return 1;

	bzero(&co, sizeof(co));

	co.icb = &cmd_icb;
	co.tries = 3;
	co.name = tokens[0];
	co.device = tokens[1];

	/* (Try to) parse extra options */
	for (i = 0; options[i] != NULL; i++)
		parse_crypt_options(&co, options[i]);

	/* Verify that the device is indeed a LUKS-formatted device */
	error = crypt_isLuks(&co);
	if (error) {
		printf("crypttab: line %d: device %s is not a luks device\n",
		    line_no, co.device);
		return 1;
	}

	if (type == CRYPTDISKS_STOP) {
		/* Check if the device is active */
		r = crypt_query_device(&co);

		/* If r > 0, then the device is active */
		if (r <= 0)
			return 0;

		/* Actually close the device */
		crypt_remove_device(&co);
	} else if (type == CRYPTDISKS_START) {
		if ((tokens[2] != NULL) && (strcmp(tokens[2], "none") != 0)) {
			/* We got a keyfile */
			co.key_file = tokens[2];
		}

		/* Open the device */
		crypt_luksOpen(&co);
	}

	return 0;
}

static int
process_line(FILE* fd, int type)
{
	char buffer[4096];
	char *tokens[256];
	char *options[256];
	int c, n, i = 0;
	int ret = 0;

	while (((c = fgetc(fd)) != EOF) && (c != '\n')) {
		buffer[i++] = (char)c;
		if (i == (sizeof(buffer) -1))
			break;
	}
	buffer[i] = '\0';

	if (feof(fd) || ferror(fd))
		ret = 1;


	n = line_tokenize(buffer, &iswhitespace, '#', tokens);

	/*
	 * If there are not enough arguments for any function or it is
	 * a line full of whitespaces, we just return here. Or if a
	 * quote wasn't closed.
	 */
	if ((n < 2) || (tokens[0][0] == '\0'))
		return ret;

	/*
	 * If there are at least 4 tokens, one of them (the last) is a list
	 * of options.
	 */
	if (n >= 4)
	{
		i = line_tokenize(tokens[3], &iscomma, '#', options);
		if (i == 0)
			syntax_error("Invalid expression in options token");
			/* NOTREACHED */
	}

	entry_parser(tokens, options, type);

	return ret;
}


int
main(int argc, char *argv[])
{
	FILE *fd;
	int ch, start = 0, stop = 0;

	while ((ch = getopt(argc, argv, "01")) != -1) {
		switch (ch) {
		case '1':
			start = 1;
			break;
		case '0':
			stop = 1;
			break;
		default:
			break;
		}
	}

	argc -= optind;
	argv += optind;

	atexit(check_and_purge_safe_mem);

	if ((start && stop) || (!start && !stop))
		errx(1, "please specify exactly one of -0 and -1");

	fd = fopen("/etc/crypttab", "r");
	if (fd == NULL)
		err(1, "fopen");
		/* NOTREACHED */

	while (process_line(fd, (start) ? CRYPTDISKS_START : CRYPTDISKS_STOP) == 0)
		++line_no;

	fclose(fd);
	return 0;
}

