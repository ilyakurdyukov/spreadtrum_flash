/* ld-script : SC6531E FDL1 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

BSS_START = 0x40000000; BSS_SIZE = 0x4000;
IMAGE_START = 0x40004000; IMAGE_SIZE = 0x3000;
STACK_SIZE = 0x800;

ENTRY(_start)
SECTIONS {
	. = BSS_START; __bss_start = .;
	.bss : { *(.bss .bss.* .gnu.linkonce.b.*) }
	__bss_end = ALIGN(16);

	ASSERT(__bss_end - BSS_START <= BSS_SIZE, "BSS overflow")

	. = IMAGE_START; __image_start = .;
	.text : {
		*(.text._start) *(.text .text.*)
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.data .data.* .gnu.linkonce.d.*)
	}
	__image_end = .;
	__stack_top = ALIGN(16);
	__stack_bottom = __stack_top + STACK_SIZE;

	ASSERT(__stack_bottom - IMAGE_START <= IMAGE_SIZE, "image overflow")

	.shstrtab : { *(.shstrtab) }
	.got : { *(.got) }

	/DISCARD/ : { *(*) }
}
