/*
 * elf2flt.c: Convert ELF (or any BFD format) to FLAT binary format
 *
 * (c) 1999-2002, Greg Ungerer <gerg@snapgear.com>
 * Created elf2flt from coff2flt (see copyrights below). Added all the
 * ELF format file handling. Extended relocation support for all of
 * text and data.
 *
 * (c) 2003, H8 support <davidm@snapgear.com>
 * (c) 2001-2003, arm/arm-pic/arm-big-endian support <davidm@snapgear.com>
 * (c) 2001, v850 changes, Mile Bader <miles@lsi.nec.co.jp>
 * (c) 2003, SuperH support, Paul Mundt <lethal@linux-sh.org>
 * (c) 2001, zflat support <davidm@snapgear.com>
 * (c) 2001, Changes for GOT entries Paul Dale <pauli@snapgear.com> and
 *           David McCullough <davidm@snapgear.com>
 *
 * Now supports PIC with GOT tables.  This works by taking a '.elf' file
 * and a fully linked elf executable (at address 0) and produces a flat
 * file that can be loaded with some fixups.  It still supports the old
 * style fully relocatable elf format files.
 *
 * Originally obj-res.c
 *
 * (c) 1998, Kenneth Albanowski <kjahds@kjahds.com>
 * (c) 1998, D. Jeff Dionne
 * (c) 1998, The Silver Hammer Group Ltd.
 * (c) 1996, 1997 Dionne & Associates <jeff@ryeham.ee.ryerson.ca>
 *
 * This is Free Software, under the GNU Public Licence v2 or greater.
 *
 * Relocation added March 1997, Kresten Krab Thorup 
 * krab@california.daimi.aau.dk
 */
 
#include <stdio.h>    /* Userland pieces of the ANSI C standard I/O package  */
#include <stdlib.h>   /* Userland prototypes of the ANSI C std lib functions */
#include <stdarg.h>   /* Allows va_list to exist in the these namespaces     */
#include <string.h>   /* Userland prototypes of the string handling funcs    */
#include <strings.h>
#include <unistd.h>   /* Userland prototypes of the Unix std system calls    */
#include <fcntl.h>    /* Flag value for file handling functions              */
#include <time.h>

#include <netinet/in.h> /* Consts and structs defined by the internet system */

/* from $(INSTALLDIR)/include       */
#include <bfd.h>      /* Main header file for the BFD library                */

#if defined(TARGET_h8300)
#include <elf/h8.h>      /* TARGET_* ELF support for the BFD library            */
#else
#include <elf.h>      /* TARGET_* ELF support for the BFD library            */
#endif

/* from uClinux-x.x.x/include/linux */
#include "flat.h"     /* Binary flat header description                      */


#ifdef TARGET_v850e
#define TARGET_v850
#endif

#if defined(TARGET_m68k)
#define	ARCH	"m68k/coldfire"
#elif defined(TARGET_arm)
#define	ARCH	"arm"
#elif defined(TARGET_sparc)
#define	ARCH	"sparc"
#elif defined(TARGET_v850)
#define	ARCH	"v850"
#elif defined(TARGET_sh)
#define	ARCH	"sh"
#elif defined(TARGET_h8300)
#define	ARCH	"h8300"
#elif defined(TARGET_microblaze)
#define ARCH	"microblaze"
#else
#error "Don't know how to support your CPU architecture??"
#endif

/*
 * Define a maximum number of bytes allowed in the offset table.
 * We'll fail if the table is larger than this.
 *
 * This limit may be different for platforms other than m68k, but
 * 8000 entries is a lot,  trust me :-) (davidm)
 */
#define GOT_LIMIT 32767

#ifndef O_BINARY
#define O_BINARY 0
#endif


int verbose = 0;      /* extra output when running */
int pic_with_got = 0; /* do elf/got processing with PIC code */
int load_to_ram = 0;  /* instruct loader to allocate everything into RAM */
int compress = 0;     /* 1 = compress everything, 2 = compress data only */
int use_resolved = 0; /* If true, get the value of symbol references from */
		      /* the program contents, not from the relocation table. */
		      /* In this case, the input ELF file must be already */
		      /* fully resolved (using the `-q' flag with recent */
		      /* versions of GNU ld will give you a fully resolved */
		      /* output file with relocation entries).  */

const char *progname, *filename;
int lineno;

int nerrors = 0;
int nwarnings = 0;

static char where[200];

enum {
  /* Use exactly one of these: */
  E_NOFILE = 0,         /* "progname: " */
  E_FILE = 1,           /* "filename: " */
  E_FILELINE = 2,       /* "filename:lineno: " */
  E_FILEWHERE = 3,      /* "filename:%s: " -- set %s with ewhere() */
          
  /* Add in any of these with |': */
  E_WARNING = 0x10,
  E_PERROR = 0x20
};
                  
void ewhere (const char *format, ...);
void einfo (int type, const char *format, ...);
                  

