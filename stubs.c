#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libiberty.h"

#include "stubs.h"

#ifndef HAVE_DCGETTEXT
const char *dcgettext(const char *domain, const char *msg, int category)
{
  return msg;
}
#endif /* !HAVE_DCGETTEXT */

#ifndef HAVE_LIBINTL_DGETTEXT
const char *libintl_dgettext(const char *domain, const char *msg)
{
  return msg;
}
#endif /* !HAVE_LIBINTL_DGETTEXT */

/* fatal error & exit */
void fatal(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	fprintf(stderr, "%s: ", elf2flt_progname);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(1);
}

/* fatal error, perror & exit */
void fatal_perror(const char *format, ...)
{
	int e = errno;
	va_list args;

	va_start(args, format);
	fprintf(stderr, "%s: ", elf2flt_progname);
	vfprintf(stderr, format, args);
	fprintf(stderr, ": %s\n", strerror(e));
	va_end(args);
	exit(1);
}
