/*
 * crtos-app.ld - added to the linker's own script when a CRTOS program is linked (ld -r).
 *
 * A partial link keeps ".init_array.NNNNN" (constructors with a priority) apart from
 * ".init_array", and the kernel runs only the section ".init_array" (with ".preinit_array"
 * right before it). Here they become one table each, in priority order as in a full link.
 */
SECTIONS
{
  .preinit_array 0 : { KEEP (*(.preinit_array)) }
  .init_array 0 : {
    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP (*(.init_array .ctors))
  }
  .fini_array 0 : {
    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
    KEEP (*(.fini_array .dtors))
  }
}
INSERT AFTER .text;
