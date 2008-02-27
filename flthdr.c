/****************************************************************************/
/*
 *	A simple program to manipulate flat files
 *
 *	Copyright (C) 2001-2003 SnapGear Inc, davidm@snapgear.com
 *	Copyright (C) 2001 Lineo, davidm@lineo.com
 *
 * This is Free Software, under the GNU Public Licence v2 or greater.
 *
 */
/****************************************************************************/

#include <stdio.h>    /* Userland pieces of the ANSI C standard I/O package  */
#include <unistd.h>   /* Userland prototypes of the Unix std system calls    */
#include <time.h>
#include <stdlib.h>   /* exit() */
#include <string.h>   /* strcat(), strcpy() */
#include <assert.h>

/* macros for conversion between host and (internet) network byte order */
#ifndef WIN32
#include <netinet/in.h> /* Consts and structs defined by the internet system */
#define	BINARY_FILE_OPTS
#else
#include <winsock2.h>
#define	BINARY_FILE_OPTS "b"
#endif

#include "compress.h"
#include <libiberty.h>

/* from uClinux-x.x.x/include/linux */
#include "flat.h"     /* Binary flat header description                      */

#if defined(__MINGW32__)
#include <getopt.h>

#define mkstemp(p) mktemp(p)

#endif

/****************************************************************************/

char *program_name;

static int print = 0, docompress = 0, ramload = 0, stacksize = 0, ktrace = 0;

/****************************************************************************/

