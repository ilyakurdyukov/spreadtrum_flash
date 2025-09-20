/* ld-script : UMS9117 FDL1 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

BSS_START = 0x0; BSS_SIZE = 0x6000;
IMAGE_START = 0x6200; IMAGE_SIZE = 0x5000;
STACK_SIZE = 0x2000;

ENTRY(_start)
SECTIONS {
	.bss BSS_START : {
		__bss_start = .;
		*(.bss .bss.* .gnu.linkonce.b.*)
		__bss_end = ALIGN(4);
	}
	ASSERT(__bss_end - BSS_START <= BSS_SIZE, "BSS overflow")

	.text IMAGE_START : {
		__image_start = .;
		*(.text._start) *(.text .text.*)
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.data .data.* .gnu.linkonce.d.*)
		. = ALIGN(4);
		__image_end = .;
		__stack_top = ALIGN(16);
		__stack_bottom = __stack_top + STACK_SIZE;
	}
	ASSERT(__stack_bottom - IMAGE_START <= IMAGE_SIZE, "image overflow")

	__image_size = __image_end - __image_start - 0x200;
	__bss_size = __bss_end - __image_end;

	/DISCARD/ : { *(.ARM.exidx .ARM.exidx.*) }
}
