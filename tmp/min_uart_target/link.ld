ENTRY(_flash_entry)
SECTIONS
{
    . = 0x42000000;
    .flash_header : { *(.flash_header) }
    .text.entry   : { *(.text.entry) }
    .text         : { *(.text) }
}
