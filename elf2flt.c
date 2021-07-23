/*
 * elf2flt.c: Convert ELF (or any BFD format) to FLAT binary format
 *
 * (c) 1999-2002, Greg Ungerer <gerg@snapgear.com>
 * Created elf2flt from coff2flt (see copyrights below). Added all the
 * ELF format file handling. Extended relocation support for all of
 * text and data.
 *
 * (c) 2008-2009, Xtensa support, Oskar Schirmer <os@emlix.com>
 * (c) 2006  Support the -a (use_resolved) option for TARGET_arm.
 *           Shaun Jackman <sjackman@gmail.com>
 * (c) 2004, Nios II support, Wentao Xu <wentao@microtronix.com>
 * (c) 2003, H8 support, ktrace <davidm@snapgear.com>
 * (c) 2003-2004, MicroBlaze support, John Williams <jwilliams@itee.uq.edu.au>
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
 
#include <inttypes.h> /* All the standard PRI define times for printf        */
#include <stdio.h>    /* Userland pieces of the ANSI C standard I/O package  */
#include <stdlib.h>   /* Userland prototypes of the ANSI C std lib functions */
#include <stdarg.h>   /* Allows va_list to exist in the these namespaces     */
#include <string.h>   /* Userland prototypes of the string handling funcs    */
#include <strings.h>
#include <unistd.h>   /* Userland prototypes of the Unix std system calls    */
#include <fcntl.h>    /* Flag value for file handling functions              */
#include <time.h>
#include <errno.h>

/* from $(INSTALLDIR)/include       */
#include <bfd.h>      /* Main header file for the BFD library                */
#include <libiberty.h>

#include "stubs.h"
const char *elf2flt_progname;

/* Include the right header file for the R_xxx defines.  */
#if defined(TARGET_arm)
#include <elf/arm.h>
#elif defined(TARGET_bfin)
#include <elf/bfin.h>
#elif defined(TARGET_h8300)
#include <elf/h8.h>
#elif defined(TARGET_m68k)
#include <elf/m68k.h>
#elif defined(TARGET_microblaze)
#include <elf/microblaze.h>
#elif defined(TARGET_nios) || defined(TARGET_nios2)
/* Altera NIOS specific definitions.  */
#define FLAT_NIOS2_R_32			0 /* Normal 32-bit reloc */
#define FLAT_NIOS2_R_HI_LO		1
#define FLAT_NIOS2_R_HIADJ_LO	2
#define FLAT_NIOS2_R_CALL26		4
#include <elf/nios2.h>
#elif defined(TARGET_sh)
#include <elf/sh.h>
#elif defined(TARGET_sparc)
#include <elf/sparc.h>
#elif defined(TARGET_v850)
#include <elf/v850.h>
#elif defined(TARGET_xtensa)
#include <elf/xtensa.h>
#elif defined(TARGET_riscv64)
#include <elf/riscv.h>
#endif

#if defined(__MINGW32__)
#include <getopt.h>
#endif

/* from uClinux-x.x.x/include/linux */
#include "flat.h"     /* Binary flat header description                      */
#include "compress.h"

#ifdef TARGET_e1
#include <e1.h>
#endif

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
#elif defined(TARGET_e1)
#define ARCH    "e1-coff"
#elif defined(TARGET_bfin)
#define ARCH	"bfin"
#elif defined(TARGET_nios)
#define ARCH	"nios"
#elif defined(TARGET_nios2)
#define ARCH	"nios2"
#elif defined(TARGET_xtensa)
#define ARCH	"xtensa"
#elif defined(TARGET_riscv64)
#define ARCH	"riscv64"
#else
#error "Don't know how to support your CPU architecture??"
#endif

#if defined(TARGET_m68k) || defined(TARGET_h8300) || defined(TARGET_bfin)
/*
 * Define a maximum number of bytes allowed in the offset table.
 * We'll fail if the table is larger than this.
 *
 * This limit may be different for platforms other than m68k, but
 * 8000 entries is a lot,  trust me :-) (davidm)
 */
#define GOT_LIMIT 32767
/*
 * we have to mask out the shared library id here and there,  this gives
 * us the real address bits when needed
 */
#define	real_address_bits(x)	(pic_with_got ? ((x) & 0xffffff) : (x))
#else
#define	real_address_bits(x)	(x)
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif


/* Extra output when running.  */
static int verbose = 0;
/* Do ELF/GOT processing with PIC code.  */
static int pic_with_got = 0;
/* Instruct loader to allocate everything into RAM.  */
static int load_to_ram = 0;
/* Instruct kernel loader to output debug/trace info when loading.  */
static int ktrace = 0;
/* 1 = compress everything, 2 = compress data only.  */
static int docompress = 0;
/* If true, get the value of symbol references from the program contents,
   not from the relocation table.  In this case, the input ELF file must
   be already fully resolved (using the `-q' flag with recent versions of
   GNU ld will give you a fully resolved output file with relocation
   entries).  */
static int use_resolved = 0;

/* Set if the text section contains any relocations.  If it does, we must
   set the load_to_ram flag.  */
static int text_has_relocs = 0;

static asymbol **
get_symbols (bfd *abfd, long *num)
{
  int32_t storage_needed;
  asymbol **symbol_table;
  long number_of_symbols;
  
  storage_needed = bfd_get_symtab_upper_bound (abfd);
	  
  if (storage_needed < 0)
    abort ();
      
  if (storage_needed == 0)
    return NULL;

  symbol_table = xmalloc (storage_needed);

  number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);
  
  if (number_of_symbols < 0) 
    abort ();

  *num = number_of_symbols;
  return symbol_table;
}



static int
dump_symbols(asymbol **symbol_table, long number_of_symbols)
{
  long i;
  printf("SYMBOL TABLE:\n");
  for (i=0; i<number_of_symbols; i++) {
	printf("  NAME=%s  VALUE=0x%"BFD_VMA_FMT"x\n",
		symbol_table[i]->name, symbol_table[i]->value);
  }
  printf("\n");
  return(0);
}  



#if !defined TARGET_e1 && !defined TARGET_bfin
static long
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
#endif



#ifdef TARGET_nios2
static long
get_gp_value(asymbol **symbol_table, long number_of_symbols)
{
  long i;
  for (i=0; i<number_of_symbols; i++) {
      if (!strcmp(symbol_table[i]->name, "_gp"))
		return symbol_table[i]->value;
  }
  return -1;
}
#endif



