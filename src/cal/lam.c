#ifndef lint
static const char	RCSid[] = "$Id: lam.c,v 1.21 2019/07/05 00:46:23 greg Exp $";
#endif
/*
 *  lam.c - simple program to laminate files.
 *
 *	7/14/88		Greg Ward
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "platform.h"
#include "rtio.h"

#define MAXFILE		512		/* maximum number of files */

#define MAXLINE		65536		/* maximum input line */

FILE	*input[MAXFILE];
int	bytsiz[MAXFILE];
char	*tabc[MAXFILE];
int	nfiles = 0;

char	buf[MAXLINE];

int
main(int argc, char *argv[])
{
	long	incnt = 0;
	int	unbuff = 0;
	int	binout = 0;
	char	*curtab = "\t";
	int	curbytes = 0;
	int	puteol;
	int	i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 't':
				curtab = argv[i]+2;
				break;
			case 'u':
				unbuff = !unbuff;
				break;
			case 'i':
				switch (argv[i][2]) {
				case 'n':
					incnt = atol(argv[++i]);
					break;
				case 'f':
					curbytes = sizeof(float);
					break;
				case 'd':
					curbytes = sizeof(double);
					break;
				case 'i':
					curbytes = sizeof(int);
					break;
				case 'w':
					curbytes = 2;
					break;
				case 'b':
					curbytes = 1;
					break;
				case 'a':
					curbytes = argv[i][3] ? -1 : 0;
					break;
				default:
					goto badopt;
				}
				if (isdigit(argv[i][3]))
					curbytes *= atoi(argv[i]+3);
				curbytes += (curbytes == -1);
				if (curbytes > MAXLINE) {
					fputs(argv[0], stderr);
					fputs(": input size too big\n", stderr);
					return(1);
				}
				if (curbytes > 0) {
					curtab = "";
					++binout;
				}
				break;
			case '\0':
				tabc[nfiles] = curtab;
				input[nfiles] = stdin;
				if (curbytes > 0)
					SET_FILE_BINARY(input[nfiles]);
				bytsiz[nfiles++] = curbytes;
				break;
			badopt:;
			default:
				fputs(argv[0], stderr);
				fputs(": bad option\n", stderr);
				return(1);
			}
		} else if (argv[i][0] == '!') {
			tabc[nfiles] = curtab;
			if ((input[nfiles] = popen(argv[i]+1, "r")) == NULL) {
				fputs(argv[i], stderr);
				fputs(": cannot start command\n", stderr);
				return(1);
			}
			if (curbytes > 0)
				SET_FILE_BINARY(input[nfiles]);
			bytsiz[nfiles++] = curbytes;
		} else {
			tabc[nfiles] = curtab;
			if ((input[nfiles] = fopen(argv[i], "r")) == NULL) {
				fputs(argv[i], stderr);
				fputs(": cannot open file\n", stderr);
				return(1);
			}
			if (curbytes > 0)
				SET_FILE_BINARY(input[nfiles]);
			bytsiz[nfiles++] = curbytes;
		}
		if (nfiles >= MAXFILE) {
			fputs(argv[0], stderr);
			fputs(": too many input streams\n", stderr);
			return(1);
		}
	}
	if (!nfiles) {
		fputs(argv[0], stderr);
		fputs(": no input streams\n", stderr);
		return(1);
	}
	if (binout)				/* binary output? */
		SET_FILE_BINARY(stdout);
#ifdef getc_unlocked				/* avoid lock/unlock overhead */
	for (i = nfiles; i--; )
		flockfile(input[i]);
	flockfile(stdout);
#endif
	puteol = 0;				/* any ASCII output at all? */
	for (i = nfiles; i--; )
		puteol += (bytsiz[i] <= 0);
	do {					/* main loop */
		for (i = 0; i < nfiles; i++) {
			if (bytsiz[i] > 0) {		/* binary input */
				if (getbinary(buf, bytsiz[i], 1, input[i]) < 1)
					break;
				putbinary(buf, bytsiz[i], 1, stdout);
			} else if (bytsiz[i] < 0) {	/* multi-line input */
				int	n = -bytsiz[i];
				while (n--) {
					if (fgets(buf, MAXLINE, input[i]) == NULL)
						break;
					if ((i > 0) | (n < -bytsiz[i]-1))
						fputs(tabc[i], stdout);
					buf[strlen(buf)-1] = '\0';
					fputs(buf, stdout);
				}
				if (n >= 0)		/* fell short? */
					break;
			} else {			/* single-line input */
				if (fgets(buf, MAXLINE, input[i]) == NULL)
					break;
				if (i)
					fputs(tabc[i], stdout);
				buf[strlen(buf)-1] = '\0';
				fputs(buf, stdout);
			}
		}
		if (i < nfiles)
			break;
		if (puteol)
			putchar('\n');
		if (unbuff)
			fflush(stdout);
	} while (--incnt);
							/* check ending */
	if (incnt > 0) {
		fputs(argv[0], stderr);
		fputs(": warning: premature EOD\n", stderr);
	}
	return(0);
}