void
process_file(char *ifile, char *ofile)
{
	int old_flags, old_stack, new_flags, new_stack;
	stream ifp, ofp;
	struct flat_hdr old_hdr, new_hdr;
	char *tfile, tmpbuf[256];
	int input_error, output_error;

	*tmpbuf = '\0';

	if (fopen_stream_u(&ifp, ifile, "r" BINARY_FILE_OPTS)) {
		fprintf(stderr, "Cannot open %s\n", ifile);
		return;
	}

	if (fread_stream(&old_hdr, sizeof(old_hdr), 1, &ifp) != 1) {
		fprintf(stderr, "Cannot read header of %s\n", ifile);
		return;
	}

	if (strncmp(old_hdr.magic, "bFLT", 4) != 0) {
		fprintf(stderr, "Cannot read header of %s\n", ifile);
		return;
	}

	new_flags = old_flags = ntohl(old_hdr.flags);
	new_stack = old_stack = ntohl(old_hdr.stack_size);
	new_hdr = old_hdr;

	if (docompress == 1) {
		new_flags |= FLAT_FLAG_GZIP;
		new_flags &= ~FLAT_FLAG_GZDATA;
	} else if (docompress == 2) {
		new_flags |= FLAT_FLAG_GZDATA;
		new_flags &= ~FLAT_FLAG_GZIP;
	} else if (docompress < 0)
		new_flags &= ~(FLAT_FLAG_GZIP|FLAT_FLAG_GZDATA);
	
	if (ramload > 0)
		new_flags |= FLAT_FLAG_RAM;
	else if (ramload < 0)
		new_flags &= ~FLAT_FLAG_RAM;
	
	if (ktrace > 0)
		new_flags |= FLAT_FLAG_KTRACE;
	else if (ktrace < 0)
		new_flags &= ~FLAT_FLAG_KTRACE;
	
	if (stacksize)
		new_stack = stacksize;

	if (print == 1) {
		time_t t;

		printf("%s\n", ifile);
		printf("    Magic:        %4.4s\n", old_hdr.magic);
		printf("    Rev:          %d\n",    ntohl(old_hdr.rev));
		t = (time_t) htonl(old_hdr.build_date);
		printf("    Build Date:   %s",      t?ctime(&t):"not specified\n");
		printf("    Entry:        0x%x\n",  ntohl(old_hdr.entry));
		printf("    Data Start:   0x%x\n",  ntohl(old_hdr.data_start));
		printf("    Data End:     0x%x\n",  ntohl(old_hdr.data_end));
		printf("    BSS End:      0x%x\n",  ntohl(old_hdr.bss_end));
		printf("    Stack Size:   0x%x\n",  ntohl(old_hdr.stack_size));
		printf("    Reloc Start:  0x%x\n",  ntohl(old_hdr.reloc_start));
		printf("    Reloc Count:  0x%x\n",  ntohl(old_hdr.reloc_count));
		printf("    Flags:        0x%x ( ",  ntohl(old_hdr.flags));
		if (old_flags) {
			if (old_flags & FLAT_FLAG_RAM)
				printf("Load-to-Ram ");
			if (old_flags & FLAT_FLAG_GOTPIC)
				printf("Has-PIC-GOT ");
			if (old_flags & FLAT_FLAG_GZIP)
				printf("Gzip-Compressed ");
			if (old_flags & FLAT_FLAG_GZDATA)
				printf("Gzip-Data-Compressed ");
			if (old_flags & FLAT_FLAG_KTRACE)
				printf("Kernel-Traced-Load ");
			printf(")\n");
		}
	} else if (print > 1) {
		static int first = 1;
		unsigned int text, data, bss, stk, rel, tot;

		if (first) {
			printf("Flag Rev   Text   Data    BSS  Stack Relocs    RAM Filename\n");
			printf("-----------------------------------------------------------\n");
			first = 0;
		}
		*tmpbuf = '\0';
		strcat(tmpbuf, (old_flags & FLAT_FLAG_KTRACE) ? "k" : "");
		strcat(tmpbuf, (old_flags & FLAT_FLAG_RAM) ? "r" : "");
		strcat(tmpbuf, (old_flags & FLAT_FLAG_GOTPIC) ? "p" : "");
		strcat(tmpbuf, (old_flags & FLAT_FLAG_GZIP) ? "z" :
					((old_flags & FLAT_FLAG_GZDATA) ? "d" : ""));
		printf("-%-3.3s ", tmpbuf);
		printf("%3d ", ntohl(old_hdr.rev));
		printf("%6d ", text=ntohl(old_hdr.data_start)-sizeof(struct flat_hdr));
		printf("%6d ", data=ntohl(old_hdr.data_end)-ntohl(old_hdr.data_start));
		printf("%6d ", bss=ntohl(old_hdr.bss_end)-ntohl(old_hdr.data_end));
		printf("%6d ", stk=ntohl(old_hdr.stack_size));
		printf("%6d ", rel=ntohl(old_hdr.reloc_count) * 4);
		/*
		 * work out how much RAM is needed per invocation, this
		 * calculation is dependent on the binfmt_flat implementation
		 */
		tot = data; /* always need data */

		if (old_flags & (FLAT_FLAG_RAM|FLAT_FLAG_GZIP))
			tot += text + sizeof(struct flat_hdr);
		
		if (bss + stk > rel) /* which is bigger ? */
			tot += bss + stk;
		else
			tot += rel;

		printf("%6d ", tot);
		/*
		 * the total depends on whether the relocs are smaller/bigger than
		 * the BSS
		 */
		printf("%s\n", ifile);
	}

	/* if there is nothing else to do, leave */
	if (new_flags == old_flags && new_stack == old_stack)
		return;
	
	new_hdr.flags = htonl(new_flags);
	new_hdr.stack_size = htonl(new_stack);

	tfile = make_temp_file("flthdr");

	if (fopen_stream_u(&ofp, tfile, "w" BINARY_FILE_OPTS)) {
		fprintf(stderr, "Failed to open %s for writing\n", tfile);
		unlink(tfile);
		exit(1);
	}

	/* Copy header (always uncompressed).  */
	if (fwrite_stream(&new_hdr, sizeof(new_hdr), 1, &ofp) != 1) {
		fprintf(stderr, "Failed to write to  %s\n", tfile);
		unlink(tfile);
		exit(1);
	}

	/* Whole input file (including text) is compressed: start decompressing
	   now.  */
	if (old_flags & FLAT_FLAG_GZIP)
		reopen_stream_compressed(&ifp);

	/* Likewise, output file is compressed. Start compressing now.  */
	if (new_flags & FLAT_FLAG_GZIP) {
		printf("zflat %s --> %s\n", ifile, ofile);
		reopen_stream_compressed(&ofp);
	}

	transfer(&ifp, &ofp,
		  ntohl(old_hdr.data_start) - sizeof(struct flat_hdr));

	/* Only data and relocs were compressed in input.  Start decompressing
	   from here.  */
	if (old_flags & FLAT_FLAG_GZDATA)
		reopen_stream_compressed(&ifp);

	/* Only data/relocs to be compressed in output.  Start compressing
	   from here.  */
	if (new_flags & FLAT_FLAG_GZDATA) {
		printf("zflat-data %s --> %s\n", ifile, ofile);
		reopen_stream_compressed(&ofp);
	}

	transfer(&ifp, &ofp, -1);

	input_error = ferror_stream(&ifp);
	output_error = ferror_stream(&ofp);

	if (input_error || output_error) {
		fprintf(stderr, "Error on file pointer%s%s\n",
				input_error ? " input" : "",
				output_error ? " output" : "");
		unlink(tfile);
		exit(1);
	}

	fclose_stream(&ifp);
	fclose_stream(&ofp);

	/* Copy temporary file to output location.  */
	fopen_stream_u(&ifp, tfile, "r" BINARY_FILE_OPTS);
	fopen_stream_u(&ofp, ofile, "w" BINARY_FILE_OPTS);

	transfer(&ifp, &ofp, -1);

	fclose_stream(&ifp);
	fclose_stream(&ofp);

	unlink(tfile);
	free(tfile);
}