void
ewhere (const char *format, ...) {
  va_list args;
  va_start (args, format);
  vsprintf (where, format, args);
  va_end (args);
}


void
einfo (int type, const char *format, ...) {
  va_list args;

  switch (type & 0x0f) {
  case E_NOFILE:
    fprintf (stderr, "%s: ", progname);
    break;
  case E_FILE:
    fprintf (stderr, "%s: ", filename);
    break;
  case E_FILELINE:
    ewhere ("%d", lineno);
    /* fall-through */
  case E_FILEWHERE:
    fprintf (stderr, "%s:%s: ", filename, where);
    break;
  }

  if (type & E_WARNING) {
    fprintf (stderr, "warning: ");
    nwarnings++;
  } else {
    nerrors++;
  }

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);

  if (type & E_PERROR)
    perror ("");
  else
    fprintf (stderr, "\n");
}


asymbol**
get_symbols (bfd *abfd, long *num)
{
  long storage_needed;
  asymbol **symbol_table;
  long number_of_symbols;
  
  storage_needed = bfd_get_symtab_upper_bound (abfd);
	  
  if (storage_needed < 0)
    abort ();
      
  if (storage_needed == 0)
    return NULL;

  symbol_table = (asymbol **) malloc (storage_needed);

  number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);
  
  if (number_of_symbols < 0) 
    abort ();

  *num = number_of_symbols;
  return symbol_table;
}



int
dump_symbols(asymbol **symbol_table, long number_of_symbols)
{
  long i;
  printf("SYMBOL TABLE:\n");
  for (i=0; i<number_of_symbols; i++) {
	printf("  NAME=%s  VALUE=0x%x\n", symbol_table[i]->name,
		symbol_table[i]->value);
  }
  printf("\n");
  return(0);
}  



long
get_symbol_offset(char *name, asection *sec, asymbol **symbol_table, long number_of_symbols)
{
  long i;
  for (i=0; i<number_of_symbols; i++) {
    if (symbol_table[i]->section == sec) {
      if (!strcmp(symbol_table[i]->name, name)) {
	return symbol_table[i]->value;
      }
    }
  }
  return -1;
}  



long
add_com_to_bss(asymbol **symbol_table, long number_of_symbols, long bss_len)
{
  long i, comsize;
  long offset;

  comsize = 0;
  for (i=0; i<number_of_symbols; i++) {
    if (strcmp("*COM*", symbol_table[i]->section->name) == 0) {
      offset = bss_len + comsize;
      comsize += symbol_table[i]->value;
      symbol_table[i]->value = offset;
    }
  }
  return comsize;
}  



