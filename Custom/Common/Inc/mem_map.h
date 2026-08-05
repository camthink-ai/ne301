#ifndef __MEMORY_MAP_H__
#define __MEMORY_MAP_H__

/*
=================== Memory Region Layout ===================
|   Region      |   Start Address   |   End Address     |   Size     |
| APP           | 0x90000000        | 0x903FFFFF        | 4096K      |
| SRAM_POOL     | 0x34000000        | 0x341B1FFF        | 1736K      |
| UNCACHED      | 0x341B2000        | 0x341FFFFF        | 312K       |
| AI            | 0x34200000        | 0x343BFFFF        | 1M + 768K  |
#if defined(BOARD_PSRAM_SIZE) && BOARD_PSRAM_SIZE == 64
| PSRAM REGION  | 0x90400000        | 0x93FFFFFF        | 60M        |
#else // 32M PSRAM
| PSRAM REGION  | 0x90400000        | 0x91FFFFFF        | 28M        |
#endif

=================== Flash Partition Layout ===================
|   Partition    |   Start Address   |   End Address     |   Size     |
| FSBL           | 0x70000000        | 0x7007FFFF        | 512K       |
| NVS            | 0x70080000        | 0x7008FFFF        | 64K        |
| OTA            | 0x70090000        | 0x70091FFF        | 8K         |
| SWAP           | 0x70092000        | 0x700A1FFF        | 64K        |
| Reserve1       | 0x700A2000        | 0x700FFFFF        | 376K       |
| APP1           | 0x70100000        | 0x704FFFFF        | 4M         |
| APP2           | 0x70500000        | 0x708FFFFF        | 4M         |
| AI_1           | 0x70900000        | 0x710FFFFF        | 8M         |
| AI_2           | 0x71100000        | 0x718FFFFF        | 8M         |
| WEB            | 0x71900000        | 0x719FFFFF        | 1M         |
| WiFi FW        | 0x71A00000        | 0x71CFFFFF        | 3M         |
#if defined(BOARD_FLASH_SIZE) && BOARD_FLASH_SIZE == 128
| LittleFS       | 0x71D00000        | 0x77CFFFFF        | 96M        |
| Reserve2       | 0x77D00000        | 0x780FFFFF        | 4M         |
#else // 64M flash
| LittleFS       | 0x71D00000        | 0x73CFFFFF        | 32M        |
| Reserve2       | 0x73D00000        | 0x740FFFFF        | 4M         |
#endif
*/


// =================== Memory Regions ===================
#define SRAM_BASE            0x34000000U
#define SRAM_APP_BASE        0x90000000U    // 4096K
#define SRAM_APP_END         0x903FFFFFU
#define SRAM_APP_SIZE        (0x90400000U - 0x90000400U)   // 4095K
#define SRAM_POOL_BASE       0x34000000U
#define SRAM_POOL_END        0x341B1FFFU
#define SRAM_POOL_SIZE       (0x341B2000U - 0x34000000U)   // 1736K
#define SRAM_UNCACHED_BASE   0x341B2000U
#define SRAM_UNCACHED_END    0x341FFFFFU
#define SRAM_UNCACHED_SIZE   (0x34200000U - 0x341B2000U)   // 312K
#define SRAM_AI_BASE         0x34200000U
#define SRAM_AI_END          0x343BFFFFU
#define SRAM_AI_SIZE         (0x343C0000U - 0x34200000U)   // 1M + 768K

#define PSRAM_SWAP_BASE      0x90400000U
#define PSRAM_SWAP_END       0x9040FFFFU
#define PSRAM_SWAP_SIZE      (0x90400000U - 0x903F0000U)   // 64K

#define PSRAM_REGION_BASE     0x90400000
#if defined(BOARD_PSRAM_SIZE) && BOARD_PSRAM_SIZE == 64
#define PSRAM_REGION_END      0x93FFFFFFU
#define PSRAM_REGION_SIZE     (0x94000000U - 0x90400000U)   // 60M
#else
#define PSRAM_REGION_END      0x91FFFFFFU
#define PSRAM_REGION_SIZE     (0x92000000U - 0x90400000U)   // 28M
#endif

// =================== Flash Partitions ===================
#define FLASH_BASE      0x70000000U
#define FSBL_BASE       0x70000000U
#define FSBL_END        0x7007FFFFU
#define FSBL_SIZE       (0x70080000U - 0x70000000U)   // 512K
#define NVS_BASE        0x70080000U
#define NVS_END         0x7008FFFFU
#define NVS_SIZE        (0x70090000U - 0x70080000U)   // 64K
#define OTA_BASE        0x70090000U
#define OTA_END         0x70091FFFU
#define OTA_SIZE        (0x70092000U - 0x70090000U)   // 8K
#define SWAP_BASE       0x70092000U
#define SWAP_END        0x700A1FFFU
#define SWAP_SIZE       (0x700A2000U - 0x70092000U)   // 64K
#define RESERVE1_BASE   0x700A2000U
#define RESERVE1_END    0x700FFFFFU
#define RESERVE1_SIZE   (0x70100000U - 0x700A2000U)   // 376K
#define APP1_BASE       0x70100000U
#define APP1_END        0x704FFFFFU
#define APP1_SIZE       (0x70500000U - 0x70100000U)   // 4M
#define APP2_BASE       0x70500000U
#define APP2_END        0x708FFFFFU
#define APP2_SIZE       (0x70900000U - 0x70500000U)   // 4M
#define AI_1_BASE       0x70900000U
#define AI_1_END        0x710FFFFFU
#define AI_1_SIZE       (0x71100000U - 0x70900000U)   // 8M
#define AI_2_BASE       0x71100000U
#define AI_2_END        0x718FFFFFU
#define AI_2_SIZE       (0x71900000U - 0x71100000U)   // 8M
#define WEB_BASE        0x71900000U
#define WEB_END         0x719FFFFFU
#define WEB_SIZE        (0x71A00000U - 0x71900000U)   // 1M
#define WIFI_FW_BASE    0x71A00000U
#define WIFI_FW_END     0x71CFFFFFU
#define WIFI_FW_SIZE    (0x71D00000U - 0x71A00000U)   // 3M
#if defined(BOARD_FLASH_SIZE) && BOARD_FLASH_SIZE == 128
#define LITTLEFS_BASE   0x71D00000U
#define LITTLEFS_END    0x77CFFFFFU
#define LITTLEFS_SIZE   (0x77D00000U - 0x71D00000U)   // 96M
#define RESERVE2_BASE   0x77D00000U
#define RESERVE2_END    0x780FFFFFU
#define RESERVE2_SIZE   (0x78100000U - 0x77D00000U)   // 4M
#else
#define LITTLEFS_BASE   0x71D00000U
#define LITTLEFS_END    0x73CFFFFFU
#define LITTLEFS_SIZE   (0x73D00000U - 0x71D00000U)   // 32M
#define RESERVE2_BASE   0x73D00000U
#define RESERVE2_END    0x740FFFFFU
#define RESERVE2_SIZE   (0x74100000U - 0x73D00000U)   // 4M
#endif

#endif // __MEMORY_MAP_H__