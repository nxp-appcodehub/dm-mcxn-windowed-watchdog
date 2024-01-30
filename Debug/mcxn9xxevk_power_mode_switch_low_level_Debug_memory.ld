/*
 * GENERATED FILE - DO NOT EDIT
 * Copyright 2008-2013 Code Red Technologies Ltd,
 * Copyright 2013-2024 NXP
 * Generated linker script file for MCXN947
 * Created from memory.ldt by FMCreateLinkMemory
 * Using Freemarker v2.3.30
 * MCUXpresso IDE v11.8.0 [Build 1165] [2023-07-26] on Jan 23, 2024, 3:15:09 PM
 */

MEMORY
{
  /* Define each memory region */
  PROGRAM_FLASH (rx) : ORIGIN = 0x0, LENGTH = 0xc0000 /* 768K bytes (alias Flash) */  
  Ram0 (rwx) : ORIGIN = 0x20000000, LENGTH = 0x4e000 /* 312K bytes (alias RAM) */  
}

  /* Define a symbol for the top of each memory region */
  __base_PROGRAM_FLASH = 0x0  ; /* PROGRAM_FLASH */  
  __base_Flash = 0x0 ; /* Flash */  
  __top_PROGRAM_FLASH = 0x0 + 0xc0000 ; /* 768K bytes */  
  __top_Flash = 0x0 + 0xc0000 ; /* 768K bytes */  
  __base_Ram0 = 0x20000000  ; /* Ram0 */  
  __base_RAM = 0x20000000 ; /* RAM */  
  __top_Ram0 = 0x20000000 + 0x4e000 ; /* 312K bytes */  
  __top_RAM = 0x20000000 + 0x4e000 ; /* 312K bytes */  