unsigned long *
output_relocs (
  bfd *abs_bfd,
  asymbol **symbols,
  int number_of_symbols,
  unsigned long *n_relocs,
  unsigned char *text, int text_len, unsigned char *data, int data_len,
  bfd *rel_bfd)
{
  unsigned long		*flat_relocs;
  asection		*a, *sym_section, *r;
  arelent		**relpp, **p, *q;
  const char		*sym_name, *section_name;
  unsigned char		*sectionp;
  unsigned long		pflags;
  char			addstr[16];
  long			sym_addr, sym_vma, section_vma;
  int			relsize, relcount;
  int			flat_reloc_count;
  int			sym_reloc_size, rc;
  int			got_size = 0;
  int			bad_relocs = 0;
  asymbol		**symb;
  long			nsymb;
  
#if 0
  printf("%s(%d): output_relocs(abs_bfd=%d,synbols=%x,number_of_symbols=%d"
	"n_relocs=%x,text=%x,text_len=%d,data=%x,data_len=%d)\n",
	__FILE__, __LINE__, abs_bfd, symbols, number_of_symbols, n_relocs,
	text, text_len, data, data_len);
#endif

#if 0
dump_symbols(symbols, number_of_symbols);
#endif

  *n_relocs = 0;
  flat_relocs = NULL;
  flat_reloc_count = 0;
  rc = 0;
  pflags = 0;

  /* Determine how big our offset table is in bytes.
   * This isn't too difficult as we've terminated the table with -1.
   * Also note that both the relocatable and absolute versions have this
   * terminator even though the relocatable one doesn't have the GOT!
   */
  if (pic_with_got) {
    unsigned long *lp = (unsigned long *)data;
    /* Should call ntohl(*lp) here but is isn't going to matter */
    while (*lp != 0xffffffff) lp++;
    got_size = ((unsigned char *)lp) - data;
    if (verbose)
	    printf("GOT table contains %d entries (%d bytes)\n",
			    got_size/sizeof(unsigned long), got_size);
#ifdef TARGET_m68k
    if (got_size > GOT_LIMIT) {
	    fprintf(stderr, "GOT too large: %d bytes (limit = %d bytes)\n",
			    got_size, GOT_LIMIT);
	    exit(1);
    }
#endif
  }

  for (a = abs_bfd->sections; (a != (asection *) NULL); a = a->next) {
  	section_vma = bfd_section_vma(abs_bfd, a);

	if (verbose)
		printf("SECTION: %s [%x]: flags=%x vma=%x\n", a->name, a,
			a->flags, section_vma);

//	if (bfd_is_abs_section(a))
//		continue;
	if (bfd_is_und_section(a))
		continue;
	if (bfd_is_com_section(a))
		continue;
//	if ((a->flags & SEC_RELOC) == 0)
//		continue;

	/*
	 *	Only relocate things in the data sections if we are PIC/GOT.
	 *	otherwise do text as well
	 */
	if (!pic_with_got && strcmp(".text", a->name) == 0)
		sectionp = text;
	else if (strcmp(".data", a->name) == 0)
		sectionp = data;
	else
		continue;

	/* Now search for the equivalent section in the relocation binary
	 * and use that relocation information to build reloc entries
	 * for this one.
	 */
	for (r=rel_bfd->sections; r != NULL; r=r->next)
		if (strcmp(a->name, r->name) == 0)
			break;
	if (r == NULL)
	  continue;
	if (verbose)
	  printf(" RELOCS: %s [%x]: flags=%x vma=%x\n", r->name, r,
			r->flags, bfd_section_vma(abs_bfd, r));
  	if ((r->flags & SEC_RELOC) == 0)
  	  continue;
	relsize = bfd_get_reloc_upper_bound(rel_bfd, r);
	if (relsize <= 0) {
		if (verbose)
			printf("%s(%d): no relocation entries section=%x\n",
				__FILE__, __LINE__, r->name);
		continue;
	}

	symb = get_symbols(rel_bfd, &nsymb);
	relpp = (arelent **) xmalloc(relsize);
	relcount = bfd_canonicalize_reloc(rel_bfd, r, relpp, symb);
	if (relcount <= 0) {
		if (verbose)
			printf("%s(%d): no relocation entries section=%s\n",
			__FILE__, __LINE__, r->name);
		continue;
	} else {
		for (p = relpp; (relcount && (*p != NULL)); p++, relcount--) {
			int relocation_needed = 0;

#ifdef TARGET_microblaze
			/* The MICROBLAZE_XX_NONE relocs can be skipped.
			   They represent PC relative branches that the
			   linker has already resolved */
				
			switch ((*p)->howto->type) 
			{
			case R_MICROBLAZE_NONE:
			case R_MICROBLAZE_64_NONE:
				continue;
			}
#endif /* TARGET_microblaze */
			   
#ifdef TARGET_v850
			/* Skip this relocation entirely if possible (we
			   do this early, before doing any other
			   processing on it).  */
			switch ((*p)->howto->type) {
#ifdef R_V850_9_PCREL
			case R_V850_9_PCREL:
#endif
#ifdef R_V850_22_PCREL
			case R_V850_22_PCREL:
#endif
#ifdef R_V850_SDA_16_16_OFFSET
			case R_V850_SDA_16_16_OFFSET:
#endif
#ifdef R_V850_SDA_15_16_OFFSET
			case R_V850_SDA_15_16_OFFSET:
#endif
#ifdef R_V850_ZDA_15_16_OFFSET
			case R_V850_ZDA_15_16_OFFSET:
#endif
#ifdef R_V850_TDA_6_8_OFFSET
			case R_V850_TDA_6_8_OFFSET:
#endif
#ifdef R_V850_TDA_7_8_OFFSET
			case R_V850_TDA_7_8_OFFSET:
#endif
#ifdef R_V850_TDA_7_7_OFFSET
			case R_V850_TDA_7_7_OFFSET:
#endif
#ifdef R_V850_TDA_16_16_OFFSET
			case R_V850_TDA_16_16_OFFSET:
#endif
#ifdef R_V850_TDA_4_5_OFFSET
			case R_V850_TDA_4_5_OFFSET:
#endif
#ifdef R_V850_TDA_4_4_OFFSET
			case R_V850_TDA_4_4_OFFSET:
#endif
#ifdef R_V850_SDA_16_16_SPLIT_OFFSET
			case R_V850_SDA_16_16_SPLIT_OFFSET:
#endif
#ifdef R_V850_CALLT_6_7_OFFSET
			case R_V850_CALLT_6_7_OFFSET:
#endif
#ifdef R_V850_CALLT_16_16_OFFSET
			case R_V850_CALLT_16_16_OFFSET:
#endif
				/* These are relative relocations, which
				   have already been fixed up by the
				   linker at this point, so just ignore
				   them.  */ 
				continue;
			}
#endif /* USE_V850_RELOCS */

			q = *p;
			if (q->sym_ptr_ptr && *q->sym_ptr_ptr) {
				sym_name = (*(q->sym_ptr_ptr))->name;
				sym_section = (*(q->sym_ptr_ptr))->section;
				section_name=(*(q->sym_ptr_ptr))->section->name;
			} else {
				printf("ERROR: undefined relocation entry\n");
				rc = -1;
				continue;
			}
			/* Adjust the address to account for the GOT table which wasn't
			 * present in the relative file link.
			 */
			if (pic_with_got)
			  q->address += got_size;
					
			/*
			 *	Fixup offset in the actual section.
			 */
			addstr[0] = 0;
  			if ((sym_addr = get_symbol_offset((char *) sym_name,
			    sym_section, symbols, number_of_symbols)) == -1) {
				sym_addr = 0;
			}

			if (use_resolved) {
				/* Use the address of the symbol already in
				   the program text.  The alignment of the
				   build host might be stricter, and the
				   endian-ness different, than that of the
				   target, so we have to be careful. */
				unsigned char *p = sectionp + q->address;
				if (bfd_big_endian (abs_bfd))
					sym_addr =
						(p[0] << 24) + (p[1] << 16)
						+ (p[2] << 8) + p[3];
				else
					sym_addr =
						p[0] + (p[1] << 8)
						+ (p[2] << 16) + (p[3] << 24);
				relocation_needed = 1;
			} else {
				/* Calculate the sym address ourselves.  */
				sym_reloc_size = bfd_get_reloc_size(q->howto);

#ifndef TARGET_h8300
				if (sym_reloc_size != 4) {
					printf("ERROR: bad reloc type %d size=%d for symbol=%s\n",
							(*p)->howto->type, sym_reloc_size, sym_name);
					bad_relocs++;
					rc = -1;
					continue;
				}
#endif

				switch ((*p)->howto->type) {

#if defined(TARGET_m68k)
				case R_68K_32:
					relocation_needed = 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_68K_PC32:
					sym_vma = 0;
					sym_addr += sym_vma + q->addend;
					sym_addr -= q->address;
					break;
#endif

#if defined(TARGET_arm)
				case R_ARM_ABS32:
					relocation_needed = 1;
					if (verbose)
						fprintf(stderr,
							"%s vma=0x%x, value=0x%x, address=0x%x "
							"sym_addr=0x%x rs=0x%x, opcode=0x%x\n",
							"ABS32",
							sym_vma, (*(q->sym_ptr_ptr))->value,
							q->address, sym_addr,
							(*p)->howto->rightshift,
							*((unsigned long *) (sectionp + q->address)));
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_ARM_GOT32:
				case R_ARM_GOTPC:
					/* Should be fine as is */
					break;
				case R_ARM_PLT32:
					if (verbose)
						fprintf(stderr,
							"%s vma=0x%x, value=0x%x, address=0x%x "
							"sym_addr=0x%x rs=0x%x, opcode=0x%x\n",
							"PLT32",
							sym_vma, (*(q->sym_ptr_ptr))->value,
							q->address, sym_addr,
							(*p)->howto->rightshift,
							*((unsigned long *) (sectionp + q->address)));
				case R_ARM_PC24:
					sym_vma = 0;
					sym_addr = (sym_addr-q->address)>>(*p)->howto->rightshift;
					break;
#endif

#ifdef TARGET_v850
				case R_V850_32:
					relocation_needed = 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
#if defined(R_V850_ZDA_16_16_OFFSET) || defined(R_V850_ZDA_16_16_SPLIT_OFFSET)
#ifdef R_V850_ZDA_16_16_OFFSET
				case R_V850_ZDA_16_16_OFFSET:
#endif
#ifdef R_V850_ZDA_16_16_SPLIT_OFFSET
				case R_V850_ZDA_16_16_SPLIT_OFFSET:
#endif
					/* Can't support zero-relocations.  */
					printf ("ERROR: %s+0x%x: zero relocations not supported\n",
							sym_name, q->addend);
					continue;
#endif /* R_V850_ZDA_16_16_OFFSET || R_V850_ZDA_16_16_SPLIT_OFFSET */
#endif /* TARGET_v850 */

#ifdef TARGET_h8300
				case R_H8_DIR24R8:
					if (sym_reloc_size != 4) {
						printf("R_H8_DIR24R8 size %d\n", sym_reloc_size);
						bad_relocs++;
						continue;
					}
					relocation_needed = 1;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					q->address -= 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr |= (*((unsigned char *)(sectionp+q->address))<<24);
					break;
				case R_H8_DIR24A8:
					if (sym_reloc_size != 4) {
						printf("R_H8_DIR24A8 size %d\n", sym_reloc_size);
						bad_relocs++;
						continue;
					}
					relocation_needed = 1;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_H8_DIR32:
				case R_H8_DIR32A16: /* currently 32,  could be made 16 */
					if (sym_reloc_size != 4) {
						printf("R_H8_DIR32 size %d\n", sym_reloc_size);
						bad_relocs++;
						continue;
					}
					relocation_needed = 1;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_H8_PCREL16:
					sym_vma = 0;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 2);
					if (bfd_big_endian(abs_bfd))
					* ((unsigned short *) (sectionp + q->address)) =
						bfd_big_endian(abs_bfd) ? htons(sym_addr) : sym_addr;
					continue;
				case R_H8_PCREL8:
					sym_vma = 0;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 1);
					* ((unsigned char *) (sectionp + q->address)) = sym_addr;
					continue;
#endif

#ifdef TARGET_microblaze
				case R_MICROBLAZE_64:
		/* The symbol is split over two consecutive instructions.  
		   Flag this to the flat loader by setting the high bit of 
		   the relocation symbol. */
				{
					unsigned char *p = sectionp + q->address;
					unsigned long offset;
					pflags=0x80000000;

					/* work out the relocation */
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					/* grab any offset from the text */
					offset = (p[2]<<24) + (p[3] << 16) + (p[6] << 8) + (p[7]);
					/* Update the address */
					sym_addr += offset + sym_vma + q->addend;
					/* Write relocated pointer back */
					p[2] = (sym_addr >> 24) & 0xff;
					p[3] = (sym_addr >> 16) & 0xff;
					p[6] = (sym_addr >>  8) & 0xff;
					p[7] =  sym_addr        & 0xff;

					/* create a new reloc entry */
					flat_relocs = realloc(flat_relocs,
						(flat_reloc_count + 1) * sizeof(unsigned long));
					flat_relocs[flat_reloc_count] = pflags | (section_vma + q->address);
					flat_reloc_count++;
					relocation_needed = 0;
					pflags = 0;
			sprintf(&addstr[0], "+0x%x", sym_addr - (*(q->sym_ptr_ptr))->value -
					 bfd_section_vma(abs_bfd, sym_section));
			if (verbose)
				printf("  RELOC[%d]: offset=%x symbol=%s%s "
					"section=%s size=%d "
					"fixup=%x (reloc=0x%x)\n", flat_reloc_count,
					q->address, sym_name, addstr,
					section_name, sym_reloc_size,
					sym_addr, section_vma + q->address);
			if (verbose)
				printf("reloc[%d] = 0x%x\n", flat_reloc_count,
					 section_vma + q->address);

					continue;
				}
				case R_MICROBLAZE_32:
					relocation_needed = 1;
					//sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;

				case R_MICROBLAZE_64_PCREL:
					sym_vma = 0;
					//sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 4);
					sym_addr = htonl(sym_addr);
					/* insert 16 MSB */
					* ((unsigned short *) (sectionp + q->address+2)) |= (sym_addr) & 0xFFFF;
					/* then 16 LSB */
					* ((unsigned short *) (sectionp + q->address+6)) |= (sym_addr >> 16) & 0xFFFF;
					/* We've done all the work, so continue
					   to next reloc instead of break */
					continue;

#endif /* TARGET_microblaze */
					
#ifdef TARGET_sparc
				case R_SPARC_32:
				case R_SPARC_UA32:
					relocation_needed = 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_SPARC_PC22:
					sym_vma = 0;
					sym_addr += sym_vma + q->addend;
					sym_addr -= q->address;
					break;
				case R_SPARC_WDISP30:
					sym_addr = (((*(q->sym_ptr_ptr))->value-
						q->address) >> 2) & 0x3fffffff;
					sym_addr |= (
						ntohl(*((unsigned long *) (sectionp + q->address))) &
						0xc0000000
						);
					break;
				case R_SPARC_HI22:
					relocation_needed = 1;
					pflags = 0x80000000;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr |= (
						htonl(* ((unsigned long *) (sectionp + q->address))) &
						0xffc00000
						);
					break;
				case R_SPARC_LO10:
					relocation_needed = 1;
					pflags = 0x40000000;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr &= 0x000003ff;
					sym_addr |= (
						htonl(* ((unsigned long *) (sectionp + q->address))) &
						0xfffffc00
						);
					break;
#endif /* TARGET_sparc */

#ifdef TARGET_sh
				case R_SH_DIR32:
					relocation_needed = 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_SH_REL32:
					sym_vma = 0;
					sym_addr += sym_vma + q->addend;
					sym_addr -= q->address;
					break;
#endif /* TARGET_sh */

				default:
					/* missing support for other types of relocs */
					printf("ERROR: bad reloc type %d\n", (*p)->howto->type);
					bad_relocs++;
					continue;
				}
			}

			sprintf(&addstr[0], "+0x%x", sym_addr - (*(q->sym_ptr_ptr))->value -
					 bfd_section_vma(abs_bfd, sym_section));


			/*
			 * for full elf relocation we have to write back the
			 * start_code relative value to use.
			 */
			if (!pic_with_got) {
#if defined(TARGET_arm)
				union {
					unsigned char c[4];
					unsigned long l;
				} tmp;
				long hl;
				int i0, i1, i2, i3;

				/*
				 * horrible nasty hack to support different endianess
				 */
				if (!bfd_big_endian(abs_bfd)) {
					i0 = 0;
					i1 = 1;
					i2 = 2;
					i3 = 3;
				} else {
					i0 = 3;
					i1 = 2;
					i2 = 1;
					i3 = 0;
				}

				tmp.l = *((unsigned long *) (sectionp + q->address));
				hl = tmp.c[i0] | (tmp.c[i1] << 8) | (tmp.c[i2] << 16);
				if (((*p)->howto->type != R_ARM_PC24) &&
				    ((*p)->howto->type != R_ARM_PLT32))
					hl |= (tmp.c[i3] << 24);
				else if (tmp.c[i2] & 0x80)
					hl |= 0xff000000; /* sign extend */
				hl += sym_addr;
				tmp.c[i0] = hl & 0xff;
				tmp.c[i1] = (hl >> 8) & 0xff;
				tmp.c[i2] = (hl >> 16) & 0xff;
				if (((*p)->howto->type != R_ARM_PC24) &&
				    ((*p)->howto->type != R_ARM_PLT32))
					tmp.c[i3] = (hl >> 24) & 0xff;
				if ((*p)->howto->type == R_ARM_ABS32)
					*((unsigned long *) (sectionp + q->address)) = htonl(hl);
				else
					*((unsigned long *) (sectionp + q->address)) = tmp.l;
#else /* ! TARGET_arm */
				/* The alignment of the build host might be
				   stricter than that of the target, so be
				   careful.  We store in network byte order. */
				unsigned char *p = sectionp + q->address;
				p[0] = (sym_addr >> 24) & 0xff;
				p[1] = (sym_addr >> 16) & 0xff;
				p[2] = (sym_addr >>  8) & 0xff;
				p[3] =  sym_addr        & 0xff;
#endif
			}

			if (verbose)
				printf("  RELOC[%d]: offset=%x symbol=%s%s "
					"section=%s size=%d "
					"fixup=%x (reloc=0x%x)\n", flat_reloc_count,
					q->address, sym_name, addstr,
					section_name, sym_reloc_size,
					sym_addr, section_vma + q->address);

			/*
			 *	Create relocation entry (PC relative doesn't need this).
			 */
			if (relocation_needed) {
				flat_relocs = realloc(flat_relocs,
					(flat_reloc_count + 1) * sizeof(unsigned long));
				flat_relocs[flat_reloc_count] = pflags |
					(section_vma + q->address);

				if (verbose)
					printf("reloc[%d] = 0x%x\n", flat_reloc_count,
							section_vma + q->address);
				flat_reloc_count++;
				relocation_needed = 0;
				pflags = 0;
			}

#if 0
printf("%s(%d): symbol name=%s address=%x section=%s -> RELOC=%x\n",
	__FILE__, __LINE__, sym_name, q->address, section_name,
	flat_relocs[flat_reloc_count]);
#endif
		}
	}
  }

  if (bad_relocs) {
	  printf("%d bad relocs\n", bad_relocs);
	  exit(1);
  }

  if (rc < 0)
	return(0);

  *n_relocs = flat_reloc_count;
  return flat_relocs;
}



