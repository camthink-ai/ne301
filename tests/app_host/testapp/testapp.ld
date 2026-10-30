ENTRY(app_entry)

MEMORY
{
  APP (rx) : ORIGIN = 0x93E00000, LENGTH = 2M
}

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > APP
}