/****************************************************************************/

void
usage(char *s)
{
	if (s)
		fprintf(stderr, "%s\n", s);
	fprintf(stderr, "usage: %s [options] flat-file\n", program_name);
	fprintf(stderr, "       Allows you to change an existing flat file\n\n");
	fprintf(stderr, "       -p      : print current settings\n");
	fprintf(stderr, "       -z      : compressed flat file\n");
	fprintf(stderr, "       -d      : compressed data-only flat file\n");
	fprintf(stderr, "       -Z      : un-compressed flat file\n");
	fprintf(stderr, "       -r      : ram load\n");
	fprintf(stderr, "       -R      : do not RAM load\n");
	fprintf(stderr, "       -k      : kernel traced load (for debug)\n");
	fprintf(stderr, "       -K      : normal non-kernel traced load\n");
	fprintf(stderr, "       -s size : stack size\n");
	fprintf(stderr, "       -o file : output-file\n"
	                "                 (default is to modify input file)\n");
	exit(1);
}

/****************************************************************************/

int
main(int argc, char *argv[])
{
	int c;
	char *ofile = NULL, *ifile;

	program_name = argv[0];

	while ((c = getopt(argc, argv, "pdzZrRkKs:o:")) != EOF) {
		switch (c) {
		case 'p': print = 1;                break;
		case 'z': docompress = 1;           break;
		case 'd': docompress = 2;           break;
		case 'Z': docompress = -1;          break;
		case 'r': ramload = 1;              break;
		case 'R': ramload = -1;             break;
		case 'k': ktrace = 1;               break;
		case 'K': ktrace = -1;              break;
		case 'o': ofile = optarg;           break;
		case 's':
			if (sscanf(optarg, "%i", &stacksize) != 1)
				usage("invalid stack size");
			break;
		default:
			usage("invalid option");
			break;
		}
	}

	if (optind >= argc)
		usage("No input files provided");

	if (ofile && argc - optind > 1)
		usage("-o can only be used with a single file");

	if (!print && !docompress && !ramload && !stacksize) /* no args == print */
		print = argc - optind; /* greater than 1 is short format */
	
	for (c = optind; c < argc; c++) {
		ifile = argv[c];
		if (!ofile)
			ofile = ifile;
		process_file(ifile, ofile);
		ofile = NULL;
	}
	
	exit(0);
}

/****************************************************************************/
