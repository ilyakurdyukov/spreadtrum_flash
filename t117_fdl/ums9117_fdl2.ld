/* ld-script : UMS9117 FDL2 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)

EXTRA_START = 0x80000000; EXTRA_SIZE = 0x100000;
IMAGE_START = 0x80100000; IMAGE_SIZE = 0x300000;

ENTRY(_start)
SECTIONS {
	.text IMAGE_START : {
		__image_start = .;
		*(.text._start) *(.text .text.*)

		. = ALIGN(4);
		__init_array_start = .;
		/* the linker from android-ndk-r15c doesn't support SORT_BY_INIT_PRIORITY */
		/* this feature is not required for simple code */
		/* KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*))) */
		KEEP(*(.init_array.* .ctors.*))
		KEEP(*(.init_array .ctors))
		__init_array_end = .;

		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.data .data.* .gnu.linkonce.d.*)
		. = ALIGN(4);
		__image_end = .;
	}
	.bss : {
		__bss_start = .;
		*(.bss .bss.* .gnu.linkonce.b.*) *(COMMON)
		. = ALIGN(4);
		__bss_end = .;
	}

	__image_size = __image_end - __image_start;
	__bss_size = __bss_end - __image_end;

	__stack_top = EXTRA_START;
	__stack_bottom = __stack_top + EXTRA_SIZE;

	ASSERT(__bss_end - IMAGE_START <= IMAGE_SIZE, "image overflow")

	/* Clang's linker tries to insert this section between .text and .bss if I don't list it here or discard it. */
  .dynamic : { *(.dynamic) }

	/* workaround for "relocation R_ARM_PREL31 out of range" */
	/DISCARD/ : { *(.ARM.exidx .ARM.exidx.*) }
}