static int32_t
add_com_to_bss(asymbol **symbol_table, int32_t number_of_symbols, int32_t bss_len)
{
  int32_t i, comsize;
  int32_t offset;

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

#ifdef TARGET_bfin
/* FUNCTION : weak_und_symbol
   ABSTRACT : return true if symbol is weak and undefined.
*/
static int
weak_und_symbol(const char *reloc_section_name,
                struct bfd_symbol *symbol)
{
    if (!(strstr (reloc_section_name, "text")
	  || strstr (reloc_section_name, "data")
	  || strstr (reloc_section_name, "bss"))) {
	if (symbol->flags & BSF_WEAK) {
#ifdef DEBUG_BFIN
	    fprintf(stderr, "found weak undefined symbol %s\n", symbol->name);
#endif
	    return TRUE;
	}
    }
    return FALSE;
}

static int
bfin_set_reloc (uint32_t *reloc,
		const char *reloc_section_name,
		const char *sym_name,
		struct bfd_symbol *symbol,
		int sp, int32_t offset)
{
    unsigned int type = 0;
    uint32_t val;

    if (strstr (reloc_section_name, "stack")) {
	    if (verbose)
		    printf("Stack-relative reloc, offset %08"PRIx32"\n", offset);
	    /* This must be a stack_start reloc for stack checking.  */
	    type = 1;
    }
    val = (offset & ((1 << 26) - 1));
    val |= (sp & ((1 << 3) - 1)) << 26;
    val |= type << 29;
    *reloc = val;
    return 0;
}

static bfd *compare_relocs_bfd;

static int
compare_relocs (const void *pa, const void *pb)
{
	const arelent *const *a = pa, *const *b = pb;
	const arelent *ra = *a, *rb = *b;
	unsigned long va, vb;
	uint32_t a_vma, b_vma;

	if (!ra->sym_ptr_ptr || !*ra->sym_ptr_ptr)
		return -1;
	else if (!rb->sym_ptr_ptr || !*rb->sym_ptr_ptr)
		return 1;

	a_vma = bfd_section_vma(compare_relocs_bfd,
				(*(ra->sym_ptr_ptr))->section);
	b_vma = bfd_section_vma(compare_relocs_bfd,
				(*(rb->sym_ptr_ptr))->section);
	va = (*(ra->sym_ptr_ptr))->value + a_vma + ra->addend;
	vb = (*(rb->sym_ptr_ptr))->value + b_vma + rb->addend;
	return va - vb;
}
#endif

static uint32_t *
output_relocs (
  bfd *abs_bfd,
  asymbol **symbols,
  int number_of_symbols,
  uint32_t *n_relocs,
  unsigned char *text, int text_len, uint32_t text_vma,
  unsigned char *data, int data_len, uint32_t data_vma,
  bfd *rel_bfd)
{
  uint32_t		*flat_relocs;
  asection		*a, *sym_section, *r;
  arelent		**relpp, **p, *q;
  const char		*sym_name, *section_name;
  unsigned char		*sectionp;
  unsigned long		pflags;
  char			addstr[20];
  uint32_t		sym_addr, sym_vma, section_vma;
  int			relsize, relcount;
  int			flat_reloc_count;
  int			sym_reloc_size, rc;
  int			got_size = 0;
  int			bad_relocs = 0;
  asymbol		**symb;
  long			nsymb;
#ifdef TARGET_bfin
  unsigned long		persistent_data = 0;
#endif
  
#if 0
  printf("%s(%d): output_relocs(abs_bfd=%d,synbols=0x%x,number_of_symbols=%d"
	"n_relocs=0x%x,text=0x%x,text_len=%d,data=0x%x,data_len=%d)\n",
	__FILE__, __LINE__, abs_bfd, symbols, number_of_symbols, n_relocs,
	text, text_len, data, data_len);
#endif

  if (0)
    dump_symbols(symbols, number_of_symbols);

  *n_relocs = 0;
  flat_relocs = NULL;
  flat_reloc_count = 0;
  sym_reloc_size = 0;
  rc = 0;
  pflags = 0;
  /* Silence gcc warnings */
  (void) pflags;
  (void) sym_vma;

  /* Determine how big our offset table is in bytes.
   * This isn't too difficult as we've terminated the table with -1.
   * Also note that both the relocatable and absolute versions have this
   * terminator even though the relocatable one doesn't have the GOT!
   */
  if (pic_with_got && !use_resolved) {
    uint32_t *lp = (uint32_t *)data;
    /* Should call ntohl(*lp) here but is isn't going to matter */
    while (*lp != 0xffffffff) lp++;
    got_size = ((unsigned char *)lp) - data;
    if (verbose)
	    printf("GOT table contains %zu entries (%d bytes)\n",
			    got_size/sizeof(uint32_t), got_size);
#ifdef TARGET_m68k
    if (got_size > GOT_LIMIT)
	    fatal("GOT too large: %d bytes (limit = %d bytes)",
			got_size, GOT_LIMIT);
#endif
  }

  for (a = abs_bfd->sections; (a != (asection *) NULL); a = a->next) {
  	section_vma = bfd_section_vma(abs_bfd, a);

	if (verbose)
		printf("SECTION: %s [%p]: flags=0x%x vma=0x%"PRIx32"\n",
			a->name, a, a->flags, section_vma);

//	if (bfd_is_abs_section(a))
//		continue;
	if (bfd_is_und_section(a))
		continue;
	if (bfd_is_com_section(a))
		continue;
//	if ((a->flags & SEC_RELOC) == 0)
//		continue;

	/*
	 * Only relocate things in the writable data sections if we are PIC/GOT.
	 * Otherwise do text (and read only data) as well.
	 */
	if ((!pic_with_got || ALWAYS_RELOC_TEXT) &&
	    ((a->flags & SEC_CODE) ||
	    ((a->flags & (SEC_DATA | SEC_READONLY)) == (SEC_DATA | SEC_READONLY))))
		sectionp = text + (a->vma - text_vma);
	else if (a->flags & SEC_DATA)
		sectionp = data + (a->vma - data_vma);
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
	  printf(" RELOCS: %s [%p]: flags=0x%x vma=0x%"BFD_VMA_FMT"x\n",
			r->name, r, r->flags, bfd_section_vma(abs_bfd, r));
  	if ((r->flags & SEC_RELOC) == 0)
  	  continue;
	relsize = bfd_get_reloc_upper_bound(rel_bfd, r);
	if (relsize <= 0) {
		if (verbose)
			printf("%s(%d): no relocation entries section=%s\n",
				__FILE__, __LINE__, r->name);
		continue;
	}

	symb = get_symbols(rel_bfd, &nsymb);
	relpp = xmalloc(relsize);

	relcount = bfd_canonicalize_reloc(rel_bfd, r, relpp, symb);
	if (relcount <= 0) {
		if (verbose)
			printf("%s(%d): no relocation entries section=%s\n",
			__FILE__, __LINE__, r->name);
		continue;
	} else {
#ifdef TARGET_bfin
		compare_relocs_bfd = abs_bfd;
		qsort (relpp, relcount, sizeof *relpp, compare_relocs);
#endif
		for (p = relpp; (relcount && (*p != NULL)); p++, relcount--) {
			unsigned char *r_mem = NULL;
			int relocation_needed = 0;
			/* Do we need to update the text segment? By default yes if not pic_with_got */
			int update_text = !pic_with_got;

#ifdef TARGET_v850
			/* Skip this relocation entirely if possible (we
			   do this early, before doing any other
			   processing on it).  */
			switch ((*p)->howto->type) {
			case R_V850_9_PCREL:
			case R_V850_22_PCREL:
			case R_V850_SDA_16_16_OFFSET:
			case R_V850_SDA_15_16_OFFSET:
			case R_V850_ZDA_15_16_OFFSET:
			case R_V850_TDA_6_8_OFFSET:
			case R_V850_TDA_7_8_OFFSET:
			case R_V850_TDA_7_7_OFFSET:
			case R_V850_TDA_16_16_OFFSET:
			case R_V850_TDA_4_5_OFFSET:
			case R_V850_TDA_4_4_OFFSET:
			case R_V850_SDA_16_16_SPLIT_OFFSET:
			case R_V850_CALLT_6_7_OFFSET:
			case R_V850_CALLT_16_16_OFFSET:
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
#ifndef TARGET_bfin
			/* Adjust the address to account for the GOT table which wasn't
			 * present in the relative file link.
			 */
			if (pic_with_got && !use_resolved)
			  q->address += got_size;
#endif

			/* A pointer to what's being relocated, used often
			   below.  */
			r_mem = sectionp + q->address;

			/*
			 *	Fixup offset in the actual section.
			 */
			addstr[0] = 0;
#if !defined TARGET_e1 && !defined TARGET_bfin
  			if ((sym_addr = get_symbol_offset((char *) sym_name,
			    sym_section, symbols, number_of_symbols)) == -1) {
				sym_addr = 0;
			}
#else
			sym_addr = (*(q->sym_ptr_ptr))->value;
#endif			
			if (use_resolved) {
				/* Use the address of the symbol already in
				   the program text.  How this is handled may
				   still depend on the particular relocation
				   though.  */
				switch (q->howto->type) {
#ifdef TARGET_v850
					int r2_type;
				case R_V850_HI16_S:
					/* We specially handle adjacent
					   HI16_S/ZDA_15_16_OFFSET and
					   HI16_S/LO16 pairs that reference the
					   same address (these are usually
					   movhi/ld and movhi/movea pairs,
					   respectively).  */
					if (relcount == 0)
						r2_type = R_V850_NONE;
					else
						r2_type = p[1]->howto->type;
					if ((r2_type == R_V850_ZDA_15_16_OFFSET
					     || r2_type == R_V850_LO16)
					    && (p[0]->sym_ptr_ptr
						== p[1]->sym_ptr_ptr)
					    && (p[0]->addend == p[1]->addend))
					{
						relocation_needed = 1;

						switch (r2_type) {
						case R_V850_ZDA_15_16_OFFSET:
							pflags = 0x10000000;
							break;
						case R_V850_LO16:
							pflags = 0x20000000;
							break;
						}

						/* We don't really need the
						   actual value -- the bits
						   produced by the linker are
						   what we want in the final
						   flat file -- but get it
						   anyway if useful for
						   debugging.  */
						if (verbose) {
							unsigned char *r2_mem =
								sectionp
								+ p[1]->address;
							/* little-endian */
							int hi = r_mem[0]
								+ (r_mem[1] << 8);
							int lo = r2_mem[0]
								+ (r2_mem[1] << 8);
							/* Sign extend LO.  */
							lo = (lo ^ 0x8000)
								- 0x8000;

							/* Maybe ignore the LSB
							   of LO, which is
							   actually part of the
							   instruction.  */
							if (r2_type != R_V850_LO16)
								lo &= ~1;

							sym_addr =
								(hi << 16)
								+ lo;
						}
					} else
						goto bad_resolved_reloc;
					break;

				case R_V850_LO16:
					/* See if this is actually the
					   2nd half of a pair.  */
					if (p > relpp
					    && (p[-1]->howto->type
						== R_V850_HI16_S)
					    && (p[-1]->sym_ptr_ptr
						== p[0]->sym_ptr_ptr)
					    && (p[-1]->addend == p[0]->addend))
						break; /* not an error */
					else
						goto bad_resolved_reloc;

				case R_V850_HI16:
					goto bad_resolved_reloc;
				default:
					goto good_32bit_resolved_reloc;
#elif defined(TARGET_microblaze)
				case R_MICROBLAZE_64:
					sym_addr =
						(r_mem[2] << 24)
						+ (r_mem[3] << 16)
						+ (r_mem[6] << 8)
						+ r_mem[7];
					relocation_needed = 1;
					update_text = 0;
					pflags = 0x80000000;
					break;
				case R_MICROBLAZE_GOTPC_64:
					/* This is data-relative. We can't support this with bflt */
					goto bad_resolved_reloc;
				case R_MICROBLAZE_GOT_64:
					/* This is a GOT relocation. But it is accessed via the GOT register (r20)
					 * so doesn't need relocation
					 */
					relocation_needed = 0;
					break;
				case R_MICROBLAZE_32:
					goto good_32bit_resolved_reloc;
				/* These are already relocated for us as text-relative, or are dummy entries */
				case R_MICROBLAZE_64_PCREL:
				case R_MICROBLAZE_PLT_64:
				case R_MICROBLAZE_NONE:
				case R_MICROBLAZE_64_NONE:
				case R_MICROBLAZE_32_PCREL_LO:
					relocation_needed = 0;
					continue;
				default:
					goto bad_resolved_reloc;
#elif defined(TARGET_arm)
				case R_ARM_TARGET1:
				case R_ARM_TARGET2:
				case R_ARM_ABS32:
					relocation_needed = 1;
					break;
				case R_ARM_REL32:
				case R_ARM_JUMP24:
				case R_ARM_CALL:
				case R_ARM_THM_PC11:
				case R_ARM_THM_PC22:
				case R_ARM_THM_JUMP24:
				case R_ARM_PC24:
				case R_ARM_PLT32:
				case R_ARM_GOTPC:
				case R_ARM_GOT32:
				case R_ARM_PREL31:
				case R_ARM_V4BX:
				case R_ARM_NONE:
					relocation_needed = 0;
					break;
				default:
					goto bad_resolved_reloc;
#elif defined(TARGET_m68k)
				case R_68K_32:
					goto good_32bit_resolved_reloc;
				case R_68K_PC32:
				case R_68K_PC16:
					/* The linker has already resolved
					   PC relocs for us.  In PIC links,
					   the symbol must be in the data
					   segment.  */
				case R_68K_NONE:
					continue;
				default:
					goto bad_resolved_reloc;
#elif defined TARGET_bfin
				case R_BFIN_RIMM16:
				case R_BFIN_LUIMM16:
				case R_BFIN_HUIMM16:
				    sym_vma = bfd_section_vma(abs_bfd, sym_section);
				    sym_addr += sym_vma + q->addend;

				    if (weak_und_symbol(sym_section->name, (*(q->sym_ptr_ptr))))
					continue;
				    if (q->howto->type == R_BFIN_RIMM16 && (0xFFFF0000 & sym_addr)) {
					fprintf (stderr, "Relocation overflow for rN = %s\n",sym_name);
					bad_relocs++;
				    }
				    if ((0xFFFF0000 & sym_addr) != persistent_data) {
				    flat_relocs = (uint32_t *)
					(realloc (flat_relocs, (flat_reloc_count + 1) * sizeof (uint32_t)));
					    if (verbose)
						    printf("New persistent data for %08"PRIx32"\n", sym_addr);
					    persistent_data = 0xFFFF0000 & sym_addr;
					    flat_relocs[flat_reloc_count++]
						    = (sym_addr >> 16) | (3 << 26);
				    }

				    flat_relocs = (uint32_t *)
					(realloc (flat_relocs, (flat_reloc_count + 1) * sizeof (uint32_t)));
				    if (bfin_set_reloc (flat_relocs + flat_reloc_count,
							sym_section->name, sym_name,
							(*(q->sym_ptr_ptr)),
							q->howto->type == R_BFIN_HUIMM16 ? 1 : 0,
							section_vma + q->address))
					bad_relocs++;
				    if (a->flags & SEC_CODE)
					text_has_relocs = 1;
				    flat_reloc_count++;
				    break;

				case R_BFIN_BYTE4_DATA:
				    sym_vma = bfd_section_vma(abs_bfd, sym_section);
				    sym_addr += sym_vma + q->addend;

				    if (weak_und_symbol (sym_section->name, (*(q->sym_ptr_ptr))))
					continue;

				    flat_relocs = (uint32_t *)
					(realloc (flat_relocs, (flat_reloc_count + 1) * sizeof (uint32_t)));
				    if (bfin_set_reloc (flat_relocs + flat_reloc_count,
							sym_section->name, sym_name,
							(*(q->sym_ptr_ptr)),
							2, section_vma + q->address))
					bad_relocs++;
				    if (a->flags & SEC_CODE)
					text_has_relocs = 1;

				    flat_reloc_count++;
				    break;
#elif defined (TARGET_h8300)
				case R_H8_DIR32:
				case R_H8_DIR32A16:
				case R_H8_DISP32A16:
				    r_mem[0] = 0;
				case R_H8_DIR24A8:
				case R_H8_DIR24R8:
				    goto good_32bit_resolved_reloc;
				case R_H8_PCREL8:
				case R_H8_PCREL16:
				    continue;
#elif defined(TARGET_xtensa)
				case R_XTENSA_NONE:
				case R_XTENSA_OP0:
				case R_XTENSA_OP1:
				case R_XTENSA_OP2:
				case R_XTENSA_SLOT0_OP:
				case R_XTENSA_SLOT1_OP:
				case R_XTENSA_SLOT2_OP:
				case R_XTENSA_SLOT3_OP:
				case R_XTENSA_SLOT4_OP:
				case R_XTENSA_SLOT5_OP:
				case R_XTENSA_SLOT6_OP:
				case R_XTENSA_SLOT7_OP:
				case R_XTENSA_SLOT8_OP:
				case R_XTENSA_SLOT9_OP:
				case R_XTENSA_SLOT10_OP:
				case R_XTENSA_SLOT11_OP:
				case R_XTENSA_SLOT12_OP:
				case R_XTENSA_SLOT13_OP:
				case R_XTENSA_SLOT14_OP:
				case R_XTENSA_SLOT0_ALT:
				case R_XTENSA_SLOT1_ALT:
				case R_XTENSA_SLOT2_ALT:
				case R_XTENSA_SLOT3_ALT:
				case R_XTENSA_SLOT4_ALT:
				case R_XTENSA_SLOT5_ALT:
				case R_XTENSA_SLOT6_ALT:
				case R_XTENSA_SLOT7_ALT:
				case R_XTENSA_SLOT8_ALT:
				case R_XTENSA_SLOT9_ALT:
				case R_XTENSA_SLOT10_ALT:
				case R_XTENSA_SLOT11_ALT:
				case R_XTENSA_SLOT12_ALT:
				case R_XTENSA_SLOT13_ALT:
				case R_XTENSA_SLOT14_ALT:
				case R_XTENSA_ASM_EXPAND:
				case R_XTENSA_ASM_SIMPLIFY:
				case R_XTENSA_DIFF8:
				case R_XTENSA_DIFF16:
				case R_XTENSA_DIFF32:
#if HAVE_BFD_XTENSA_PDIFF_RELOCS
				case R_XTENSA_PDIFF8:
				case R_XTENSA_PDIFF16:
				case R_XTENSA_PDIFF32:
				case R_XTENSA_NDIFF8:
				case R_XTENSA_NDIFF16:
				case R_XTENSA_NDIFF32:
#endif
				case R_XTENSA_32_PCREL:
					continue;
				case R_XTENSA_32:
				case R_XTENSA_PLT:
					goto good_32bit_resolved_reloc;
				default:
					goto bad_resolved_reloc;
#elif defined(TARGET_riscv64)
				case R_RISCV_32_PCREL:
				case R_RISCV_ADD32:
				case R_RISCV_SUB32:
					break;
				case R_RISCV_64:
					goto good_32bit_resolved_reloc;
				default:
					goto bad_resolved_reloc;
#else
				default:
					/* The default is to assume that the
					   relocation is relative and has
					   already been fixed up by the
					   linker (perhaps we ought to make
					   give an error by default, and
					   require `safe' relocations to be
					   enumberated explicitly?).  */
					goto good_32bit_resolved_reloc;
#endif
				good_32bit_resolved_reloc:
					if (bfd_big_endian (abs_bfd))
						sym_addr =
							(r_mem[0] << 24)
							+ (r_mem[1] << 16)
							+ (r_mem[2] << 8) 
							+ r_mem[3];
					else
						sym_addr =
							r_mem[0]
							+ (r_mem[1] << 8)
							+ (r_mem[2] << 16)
							+ (r_mem[3] << 24);
					relocation_needed = 1;
					update_text = 0;
					break;

				bad_resolved_reloc:
					printf("ERROR: reloc type %s unsupported in this context\n",
					       q->howto->name);
					bad_relocs++;
					break;
				}
			} else {
				/* Calculate the sym address ourselves.  */
#if defined(TARGET_xtensa)
				/* For xtensa, calculation of addresses won't
				   work this way. binutils "ld -r" generate
				   different relocation types, among others
				   type 20, R_XTENSA_SLOT0_OP. The latter is
				   produced for various opcodes that differ
				   in size and format, some will have the
				   addend filled in when linking, others won't.
				   For elf2flt to handle these relocations
				   would involve analysing the opcodes in
				   detail. Therefore, elf2flt for xtensa is
				   patched to work with "-a" option solely,
				   which will take output of "ld -q".
				*/
				fatal("ERROR: cannot run without '-a'");
#endif
				sym_reloc_size = bfd_get_reloc_size(q->howto);

				if (sym_reloc_size == 0) {
					/* These are dummy relocs that can be ignored */
					continue;
				}

#if !defined(TARGET_h8300) && !defined(TARGET_e1) && !defined(TARGET_bfin) && !defined(TARGET_m68k)
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
				case R_68K_PC16:
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
							"%s vma=0x%x, "
							"value=0x%"BFD_VMA_FMT"x, "
							"address=0x%"BFD_VMA_FMT"x "
							"sym_addr=0x%x rs=0x%x, opcode=0x%x\n",
							"ABS32",
							sym_vma, (*(q->sym_ptr_ptr))->value,
							q->address, sym_addr,
							(*p)->howto->rightshift,
							*(uint32_t *)r_mem);
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
							"%s vma=0x%x, "
							"value=0x%"BFD_VMA_FMT"x, "
							"address=0x%"BFD_VMA_FMT"x "
							"sym_addr=0x%x rs=0x%x, opcode=0x%x\n",
							"PLT32",
							sym_vma, (*(q->sym_ptr_ptr))->value,
							q->address, sym_addr,
							(*p)->howto->rightshift,
							*(uint32_t *)r_mem);
				case R_ARM_PC24:
					sym_vma = 0;
					sym_addr = (sym_addr-q->address)>>(*p)->howto->rightshift;
					break;
#endif

#ifdef TARGET_v850
				case R_V850_ABS32:
					relocation_needed = 1;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_V850_ZDA_16_16_OFFSET:
				case R_V850_ZDA_16_16_SPLIT_OFFSET:
					/* Can't support zero-relocations.  */
					printf ("ERROR: %s+0x%"BFD_VMA_FMT"x: zero relocations not supported\n",
							sym_name, q->addend);
					continue;
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
					r_mem -= 1; /* tracks q->address */
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr |= (*(unsigned char *)r_mem<<24);
					break;
				case R_H8_DIR24A8:
					if (sym_reloc_size != 4) {
						printf("R_H8_DIR24A8 size %d\n", sym_reloc_size);
						bad_relocs++;
						continue;
					}
					/* Absolute symbol done not relocation */
					relocation_needed = !bfd_is_abs_section(sym_section);
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					break;
				case R_H8_DIR32:
				case R_H8_DIR32A16: /* currently 32,  could be made 16 */
				case R_H8_DISP32A16:
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
					relocation_needed = 0;
					sym_vma = 0;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 2);
					if (bfd_big_endian(abs_bfd))
					*(unsigned short *)r_mem =
						bfd_big_endian(abs_bfd) ? htons(sym_addr) : sym_addr;
					continue;
				case R_H8_PCREL8:
					relocation_needed = 0;
					sym_vma = 0;
					sym_addr = (*(q->sym_ptr_ptr))->value;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 1);
					*(unsigned char *)r_mem = sym_addr;
					continue;
#endif

#ifdef TARGET_microblaze
				case R_MICROBLAZE_64:
					/* work out the relocation */
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					/* Write relocated pointer back */
					r_mem[2] = (sym_addr >> 24) & 0xff;
					r_mem[3] = (sym_addr >> 16) & 0xff;
					r_mem[6] = (sym_addr >>  8) & 0xff;
					r_mem[7] =  sym_addr        & 0xff;
					relocation_needed = 1;
					/* The symbol is split over two consecutive instructions.  
					   Flag this to the flat loader by setting the high bit of 
					   the relocation symbol. */
					pflags = 0x80000000;
					break;
				case R_MICROBLAZE_32:
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					relocation_needed = 1;
					break;
				case R_MICROBLAZE_64_PCREL:
					sym_vma = 0;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 4);
					sym_addr = htonl(sym_addr);
					/* insert 16 MSB */
					* ((unsigned short *) (r_mem+2)) = (sym_addr) & 0xFFFF;
					/* then 16 LSB */
					* ((unsigned short *) (r_mem+6)) = (sym_addr >> 16) & 0xFFFF;
					/* We've done all the work, so continue
					   to next reloc instead of break */
					continue;
				/* These are already relocated for us as text-relative, or are dummy entries */
				case R_MICROBLAZE_PLT_64:
				case R_MICROBLAZE_NONE:
				case R_MICROBLAZE_64_NONE:
				case R_MICROBLAZE_32_PCREL_LO:
					relocation_needed = 0;
					continue;

#endif /* TARGET_microblaze */
					
#ifdef TARGET_nios2
#define  htoniosl(x)	(x)
#define  niostohl(x)	(x)
				case R_NIOS2_BFD_RELOC_32:
					relocation_needed = 1;
					pflags = (FLAT_NIOS2_R_32 << 28);
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					/* modify target, in target order */
					*(unsigned long *)r_mem = htoniosl(sym_addr);
					break;
				case R_NIOS2_CALL26:
				{
					unsigned long exist_val;
					relocation_needed = 1;
					pflags = (FLAT_NIOS2_R_CALL26 << 28);
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					
					/* modify target, in target order */
					// exist_val = niostohl(*(unsigned long *)r_mem);
					exist_val = ((sym_addr >> 2) << 6);
					*(unsigned long *)r_mem = htoniosl(exist_val);
					break;
				}
				case R_NIOS2_HIADJ16:
				case R_NIOS2_HI16:
				{
					unsigned long exist_val;
					int r2_type;
					/* handle the adjacent HI/LO pairs */
					if (relcount == 0)
						r2_type = R_NIOS2_NONE;
					else
						r2_type = p[1]->howto->type;
					if ((r2_type == R_NIOS2_LO16)
					    && (p[0]->sym_ptr_ptr == p[1]->sym_ptr_ptr)
					    && (p[0]->addend == p[1]->addend)) 
					    {
							unsigned char * r2_mem = sectionp + p[1]->address;
							if (p[1]->address - q->address!=4)
								printf("Err: HI/LO not adjacent %ld\n", p[1]->address - q->address);
							relocation_needed = 1;
							pflags = (q->howto->type == R_NIOS2_HIADJ16) 
								? FLAT_NIOS2_R_HIADJ_LO : FLAT_NIOS2_R_HI_LO;
							pflags <<= 28;
						
							sym_vma = bfd_section_vma(abs_bfd, sym_section);
							sym_addr += sym_vma + q->addend;

							/* modify high 16 bits, in target order */
							exist_val = niostohl(*(unsigned long *)r_mem);
							exist_val =  ((exist_val >> 22) << 22) | (exist_val & 0x3f);
							if (q->howto->type == R_NIOS2_HIADJ16)
								exist_val |= ((((sym_addr >> 16) + ((sym_addr >> 15) & 1)) & 0xFFFF) << 6);
							else
								exist_val |= (((sym_addr >> 16) & 0xFFFF) << 6);
							*(unsigned long *)r_mem = htoniosl(exist_val);

							/* modify low 16 bits, in target order */
							exist_val = niostohl(*(unsigned long *)r2_mem);
							exist_val =  ((exist_val >> 22) << 22) | (exist_val & 0x3f);
							exist_val |= ((sym_addr & 0xFFFF) << 6);
							*(unsigned long *)r2_mem = htoniosl(exist_val);
						
						} else 
							goto NIOS2_RELOC_ERR;
					}
					break;

				case R_NIOS2_GPREL:
				{
					unsigned long exist_val, temp;
					//long gp = get_symbol_offset("_gp", sym_section, symbols, number_of_symbols);
					long gp = get_gp_value(symbols, number_of_symbols);
					if (gp == -1) {
						printf("Err: unresolved symbol _gp when relocating %s\n", sym_name);
						goto NIOS2_RELOC_ERR;
					}
					/* _gp holds a absolute value, otherwise the ld cannot generate correct code */
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					//printf("sym=%x, %d, _gp=%x, %d\n", sym_addr+sym_vma, sym_addr+sym_vma, gp, gp);
					sym_addr += sym_vma + q->addend;
					sym_addr -= gp;
					//printf("sym - _gp=%x, %d\n", sym_addr, sym_addr);
					/* modify the target, in target order (little_endian) */
					exist_val = niostohl(*(unsigned long *)r_mem);
					temp = ((exist_val >> 6) & 0x3ff0000) | (sym_addr & 0xffff);
					temp <<= 6;
					temp |= (exist_val & 0x3f);
					*(unsigned long *)r_mem = htoniosl(temp);
					if (verbose)
						printf("omit: offset=0x%"BFD_VMA_FMT"x symbol=%s%s "
								"section=%s size=%d "
								"fixup=0x%x (reloc=0x%"BFD_VMA_FMT"x) GPREL\n",
								q->address, sym_name, addstr,
								section_name, sym_reloc_size,
								sym_addr, section_vma + q->address);
					continue;
				}
				case R_NIOS2_PCREL16:
				{
					unsigned long exist_val;
					sym_vma = 0;
					sym_addr += sym_vma + q->addend;
					sym_addr -= (q->address + 4);
					/* modify the target, in target order (little_endian) */
					exist_val = niostohl(*(unsigned long *)r_mem);
					exist_val =  ((exist_val >> 22) << 22) | (exist_val & 0x3f);
					exist_val |= ((sym_addr & 0xFFFF) << 6);
					*(unsigned long *)r_mem = htoniosl(exist_val);
					if (verbose)
						printf("omit: offset=0x%"BFD_VMA_FMT"x symbol=%s%s "
								"section=%s size=%d "
								"fixup=0x%x (reloc=0x%"BFD_VMA_FMT"x) PCREL\n",
								q->address, sym_name, addstr,
								section_name, sym_reloc_size,
								sym_addr, section_vma + q->address);
					continue;
				}

				case R_NIOS2_LO16:
					/* check if this is actually the 2nd half of a pair */
					if ((p > relpp)
						&& ((p[-1]->howto->type == R_NIOS2_HIADJ16) 
							|| (p[-1]->howto->type == R_NIOS2_HI16))
					    && (p[-1]->sym_ptr_ptr == p[0]->sym_ptr_ptr)
					    && (p[-1]->addend == p[0]->addend)) {
						if (verbose)
							printf("omit: offset=0x%"BFD_VMA_FMT"x symbol=%s%s "
								"section=%s size=%d LO16\n", 
								q->address, sym_name, addstr,
								section_name, sym_reloc_size);
						continue;
					}

					/* error, fall through */

				case R_NIOS2_S16:
				case R_NIOS2_U16:
				case R_NIOS2_CACHE_OPX:
				case R_NIOS2_IMM5:
				case R_NIOS2_IMM6:
				case R_NIOS2_IMM8:
				case R_NIOS2_BFD_RELOC_16:
				case R_NIOS2_BFD_RELOC_8:
				case R_NIOS2_GNU_VTINHERIT:
				case R_NIOS2_GNU_VTENTRY:
				case R_NIOS2_UJMP:
				case R_NIOS2_CJMP:
				case R_NIOS2_CALLR:
NIOS2_RELOC_ERR:
					printf("Err: unexpected reloc type %s(%d)\n", q->howto->name, q->howto->type);
					bad_relocs++;
					continue;
#endif /* TARGET_nios2 */

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
						ntohl(*(uint32_t *)r_mem)
						& 0xc0000000
						);
					break;
				case R_SPARC_HI22:
					relocation_needed = 1;
					pflags = 0x80000000;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr |= (
						htonl(*(uint32_t *)r_mem)
						& 0xffc00000
						);
					break;
				case R_SPARC_LO10:
					relocation_needed = 1;
					pflags = 0x40000000;
					sym_vma = bfd_section_vma(abs_bfd, sym_section);
					sym_addr += sym_vma + q->addend;
					sym_addr &= 0x000003ff;
					sym_addr |= (
						htonl(*(uint32_t *)r_mem)
						& 0xfffffc00
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

#ifdef TARGET_e1
#define  htoe1l(x)              htonl(x)
					
#if 0 
#define  DEBUG_E1
#endif

#ifdef   DEBUG_E1
#define  DBG_E1                 printf
#else
#define  DBG_E1(x, ...  )
#endif

#define _32BITS_RELOC 0x00000000
#define _30BITS_RELOC 0x80000000
#define _28BITS_RELOC 0x40000000
					{
				char *p;
				unsigned long   sec_vma, exist_val, S;
				case R_E1_CONST31:
						relocation_needed = 1;
						DBG_E1("Handling Reloc <CONST31>\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x], q->address : [0x%x]\n",
										sec_vma, sym_addr, q->address);
						sym_addr = sec_vma + sym_addr;
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);        
						DBG_E1("Orig:exist_val : [0x%08x]\n", exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n", exist_val);
						sym_addr += exist_val;
						pflags = _30BITS_RELOC;
						break;
				case R_E1_CONST31_PCREL:
						relocation_needed = 0;
						DBG_E1("Handling Reloc <CONST31_PCREL>\n");
						DBG_E1("DONT RELOCATE AT LOADING\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x], q->address : [0x%x]\n",
										sec_vma, sym_addr, q->address);
						sym_addr =  sec_vma + sym_addr;
						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%x]\n", sym_addr );

						DBG_E1("q->address : 0x%x, section_vma : 0x%x\n", q->address,
																		section_vma );
						q->address = q->address + section_vma;
						DBG_E1("q->address += section_vma : 0x%x\n", q->address );

						if( (sym_addr = (sym_addr - q->address - 6)) < 0 )
								DBG_E1("NEGATIVE OFFSET in PC Relative instruction\n");
						DBG_E1( "sym_addr := sym_addr - q->address  - "
								"sizeof(CONST31_PCREL): [0x%x]\n",
								sym_addr );
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);              
						DBG_E1("Orig:exist_val : [0x%08x]\n", exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n", exist_val);
						sym_addr |= exist_val;
						DBG_E1("sym_addr |=  exist_val) : [0x%x]\n", sym_addr );
						break;
				case R_E1_DIS29W_PCREL:
						relocation_needed = 0;
						DBG_E1("Handling Reloc <DIS29W_PCREL>\n");
						DBG_E1("DONT RELOCATE AT LOADING\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x], q->address : [0x%x]\n",
										sec_vma, sym_addr, q->address);
						sym_addr =  sec_vma + sym_addr;
						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%x]\n", sym_addr );

						DBG_E1("q->address : 0x%x, section_vma : 0x%x\n", q->address,
																		section_vma );
						q->address = q->address + section_vma;
						DBG_E1("q->address += section_vma : 0x%x\n", q->address );

						if( (sym_addr = (sym_addr - q->address - 6)) < 0 )
								DBG_E1("NEGATIVE OFFSET in PC Relative instruction\n");
						DBG_E1( "sym_addr := sym_addr - q->address  - "
								"sizeof(CONST31_PCREL): [0x%x]\n",
								sym_addr );
						DBG_E1("sectionp:[0x%x], q->address:[0x%x]\n", sectionp, q->address );
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);       
						DBG_E1("Original:exist_val : [0x%08x]\n",exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n",exist_val);
						sym_addr += exist_val;
						break;
				case R_E1_DIS29W:
						DBG_E1("Handling Reloc <DIS29W>\n");
						goto DIS29_RELOCATION;
				case R_E1_DIS29H:
						DBG_E1("Handling Reloc <DIS29H>\n");
						goto DIS29_RELOCATION;
				case R_E1_DIS29B:
						DBG_E1("Handling Reloc <DIS29B>\n");
DIS29_RELOCATION:
						relocation_needed = 1;
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%08x]\n",
										sec_vma, sym_addr);
						sym_addr =  sec_vma + sym_addr;
						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%08x]\n", sym_addr);
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);                
						DBG_E1("Orig:exist_val : [0x%08x]\n", exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n", exist_val);
						sym_addr +=  exist_val;
						DBG_E1("sym_addr +=  exist_val : [0x%08x]\n", sym_addr);
						pflags = _28BITS_RELOC;
						break;
				case R_E1_IMM32_PCREL:
						relocation_needed = 0;
						DBG_E1("Handling Reloc <IMM32_PCREL>\n");
						DBG_E1("DONT RELOCATE AT LOADING\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x]\n",
										sec_vma, sym_addr);
						sym_addr =  sec_vma + sym_addr;

						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%x]\n", sym_addr );
						DBG_E1("q->address : 0x%x, section_vma : 0x%x\n", q->address,
																		section_vma );
						q->address = q->address + section_vma;
						DBG_E1("q->address += section_vma : 0x%x\n", q->address );

						if( (sym_addr = (sym_addr - q->address - 6 )) < 0 )
								DBG_E1("NEGATIVE OFFSET in PC Relative instruction\n");
						DBG_E1( "sym_addr := sym_addr - q->address  - "
								"sizeof(CONST31_PCREL): [0x%x]\n",
								sym_addr );
						DBG_E1("sectionp:[0x%x], q->address:[0x%x]\n", sectionp, q->address );
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);                 
 						DBG_E1("Original:exist_val : [0x%08x]\n",exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n",exist_val);
						sym_addr += exist_val;
						break;
				case R_E1_IMM32:
						relocation_needed = 1;
						DBG_E1("Handling Reloc <IMM32>\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x]\n",
										sec_vma, sym_addr);
						sym_addr =  sec_vma + sym_addr;
						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%x]\n", sym_addr );
						DBG_E1("sectionp:[0x%x], q->address:[0x%x]\n", sectionp, q->address );
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address + 2);                     
	 					DBG_E1("Original:exist_val : [0x%08x]\n",exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n",exist_val);
						sym_addr += exist_val;
						pflags = _32BITS_RELOC;
						break;
				case R_E1_WORD:
						relocation_needed = 1;
						DBG_E1("Handling Reloc <WORD>\n");
						sec_vma = bfd_section_vma(abs_bfd, sym_section);
						DBG_E1("sec_vma : [0x%x], sym_addr : [0x%x]\n",
										sec_vma, sym_addr);
						sym_addr =  sec_vma + sym_addr;
						DBG_E1("sym_addr =  sec_vma + sym_addr : [0x%x]\n", sym_addr );
						exist_val = *(unsigned long*)((unsigned long)sectionp + q->address );
						DBG_E1("Orig:exist_val : [0x%08x]\n", exist_val);
						exist_val = htoe1l(exist_val);
						DBG_E1("HtoBE:exist_val : [0x%08x]\n", exist_val);
						sym_addr +=  exist_val;
						DBG_E1("sym_addr +=  exist_val : [0x%08x]\n", sym_addr);
						pflags = _32BITS_RELOC;
						break;
				}
