#include "card_layout.h"
#include <string.h>
bool card_layout_supported(uint64_t sectors, unsigned sector_size)
{
    return sector_size == 512 && sectors >= 131072 && sectors <= 67108864;
}
static void le32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}
bool card_layout_mbr(uint8_t out[512], uint64_t sectors, unsigned sector_size)
{
    if (!card_layout_supported(sectors, sector_size)) return false;
    memset(out, 0, 512);
    uint8_t *part = out + 446;
    // One non-bootable FAT32 LBA partition, aligned at 1 MiB.
    part[1] = part[5] = 0xfe;
    part[2] = part[6] = 0xff;
    part[3] = part[7] = 0xff;
    part[4] = 0x0c;
    le32(part + 8, 2048);
    le32(part + 12, (uint32_t)sectors - 2048);
    out[510] = 0x55; out[511] = 0xaa;
    return true;
}
unsigned card_layout_cluster_size(uint64_t sectors)
{
    // Keep enough data clusters for FAT32, including decimal-labelled small
    // cards (a 128 MB card is smaller than 128 MiB). Each boundary is tested.
    if (sectors <= 262144) return 512;
    if (sectors <= 524288) return 1024;
    if (sectors <= 1048576) return 2048;
    return sectors <= 8388608 ? 4096 : 32768;
}
