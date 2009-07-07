/* macros for conversion between host and (internet) network byte order */
#ifndef WIN32
# include <netinet/in.h> /* Consts and structs defined by the internet system */
# define BINARY_FILE_OPTS
#else
# include <winsock2.h>
# define BINARY_FILE_OPTS "b"
#endif

#ifndef __WIN32
# include <sys/wait.h>
#endif
#ifndef WIFSIGNALED
# define WIFSIGNALED(S) (((S) & 0xff) != 0 && ((S) & 0xff) != 0x7f)
#endif
#ifndef WTERMSIG
# define WTERMSIG(S) ((S) & 0x7f)
#endif
#ifndef WIFEXITED
# define WIFEXITED(S) (((S) & 0xff) == 0)
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(S) (((S) & 0xff00) >> 8)
#endif
#ifndef WCOREDUMP
# define WCOREDUMP(S) ((S) & WCOREFLG)
#endif
#ifndef WCOREFLG
# define WCOREFLG 0200
#endif
#ifndef HAVE_STRSIGNAL
# define strsignal(sig) "SIG???"
#endif

extern const char *elf2flt_progname;

void fatal(const char *, ...);
void fatal_perror(const char *, ...);