#if 0
/* shared lib symbols stuff */

long
get_symbol(char *name, asection *sec, asymbol **symbol_table, long number_of_symbols)
{
  long i;
  for (i=0; i<number_of_symbols; i++) {
    if (symbol_table[i]->section == sec) {
      if (!strcmp(symbol_table[i]->name, name)) {
	return symbol_table[i]->value;
      }
    }
  }
  return -1;
}  

int
output_offset_table(int fd, char *ename, bfd *abfd, asymbol **symbol_table, long number_of_symbols)
{
  long i;
  FILE *ef;
  char buf[80];
  char libname[80];
  long etext_addr;
  long sym_addr;

  int foobar = 0;
  int count = 0;
  signed short *tab = malloc(32768); /* we don't know how many yet*/

  asection *text_section = bfd_get_section_by_name (abfd, ".text");

  if (!(ef = fopen(ename, "r"))) {
    fprintf (stderr,"Can't open %s\n",ename);
    exit(1);
  }

  fgets(libname, 80, ef);

  if (number_of_symbols < 0) {
    fprintf (stderr,"Corrupt symbol table!\n");
    exit(1);
  }

  if ((etext_addr = get_symbol("etext",
			       text_section,
			       symbol_table,
			       number_of_symbols)) == -1) {
    fprintf (stderr,"Can't find the symbol etext\n");
    exit(1);
  }

  fgets(buf, 80, ef);
  while (!feof(ef)) {
    buf[strlen(buf)-1] = 0; /* Arrrgh! linefeeds */

    if ((sym_addr = get_symbol(buf,
			       text_section,
			       symbol_table,
			       number_of_symbols)) == -1) {
      fprintf (stderr,"Can't find the symbol %s\n",buf);
      foobar++;
    } else {
      tab[++count] = htons(sym_addr - etext_addr);
    }
    fgets(buf, 80, ef);
  }

  fclose(ef);

  if (foobar) {
    fprintf (stderr,"*** %d symbols not found\n",foobar);
    exit(10);
  }

  strcpy((char *)&tab[++count],libname);
  tab[0] = htons(count * 2);
  write(fd, tab, count * 2 + strlen(libname) + 2);
  return 0;
}
#endif


