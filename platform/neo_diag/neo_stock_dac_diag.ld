ENTRY(stock_diag_entry)
MEMORY {
 CODE (rx) : ORIGIN = 0x03079D00, LENGTH = 0x2AB0
 DATA (rw) : ORIGIN = 0x03048000, LENGTH = 0x3000
}
SECTIONS {
 .code : { KEEP(*(.code_vectors)) KEEP(*(.hook_entry)) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > CODE
 .bss (NOLOAD) : { device_bss_start = .; *(.bss .bss.*) . = ALIGN(4); device_bss_end = .; } > DATA
 .noinit (NOLOAD) : { *(.noinit .noinit.*) } > DATA
 /DISCARD/ : { *(.entry_vector) *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(SIZEOF(.code) <= 0x2AB0, "payload must stay below stock overlays")
