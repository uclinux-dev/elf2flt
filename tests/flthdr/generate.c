/* Helper tool for generating simple flat files that flthdr can read.
 * Written by Mike Frysinger <vapier@gentoo.org>
 * Distributed under the terms of the GNU General Public License v2
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "flat.h"

void usage(int status)
{
	fprintf(status ? stderr : stdout,
		"Usage: generate [options]\n"
		"\n"
		"Helper tool for generating flat files for testing.\n"
		"Flat file is written to stdout by default.\n"
		"\n"
		"Options:\n"
		"  -m <str>  Set the magic (must be 4 bytes)\n"
		"  -v <u32>  Set the version (rev)\n"
		"  -e <u32>  Set the entry point\n"
		"  -d <u32>  Set the data start addr\n"
		"  -D <u32>  Set the data end addr\n"
		"  -B <u32>  Set the bss end addr\n"
		"  -s <u32>  Set the stack size\n"
		"  -r <u32>  Set the reloc start addr\n"
		"  -R <u32>  Set the reloc count\n"
		"  -f <u32>  Set the flags\n"
		"  -b <u32>  Set the build date\n"
		"  -F <u32>  Set the filler field (each -F sets 1 field)\n"
		"  -o <out>  Write output to a file\n"
		"  -h        This help output\n"
	);
	exit(status);
}

void set32(uint32_t *field, const char *val)
{
	char *end;
	long lval;

	errno = 0;
	lval = strtol(val, &end, 0);
	if (*end)
		err(1, "invalid number: %s", val);
	*field = htonl(lval);
}

int main(int argc, char *argv[])
{
	FILE *output = stdout;
	struct flat_hdr hdr;
	int c, fillerpos;

	fillerpos = 0;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, "bFLT", 4);
	hdr.rev = htonl(FLAT_VERSION);

	while ((c = getopt(argc, argv, "ho:m:v:e:d:D:B:s:r:R:f:b:F:")) != -1) {
		switch (c) {
		case 'm': /* magic */
			if (strlen(optarg) != 4)
				errx(1, "magic must be 4 bytes");
			memcpy(hdr.magic, optarg, 4);
			break;
		case 'v': /* revision */
			set32(&hdr.rev, optarg);
			break;
		case 'e': /* entry */
			set32(&hdr.entry, optarg);
			break;
		case 'd': /* data_start */
			set32(&hdr.data_start, optarg);
			break;
		case 'D': /* data_end */
			set32(&hdr.data_end, optarg);
			break;
		case 'B': /* bss_end */
			set32(&hdr.bss_end, optarg);
			break;
		case 's': /* stack_size */
			set32(&hdr.stack_size, optarg);
			break;
		case 'r': /* reloc_start */
			set32(&hdr.reloc_start, optarg);
			break;
		case 'R': /* reloc_count */
			set32(&hdr.reloc_count, optarg);
			break;
		case 'f': /* flags */
			set32(&hdr.flags, optarg);
			break;
		case 'b': /* build_date */
			set32(&hdr.build_date, optarg);
			break;
		case 'F': /* filler */
			if (fillerpos == 5)
				errx(1, "can only use -F 5 times");
			set32(&hdr.filler[fillerpos++], optarg);
			break;
		case 'o': /* output */
			output = fopen(optarg, "wbe");
			if (!output)
				err(1, "could not open %s", optarg);
			break;
		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	fwrite(&hdr, sizeof(hdr), 1, output);
	fclose(output);

	return 0;
}
