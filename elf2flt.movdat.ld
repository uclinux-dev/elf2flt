
ENTRY (_start)

MEMORY {
	flatmem : ORIGIN = 0x0, LENGTH = 0x100000
	junk    : ORIGIN = 0x0, LENGTH = 0x100000
}

SECTIONS {
	.text 0 : {
		. = . + 4;
		. = ALIGN(0x4) ;
		_stext = . ;
		*(.text)
		*(.text.*)
		*(.gnu.warning)
		*(.stub)
		*(.gnu.linkonce.t*)
		*(.glue_7t)
		*(.glue_7)
		*(.init)
		*(.fini)

		*(.rodata)
		*(.rodata1)
		*(.rodata.*)

		/* This is special code area at the end of the normal
		   text section.  It contains a small lookup table at
		   the start followed by the code pointed to by entries
		   in the lookup table.  */
		. = ALIGN (4) ;
		PROVIDE(__ctbp = .);
		*(.call_table_data)
		*(.call_table_text)

		. = ALIGN(0x10) ;
		_etext = . ;
	} > flatmem
	.data : {
		. = ALIGN(0x4) ;
		_sdata = . ;
		__data_start = . ;
		data_start = . ;
		*(.got.plt)
		*(.got)
		LONG(-1)
		*(.gnu.linkonce.r*)
		*(.data)
		*(.data1)
		*(.data.*)
		*(.gnu.linkonce.d*)
		*(.data1)
		*(.eh_frame)
		*(.gcc_except_table)
		*(.sdata) 
		*(.sdata.*)
		*(.gnu.linkonce.s.*)
		*(__libc_atexit)
		*(__libc_subinit)
		*(__libc_subfreeres)
		*(.note.ABI-tag)
		. = ALIGN(4) ;
		*(.ctors.*)
		*(.ctors)
		LONG(0)
		*(.dtors.*)
		*(.dtors)
		LONG(0)
		. = ALIGN(0x10) ;
		_edata = . ;
	} > flatmem
	.bss : {
		. = ALIGN(0x4) ;
		_sbss = ALIGN(0x4) ;
		__bss_start = . ;
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.scommon)
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(0x4) ;
		_ebss = . ;
		_end = . ;
		end = . ;
	} > flatmem

	.junk : {
		*(.rel*)
		*(.rela*)
	} > junk
}