#undef _32BITS_RELOC
#undef _30BITS_RELOC
#undef _28BITS_RELOC
#endif
				default:
					/* missing support for other types of relocs */
					printf("ERROR: bad reloc type (%s)%d\n", q->howto->name, (*p)->howto->type);
					bad_relocs++;
					continue;
				}
			}

			sprintf(&addstr[0], "+0x%lx", sym_addr - (*(q->sym_ptr_ptr))->value -
					 bfd_section_vma(abs_bfd, sym_section));


			/*
			 * for full elf relocation we have to write back the
			 * start_code relative value to use. Not needed with pic_with_got
			 * or if the fixup has already been done above (in which case update_text was set to 0)
			 */
			if (update_text) {
#if defined(TARGET_arm)
				union {
					unsigned char c[4];
					uint32_t l;
				} tmp;
				int32_t hl;
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

				tmp.l = *(uint32_t *)r_mem;
				hl = tmp.c[i0] | (tmp.c[i1] << 8) | (tmp.c[i2] << 16);
				if (use_resolved ||
					(((*p)->howto->type != R_ARM_PC24) &&
					((*p)->howto->type != R_ARM_PLT32)))
					hl |= (tmp.c[i3] << 24);
				else if (tmp.c[i2] & 0x80)
					hl |= 0xff000000; /* sign extend */
				if (!use_resolved)
					hl += sym_addr;
				tmp.c[i0] = hl & 0xff;
				tmp.c[i1] = (hl >> 8) & 0xff;
				tmp.c[i2] = (hl >> 16) & 0xff;
				if (use_resolved ||
					(((*p)->howto->type != R_ARM_PC24) &&
					((*p)->howto->type != R_ARM_PLT32)))
					tmp.c[i3] = (hl >> 24) & 0xff;
				if (((*p)->howto->type == R_ARM_ABS32) ||
				    ((*p)->howto->type == R_ARM_TARGET1) ||
				    ((*p)->howto->type == R_ARM_TARGET2))
					*(uint32_t *)r_mem = htonl(hl);
				else
					*(uint32_t *)r_mem = tmp.l;
#elif defined(TARGET_e1)
#define OPCODE_SIZE 2           /* Add 2 bytes, counting the opcode size*/
				switch ((*p)->howto->type) {
				case R_E1_CONST31:
				case R_E1_CONST31_PCREL:
				case R_E1_DIS29W_PCREL:
				case R_E1_DIS29W:
				case R_E1_DIS29H:
				case R_E1_DIS29B:
				case R_E1_IMM32_PCREL:
				case R_E1_IMM32:
						DBG_E1("In addr + 2:[0x%x] <- write [0x%x]\n",
								(sectionp + q->address + 2), sym_addr );
						*((unsigned long *) (sectionp + q->address + OPCODE_SIZE)) =
						htonl(sym_addr);
				break;
				case R_E1_WORD:
						DBG_E1("In addr : [0x%x] <- write [0x%x]\n",
								(sectionp + q->address), sym_addr );
						*((unsigned long *) (sectionp + q->address )) = htonl(sym_addr);
				break;
				default:
						fatal("ERROR: Unhandled Relocation: 0x%x", (*p)->howto->type);
				}
#elif defined TARGET_bfin
				if ((*p)->howto->type == R_BFIN_RIMM16
				    || (*p)->howto->type == R_BFIN_HUIMM16
				    || (*p)->howto->type == R_BFIN_LUIMM16)
				{
					/* for l and h we set the lower 16 bits which is only when it will be used */
					bfd_putl16 (sym_addr, sectionp + q->address);
				} else if ((*p)->howto->type == R_BFIN_BYTE4_DATA) {
					bfd_putl32 (sym_addr, sectionp + q->address);
				}
#else /* ! TARGET_arm && ! TARGET_e1 && ! TARGET_bfin */

				switch (q->howto->type) {
#ifdef TARGET_v850
				case R_V850_HI16_S:
				case R_V850_HI16:
				case R_V850_LO16:
					/* Do nothing -- for cases we handle,
					   the bits produced by the linker are
					   what we want in the final flat file
					   (and other cases are errors).  Note
					   that unlike most relocated values,
					   it is stored in little-endian order,
					   but this is necessary to avoid
					   trashing the low-bit, and the float
					   loaders knows about it.  */
					break;
#endif /* TARGET_V850 */

#ifdef TARGET_nios2
				case R_NIOS2_BFD_RELOC_32:
				case R_NIOS2_CALL26:
				case R_NIOS2_HIADJ16:
				case R_NIOS2_HI16:
					/* do nothing */
					break;
#endif /* TARGET_nios2 */

#if defined(TARGET_m68k)
				case R_68K_PC16:
					if (sym_addr < -0x8000 || sym_addr > 0x7fff) {
						fprintf (stderr, "Relocation overflow for R_68K_PC16 relocation against %s\n", sym_name);
						bad_relocs++;
					} else {
						r_mem[0] = (sym_addr >>  8) & 0xff;
						r_mem[1] =  sym_addr        & 0xff;
					}
					break;
#endif

				default:
					/* The alignment of the build host
					   might be stricter than that of the
					   target, so be careful.  We store in
					   network byte order. */
					r_mem[0] = (sym_addr >> 24) & 0xff;
					r_mem[1] = (sym_addr >> 16) & 0xff;
					r_mem[2] = (sym_addr >>  8) & 0xff;
					r_mem[3] =  sym_addr        & 0xff;
				}
#endif /* !TARGET_arm */
			}

			/*
			 *	Create relocation entry (PC relative doesn't need this).
			 */
			if (relocation_needed) {
				if (verbose)
					printf("  RELOC[%d]: offset=0x%"BFD_VMA_FMT"x symbol=%s%s "
						"section=%s size=%d "
						"fixup=0x%x (reloc=0x%"BFD_VMA_FMT"x)\n",
						flat_reloc_count,
						q->address, sym_name, addstr,
						section_name, sym_reloc_size,
						sym_addr, section_vma + q->address);

#ifndef TARGET_bfin
				flat_relocs = realloc(flat_relocs,
					(flat_reloc_count + 1) * sizeof(uint32_t));
#ifndef TARGET_e1
				flat_relocs[flat_reloc_count] = pflags |
					(section_vma + q->address);

#else
				switch ((*p)->howto->type) {
				case R_E1_CONST31:
				case R_E1_CONST31_PCREL:
				case R_E1_DIS29W_PCREL:
				case R_E1_DIS29W:
				case R_E1_DIS29H:
				case R_E1_DIS29B:
				case R_E1_IMM32_PCREL:
				case R_E1_IMM32:
				flat_relocs[flat_reloc_count] = pflags |
						(section_vma + q->address + OPCODE_SIZE);
				if (verbose)
						printf("RELOCATION TABLE : reloc[%d] = [0x%x]\n", flat_reloc_count,
										 flat_relocs[flat_reloc_count] );
				break;
				case R_E1_WORD:
				flat_relocs[flat_reloc_count] = pflags |
						(section_vma + q->address);
				if (verbose)
						printf("RELOCATION TABLE : reloc[%d] = [0x%x]\n", flat_reloc_count,
										 flat_relocs[flat_reloc_count] );
				break;
				}
#endif
				flat_reloc_count++;
#endif //TARGET_bfin
				relocation_needed = 0;
				pflags = 0;
			}