static char * program;

static void usage(void)
{  
    fprintf(stderr, "Usage: %s [vrzd] [-p <abs-pic-file>] [-s stack-size] "
	"[-o <output-file>] <elf-file>\n\n"
	"       -v              : verbose operation\n"
	"       -r              : force load to RAM\n"
	"       -z              : compress code/data/relocs\n"
	"       -d              : compress data/relocs\n"
	"       -a              : use existing symbol references\n"
	"                         instead of recalculating from\n"
	"                         relocation info\n"
        "       -R reloc-file   : read relocations from a separate file\n"
	"       -p abs-pic-file : GOT/PIC processing with files\n"
	"       -s stacksize    : set application stack size\n"
	"       -o output-file  : output file name\n\n",
	program);
	fprintf(stderr, "Compiled for " ARCH " architecture\n\n");
    exit(2);
}



int main(int argc, char *argv[])
{
  int fd;
  bfd *rel_bfd, *abs_bfd;
  asection *s;
  char *ofile=NULL, *pfile=NULL, *abs_file = NULL, *rel_file = NULL;
  char *fname = NULL;
  int opt;
  int i;
  int stack;
  char  cmd[1024];
  FILE *gf = NULL;


  asymbol **symbol_table;
  long number_of_symbols;

  unsigned long data_len;
  unsigned long bss_len;
  unsigned long text_len;
  unsigned long reloc_len;

  unsigned long data_vma;
  unsigned long bss_vma;
  unsigned long text_vma;

  void *text;
  void *data;
  unsigned long *reloc;
  
  struct flat_hdr hdr;


  program = argv[0];
  progname = argv[0];

  if (argc < 2)
  	usage();
  
  stack = 4096;

  while ((opt = getopt(argc, argv, "avzdrp:s:o:R:")) != -1) {
    switch (opt) {
    case 'v':
      verbose++;
      break;
    case 'r':
      load_to_ram++;
      break;
    case 'z':
      compress = 1;
      break;
    case 'd':
      compress = 2;
      break;
    case 'p':
      pfile = optarg;
      break;
    case 'o':
      ofile = optarg;
      break;
    case 'a':
      use_resolved = 1;
      break;
    case 's':
      stack = atoi(optarg);
      break;
    case 'R':
      rel_file = optarg;
      break;
    default:
      fprintf(stderr, "%s Unknown option\n", argv[0]);
      usage();
      break;
    }
  }
  
  /*
   * if neither the -r or -p options was given,  default to
   * a RAM load as that is the only option that makes sense.
   */
  if (!load_to_ram && !pfile)
    load_to_ram = 1;

  filename = fname = argv[argc-1];

  if (pfile) {
    pic_with_got = 1;
    abs_file = pfile;
  } else
    abs_file = fname;

  if (! rel_file)
    rel_file = fname;

  if (!(rel_bfd = bfd_openr(rel_file, 0))) {
    fprintf(stderr, "Can't open %s\n", rel_file);
    exit(1);
  }

  if (bfd_check_format (rel_bfd, bfd_object) == 0) {
    fprintf(stderr, "File is not an object file\n");
    exit(2);
  }

  if (abs_file == rel_file)
    abs_bfd = rel_bfd; /* one file does all */
  else {
    if (!(abs_bfd = bfd_openr(abs_file, 0))) {
      fprintf(stderr, "Can't open %s\n", abs_file);
      exit(1);
    }

    if (bfd_check_format (abs_bfd, bfd_object) == 0) {
      fprintf(stderr, "File is not an object file\n");
      exit(2);
    }
  }

  if (! (bfd_get_file_flags(rel_bfd) & HAS_RELOC)) {
    fprintf (stderr, "%s: Input file contains no relocation info\n", rel_file);
    exit (2);
  }

  if (use_resolved && !(bfd_get_file_flags(abs_bfd) & EXEC_P)) {
    /* `Absolute' file is not absolute, so neither are address
       contained therein.  */
    fprintf (stderr,
	     "%s: `-a' option specified with non-fully-resolved input file\n",
	     bfd_get_filename (abs_bfd));
    exit (2);
  }

  symbol_table = get_symbols(abs_bfd, &number_of_symbols);

  s = bfd_get_section_by_name (abs_bfd, ".text");
  text_vma = s->vma;
  text_len = s->_raw_size;
  text = malloc(text_len);

  if (verbose) {
    printf("TEXT -> vma=%x len=%x\n", text_vma, text_len);
    printf("        lma=%x clen=%x oo=%x ap=%x fp=%x\n",
			s->lma, s->_cooked_size, s->output_offset,
			s->alignment_power, s->filepos);
  }

  if (bfd_get_section_contents(abs_bfd,
			       s, 
			       text,
			       0,
			       s->_raw_size) == false) {
    fprintf(stderr, "read error section %s\n", s->name);
    exit(2);
  }

  s = bfd_get_section_by_name (abs_bfd, ".data");
  data_vma = s->vma;
  data_len = s->_raw_size;
  data = malloc(data_len);

  if (verbose) {
     printf("DATA -> vma=%x len=%x\n", data_vma, data_len);
     printf("        lma=%x clen=%x oo=%x ap=%x fp=%x\n",
	 		s->lma, s->_cooked_size, s->output_offset,
			s->alignment_power, s->filepos);
  }
  if ((text_vma + text_len) != data_vma) {
    if ((text_vma + text_len) > data_vma) {
      printf("ERROR: text=%x overlaps data=%x ?\n", text_len, data_vma);
      exit(1);
    }
    if (verbose)
      printf("WARNING: data=%x does not directly follow text=%x\n",
	  		data_vma, text_len);
    text_len = data_vma - text_vma;
  }

  if (bfd_get_section_contents(abs_bfd,
			       s, 
			       data,
			       0,
			       s->_raw_size) == false) {
    fprintf(stderr, "read error section %s\n", s->name);
    exit(2);
  }

  s = bfd_get_section_by_name (abs_bfd, ".bss");
  bss_len = s->_raw_size;
  bss_vma = s->vma;
  bss_len += add_com_to_bss(symbol_table, number_of_symbols, bss_len);

  if (verbose) {
	printf("BSS  -> vma=%x len=%x\n", bss_vma, bss_len);
    printf("        lma=%x clen=%x oo=%x ap=%x fp=%x\n",
			s->lma, s->_cooked_size, s->output_offset,
			s->alignment_power, s->filepos);
  }

  if ((text_vma + text_len + data_len) != bss_vma) {
    if ((text_vma + text_len + data_len) > bss_vma) {
      printf("ERROR: text=%x + data=%x overlaps bss=%x ?\n", text_len,
	  		data_len, bss_vma);
      exit(1);
    }
    if (verbose)
      printf("WARNING: bss=%x does not directly follow text=%x + data=%x(%x)\n",
      		bss_vma, text_len, data_len, text_len + data_len);
      data_len = bss_vma - data_vma;
  }

  reloc = (unsigned long *) output_relocs (abs_bfd, symbol_table,
      number_of_symbols, &reloc_len, text, text_len, data, data_len, rel_bfd);

  if (reloc == NULL)
    printf("No relocations in code!\n");

  /* Fill in the binflt_flat header */
  memcpy(hdr.magic,"bFLT",4);
  hdr.rev         = htonl(FLAT_VERSION);
  hdr.entry       = htonl(16 * 4 + bfd_get_start_address(abs_bfd));
  hdr.data_start  = htonl(16 * 4 + text_len);
  hdr.data_end    = htonl(16 * 4 + text_len + data_len);
  hdr.bss_end     = htonl(16 * 4 + text_len + data_len + bss_len);
  hdr.stack_size  = htonl(stack); /* FIXME */
  hdr.reloc_start = htonl(16 * 4 + text_len + data_len);
  hdr.reloc_count = htonl(reloc_len);
  hdr.flags       = htonl(0
	  | (load_to_ram ? FLAT_FLAG_RAM : 0)
	  | (pic_with_got ? FLAT_FLAG_GOTPIC : 0)
	  | (compress ? (compress == 2 ? FLAT_FLAG_GZDATA : FLAT_FLAG_GZIP) : 0)
	  );
  hdr.build_date = htonl((unsigned long)time(NULL));
  bzero(hdr.filler, sizeof(hdr.filler));

  for (i=0; i<reloc_len; i++) reloc[i] = htonl(reloc[i]);

  if (verbose) {
    printf("SIZE: .text=0x%04x, .data=0x%04x, .bss=0x%04x",
	text_len, data_len, bss_len);
    if (reloc)
      printf(", relocs=0x%04x", reloc_len);
    printf("\n");
  }
  
  if (!ofile) {
    ofile = malloc(strlen(fname) + 5 + 1); /* 5 to add suffix */
    strcpy(ofile, fname);
    strcat(ofile, ".bflt");
  }

  if ((fd = open (ofile, O_WRONLY|O_BINARY|O_CREAT|O_TRUNC, 0744)) < 0) {
    fprintf (stderr, "Can't open output file %s\n", ofile);
    exit(4);
  }

  write(fd, &hdr, sizeof(hdr));
  close(fd);

  /*
   * get the compression command ready
   */
  sprintf(cmd, "gzip -f -9 >> %s", ofile);

#define	START_COMPRESSOR do { \
		if (gf) fclose(gf); \
		if (!(gf = popen(cmd, "w"))) { \
			fprintf(stderr, "Can't run cmd %s\n", cmd); \
			exit(4); \
		} \
	} while (0)

  gf = fopen(ofile, "a");
  if (!gf) {
  	fprintf(stderr, "Can't opne file %s for writing\n", ofile); \
	exit(4);
  }

  if (compress == 1)
  	START_COMPRESSOR;

  fwrite(text, text_len, 1, gf);

  if (compress == 2)
  	START_COMPRESSOR;

  fwrite(data, data_len, 1, gf);
  if (reloc)
    fwrite(reloc, reloc_len*4, 1, gf);
  fclose(gf);

  exit(0);
}

