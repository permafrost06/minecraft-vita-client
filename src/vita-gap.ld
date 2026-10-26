/* vita-elf-create appends SCE data after the code segment; keep at least one
   free page before the data segment so that space always exists. */
SECTIONS { . = ALIGN(0x1000) + 0x1000; } INSERT BEFORE .init_array;