#if 0
printf("%s(%d): symbol name=%s address=0x%x section=%s -> RELOC=0x%x\n",
	__FILE__, __LINE__, sym_name, q->address, section_name,
	flat_relocs[flat_reloc_count]);
#endif
		}
	}
  }

  if (bad_relocs)
	fatal("%d bad relocs", bad_relocs);

  if (rc < 0)
	return(0);

  *n_relocs = flat_reloc_count;
  return flat_relocs;
}



static void usage(int status)
{
	fprintf(status ? stderr : stdout,
		"Usage: %s [vrzd] [-p <abs-pic-file>] [-s stack-size] "
		"[-o <output-file>] <elf-file>\n\n"
		"       -v              : verbose operation\n"
		"       -r              : force load to RAM\n"
		"       -k              : enable kernel trace on load (for debug)\n"
		"       -z              : compress code/data/relocs\n"
		"       -d              : compress data/relocs\n"
		"       -a              : use existing symbol references\n"
		"                         instead of recalculating from\n"
		"                         relocation info\n"
		"       -R reloc-file   : read relocations from a separate file\n"
		"       -p abs-pic-file : GOT/PIC processing with files\n"
		"       -s stacksize    : set application stack size\n"
		"       -o output-file  : output file name\n\n"
		"Compiled for " ARCH " architecture\n\n",
		elf2flt_progname);
	exit(status);
}


/* Write NUM zeroes to STREAM.  */
static void write_zeroes (unsigned long num, stream *stream)
{
  char zeroes[1024];
  if (num > 0) {
    /* It'd be nice if we could just use fseek, but that doesn't seem to
       work for stdio output files.  */
    memset(zeroes, 0x00, 1024);
    while (num > sizeof(zeroes)) {
      fwrite_stream(zeroes, sizeof(zeroes), 1, stream);
      num -= sizeof(zeroes);
    }
    if (num > 0)
      fwrite_stream(zeroes, num, 1, stream);
  }
}


static time_t get_build_date(void)
{
  const char *sde;
  unsigned long long epoch;
  char *endptr;

  sde = getenv("SOURCE_DATE_EPOCH");
  if (!sde)
    return time(NULL);

  /* Largely inspired from
     https://reproducible-builds.org/docs/source-date-epoch/ */
  errno = 0;
  epoch = strtoull(sde, &endptr, 10);
  if ((errno == ERANGE && (epoch == ULLONG_MAX || epoch == 0))
      || (errno != 0 && epoch == 0)
      || (endptr == sde)
      || (*endptr != '\0')
      || (epoch > ULONG_MAX))
    fatal("Invalid SOURCE_DATE_EPOCH value");

  return (time_t)epoch;
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
  stream gf;

  asymbol **symbol_table;
  long number_of_symbols;

  uint32_t data_len = 0;
  uint32_t bss_len = 0;
  uint32_t text_len = 0;
  uint32_t reloc_len;

  uint32_t data_vma = ~0;
  uint32_t bss_vma = ~0;
  uint32_t text_vma = ~0;

  uint32_t text_offs;

  void *text;
  void *data;
  uint32_t *reloc;

  struct flat_hdr hdr;

  elf2flt_progname = argv[0];
  xmalloc_set_program_name(elf2flt_progname);

  if (argc < 2)
    usage(1);

  if (sizeof(hdr) != 64)
    fatal(
	    "Potential flat header incompatibility detected\n"
	    "header size should be 64 but is %d",
	    sizeof(hdr));

#ifndef TARGET_e1
  stack = 4096;
#else /* We need plenty of stack for both of them (Aggregate and Register) */
  stack = 0x2020;
#endif

  while ((opt = getopt(argc, argv, "havzdrkp:s:o:R:")) != -1) {
    switch (opt) {
    case 'v':
      verbose++;
      break;
    case 'r':
      load_to_ram++;
      break;
    case 'k':
      ktrace++;
      break;
    case 'z':
      docompress = 1;
      break;
    case 'd':
      docompress = 2;
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
      if (sscanf(optarg, "%i", &stack) != 1) {
        fprintf(stderr, "%s invalid stack size %s\n", argv[0], optarg);
        usage(1);
      }
      break;
    case 'R':
      rel_file = optarg;
      break;
    case 'h':
      usage(0);
      break;
    default:
      fprintf(stderr, "%s Unknown option\n", argv[0]);
      usage(1);
      break;
    }
  }
  
  /*
   * if neither the -r or -p options was given,  default to
   * a RAM load as that is the only option that makes sense.
   */
  if (!load_to_ram && !pfile)
    load_to_ram = 1;

#if defined(TARGET_riscv64)
  /*
   * riscv only supports loading text and data contiguously.
   * So fail if load_to_ram is false.
   */
  if (!load_to_ram)
    fatal("Loading to RAM ('-r' option) is required");
#endif

  fname = argv[argc-1];

  if (pfile) {
    pic_with_got = 1;
    abs_file = pfile;
  } else
    abs_file = fname;

  if (! rel_file)
    rel_file = fname;

  if (!(rel_bfd = bfd_openr(rel_file, 0)))
    fatal_perror("Can't open '%s'", rel_file);

  if (bfd_check_format (rel_bfd, bfd_object) == 0)
    fatal("File is not an object file");

  if (abs_file == rel_file)
    abs_bfd = rel_bfd; /* one file does all */
  else {
    if (!(abs_bfd = bfd_openr(abs_file, 0)))
      fatal_perror("Can't open '%s'", abs_file);

    if (bfd_check_format (abs_bfd, bfd_object) == 0)
      fatal("File is not an object file");
  }

  if (! (bfd_get_file_flags(rel_bfd) & HAS_RELOC))
    fatal("%s: Input file contains no relocation info", rel_file);

  if (use_resolved && !(bfd_get_file_flags(abs_bfd) & EXEC_P))
    /* `Absolute' file is not absolute, so neither are address
       contained therein.  */
    fatal("%s: `-a' option specified with non-fully-resolved input file",
	     bfd_get_filename (abs_bfd));

  symbol_table = get_symbols(abs_bfd, &number_of_symbols);

  /* Group output sections into text, data, and bss, and calc their sizes.  */
  for (s = abs_bfd->sections; s != NULL; s = s->next) {
    uint32_t *vma, *len;
    bfd_size_type sec_size;
    bfd_vma sec_vma;

    if (s->flags & SEC_CODE) {
      vma = &text_vma;
      len = &text_len;
    } else if (s->flags & SEC_DATA) {
      vma = &data_vma;
      len = &data_len;
    } else if (s->flags & SEC_ALLOC) {
      vma = &bss_vma;
      len = &bss_len;
    } else
      continue;

    sec_size = bfd_section_size(abs_bfd, s);
    sec_vma  = bfd_section_vma(abs_bfd, s);

    if (sec_vma < *vma) {
      if (*len > 0)
	*len += sec_vma - *vma;
      else
	*len = sec_size;
      *vma = sec_vma;
    } else if (sec_vma + sec_size > *vma + *len)
      *len = sec_vma + sec_size - *vma;
  }

  if (text_len == 0)
    fatal("%s: no .text section", abs_file);

  text = xmalloc(text_len);

  if (verbose)
    printf("TEXT -> vma=0x%x len=0x%x\n", text_vma, text_len);

  /* Read in all text sections.  */
  for (s = abs_bfd->sections; s != NULL; s = s->next)
    if (s->flags & SEC_CODE) 
      if (!bfd_get_section_contents(abs_bfd, s,
				   text + (s->vma - text_vma), 0,
				   bfd_section_size(abs_bfd, s)))
      {
	fatal("read error section %s", s->name);
      }

  if (data_len == 0)
    fatal("%s: no .data section", abs_file);
  data = xmalloc(data_len);

  if (verbose)
    printf("DATA -> vma=0x%x len=0x%x\n", data_vma, data_len);

  if ((text_vma + text_len) != data_vma) {
    if ((text_vma + text_len) > data_vma)
      fatal("ERROR: text=0x%x overlaps data=0x%x ?", text_len, data_vma);
    if (verbose)
      printf("WARNING: data=0x%x does not directly follow text=0x%x\n",
	  		data_vma, text_len);
    text_len = data_vma - text_vma;
  }

  /* Read in all data sections.  */
  for (s = abs_bfd->sections; s != NULL; s = s->next)
    if (s->flags & SEC_DATA) 
      if (!bfd_get_section_contents(abs_bfd, s,
				   data + (s->vma - data_vma), 0,
				   bfd_section_size(abs_bfd, s)))
      {
	fatal("read error section %s", s->name);
      }

  if (bss_vma == ~0)
    bss_vma = data_vma + data_len;

  /* Put common symbols in bss.  */
  bss_len += add_com_to_bss(symbol_table, number_of_symbols, bss_len);

  if (verbose)
    printf("BSS  -> vma=0x%x len=0x%x\n", bss_vma, bss_len);

  if ((data_vma + data_len) != bss_vma) {
    if ((data_vma + data_len) > bss_vma)
      fatal("ERROR: text=0x%x + data=0x%x overlaps bss=0x%x ?", text_len,
	  		data_len, bss_vma);
    if (verbose)
      printf("WARNING: bss=0x%x does not directly follow text=0x%x + data=0x%x(0x%x)\n",
      		bss_vma, text_len, data_len, text_len + data_len);
    data_len = bss_vma - data_vma;
  }

  reloc = (uint32_t *)
    output_relocs(abs_bfd, symbol_table, number_of_symbols, &reloc_len,
		  text, text_len, text_vma, data, data_len, data_vma, rel_bfd);

  if (reloc == NULL && verbose)
    printf("No relocations in code!\n");

  text_offs = real_address_bits(text_vma);

  /* Fill in the binflt_flat header */
  memcpy(hdr.magic,"bFLT",4);
  hdr.rev         = htonl(FLAT_VERSION);
  hdr.entry       = htonl(sizeof(hdr) + bfd_get_start_address(abs_bfd));
  hdr.data_start  = htonl(sizeof(hdr) + text_offs + text_len);
  hdr.data_end    = htonl(sizeof(hdr) + text_offs + text_len +data_len);
  hdr.bss_end     = htonl(sizeof(hdr) + text_offs + text_len +data_len+bss_len);
  hdr.stack_size  = htonl(stack); /* FIXME */
  hdr.reloc_start = htonl(sizeof(hdr) + text_offs + text_len +data_len);
  hdr.reloc_count = htonl(reloc_len);
  hdr.flags       = htonl(0
	  | (load_to_ram || text_has_relocs ? FLAT_FLAG_RAM : 0)
	  | (ktrace ? FLAT_FLAG_KTRACE : 0)
	  | (pic_with_got ? FLAT_FLAG_GOTPIC : 0)
	  | (docompress ? (docompress == 2 ? FLAT_FLAG_GZDATA : FLAT_FLAG_GZIP) : 0)
	  );
  hdr.build_date = htonl((uint32_t)get_build_date());
  memset(hdr.filler, 0x00, sizeof(hdr.filler));

  for (i=0; i<reloc_len; i++) reloc[i] = htonl(reloc[i]);

  if (verbose) {
    printf("SIZE: .text=0x%04x, .data=0x%04x, .bss=0x%04x",
	text_len, data_len, bss_len);
    if (reloc)
      printf(", relocs=0x%04x", reloc_len);
    printf("\n");
  }
  
  if (!ofile) {
    ofile = xmalloc(strlen(fname) + 5 + 1); /* 5 to add suffix */
    strcpy(ofile, fname);
    strcat(ofile, ".bflt");
  }

  if ((fd = open (ofile, O_WRONLY|O_BINARY|O_CREAT|O_TRUNC, 0744)) < 0)
    fatal_perror("Can't open output file %s", ofile);

  if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
    fatal_perror("Couldn't write file %s", ofile);
  close(fd);

  if (fopen_stream_u(&gf, ofile, "a" BINARY_FILE_OPTS))
    fatal_perror("Can't open file %s for writing", ofile);

  if (docompress == 1)
    reopen_stream_compressed(&gf);

  /* Fill in any hole at the beginning of the text segment.  */
  if (verbose)
    printf("ZERO before text len=0x%x\n", text_offs);
  write_zeroes(text_offs, &gf);

  /* Write the text segment.  */
  fwrite_stream(text, text_len, 1, &gf);

  if (docompress == 2)
    reopen_stream_compressed(&gf);

  /* Write the data segment.  */
  fwrite_stream(data, data_len, 1, &gf);

  if (reloc)
    fwrite_stream(reloc, reloc_len * 4, 1, &gf);

  fclose_stream(&gf);

  exit(0);
}


/*
 * this __MUST__ be at the VERY end of the file - do NOT move!!
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * end:
 * vi: tabstop=8 shiftwidth=4 textwidth=79 noexpandtab
 */
