#include "card_fs.h"
#include "card_layout.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *media;
static sdmmc_card_t card;
static bool registered, missing;
static int fail_after = -1;
static unsigned writes;
PARTITION VolToPart[FF_VOLUMES] = {{0,0},{1,0}};
DWORD get_fattime(void) { return ((2026-1980)<<25) | (9<<21) | (29<<16); }
void *ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void *p) { free(p); }
int ff_mutex_create(int v) { (void)v; return 1; }
void ff_mutex_delete(int v) { (void)v; }
int ff_mutex_take(int v) { (void)v; return 1; }
void ff_mutex_give(int v) { (void)v; }
esp_err_t ff_diskio_get_drive(BYTE *drive) { assert(!registered); *drive=0; return ESP_OK; }
void ff_diskio_register_sdmmc(BYTE drive,sdmmc_card_t *c) { assert(drive==0 && c==&card);registered=true; }
void ff_diskio_unregister(BYTE drive) { assert(drive==0);registered=false; }
esp_err_t sdmmc_get_status(sdmmc_card_t *c) { return c && !missing ? ESP_OK : ESP_FAIL; }
esp_err_t sdmmc_write_sectors(sdmmc_card_t *c,const void *data,size_t sector,size_t count) {
    assert(c==&card && sector+count<=card.csd.capacity); writes++;
    if (fail_after==0) return ESP_FAIL;
    if (fail_after>0) fail_after--;
    assert(fseeko(media,(off_t)sector*512,SEEK_SET)==0);
    return fwrite(data,512,count,media)==count ? ESP_OK : ESP_FAIL;
}
DSTATUS disk_initialize(BYTE p) { (void)p;return missing ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE p) { (void)p;return missing ? STA_NOINIT : 0; }
DRESULT disk_read(BYTE p,BYTE *b,LBA_t sector,UINT count) {
    (void)p;assert(registered && sector+count<=card.csd.capacity);
    assert(fseeko(media,(off_t)sector*512,SEEK_SET)==0);
    return fread(b,512,count,media)==count ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE p,const BYTE *b,LBA_t sector,UINT count) {
    (void)p;return sdmmc_write_sectors(&card,b,sector,count)==ESP_OK ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *buffer) {
    (void)p;
    switch(cmd) {
    case CTRL_SYNC: return fflush(media)==0 ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(DWORD*)buffer=card.csd.capacity;return RES_OK;
    case GET_SECTOR_SIZE: *(WORD*)buffer=512;return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)buffer=1;return RES_OK;
    case CTRL_TRIM: return RES_OK;
    default: return RES_PARERR;
    }
}
static uint32_t le32(const uint8_t *b) { return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static void fresh(uint32_t sectors) {
    if(media)fclose(media);
    media=tmpfile();assert(media);
    card.csd.capacity=sectors;card.csd.sector_size=512;
    assert(ftruncate(fileno(media),(off_t)sectors*512)==0);
    writes=0;fail_after=-1;missing=false;
}

static void rename_preserves_files(void) {
    fresh(245760); // Same capacity as the user's 120 MiB card.
    card_info_t info;
    assert(card_fs_format(&card,&info)==ESP_OK);
    FATFS fs={0}; FIL file={0}; UINT count;
    registered=true; assert(f_mount(&fs,"0:",1)==FR_OK);
    assert(f_setlabel("0:MY DESIGNS")==FR_OK);
    assert(f_mkdir("0:/DESIGNS")==FR_OK);
    unsigned char design[7000],readback[7000];
    for(unsigned i=0;i<sizeof(design);i++)design[i]=(unsigned char)(i*37);
    assert(f_open(&file,"0:/DESIGNS/KEEP.PES",FA_CREATE_NEW|FA_WRITE)==FR_OK);
    assert(f_write(&file,design,sizeof(design),&count)==FR_OK && count==sizeof(design));
    assert(f_close(&file)==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);registered=false;
    uint8_t layout[2049*512],after[sizeof(layout)];
    assert(fseeko(media,0,SEEK_SET)==0);assert(fread(layout,1,sizeof(layout),media)==sizeof(layout));
    assert(card_fs_inspect(&card,&info)==ESP_OK && !strcmp(info.label,"MY DESIGNS"));
    fail_after=0;
    assert(card_fs_rename(&card,&info)!=ESP_OK && !registered);
    fail_after=-1;
    assert(card_fs_rename(&card,&info)==ESP_OK && !strcmp(info.label,CARD_VOLUME_LABEL));
    assert(fseeko(media,0,SEEK_SET)==0);assert(fread(after,1,sizeof(after),media)==sizeof(after));
    assert(!memcmp(layout,after,sizeof(layout))); // MBR, reserved area, boot sector unchanged.
    unsigned before=writes;
    assert(card_fs_rename(&card,&info)==ESP_OK && writes==before); // Already named: no writes.
    assert(card_fs_inspect(&card,&info)==ESP_OK && !strcmp(info.label,CARD_VOLUME_LABEL));
    registered=true;assert(f_mount(&fs,"0:",1)==FR_OK);
    assert(f_open(&file,"0:/DESIGNS/KEEP.PES",FA_READ)==FR_OK);
    assert(f_read(&file,readback,sizeof(readback),&count)==FR_OK && count==sizeof(design));
    assert(!memcmp(design,readback,sizeof(design)));assert(f_close(&file)==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);registered=false;
    fresh(245760);
    assert(card_fs_rename(&card,&info)!=ESP_OK && writes==0); // No filesystem: never format.
    registered=true;
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=2};uint8_t work[4096];
    assert(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);registered=false;
    before=writes;
    assert(card_fs_inspect(&card,&info)==ESP_OK && info.readable && !info.fat32);
    assert(card_fs_rename(&card,&info)!=ESP_OK && writes==before); // FAT16 left intact.
    missing=true;
    assert(card_fs_rename(&card,&info)!=ESP_OK && writes==before);
}
int main(void) {
    card_info_t info;
    // Include decimal card labels and both sides of each cluster-size transition.
    const uint32_t sizes[]={131072,250000,262144,262145,500000,524288,524289,
                            1000000,1048576,1048577,4194304,8388608,8388609,
                            16777216,67108864};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++) {
        fresh(sizes[i]);
        assert(card_fs_inspect(&card,&info)!=ESP_OK && info.present && !info.readable && writes==0);
        assert(card_fs_format(&card,&info)==ESP_OK && info.fat32 && info.readable);
        assert(!strcmp(info.label,CARD_VOLUME_LABEL));
        unsigned before=writes;
        assert(card_fs_inspect(&card,&info)==ESP_OK && info.fat32 && writes==before && !registered);
        uint8_t mbr[512];assert(fseeko(media,0,SEEK_SET)==0);assert(fread(mbr,1,512,media)==512);
        assert(mbr[510]==0x55 && mbr[511]==0xaa && mbr[450]==0x0c);
        assert(le32(mbr+454)==2048 && le32(mbr+458)==sizes[i]-2048);
        for(unsigned j=462;j<510;j++)assert(mbr[j]==0);
        uint8_t boot[512];assert(fseeko(media,(off_t)2048*512,SEEK_SET)==0);
        assert(fread(boot,1,512,media)==512);
        // Verify actual on-disk cluster size and two FATs, not only the helper.
        assert((unsigned)boot[13]*512==card_layout_cluster_size(sizes[i]));
        assert(boot[16]==2);
    }
    fresh(131071);assert(card_fs_format(&card,&info)==ESP_ERR_NOT_SUPPORTED && writes==0);
    fresh(67108865);assert(card_fs_format(&card,&info)==ESP_ERR_NOT_SUPPORTED && writes==0);
    fresh(1048576);card.csd.sector_size=4096;assert(card_fs_format(&card,&info)==ESP_ERR_NOT_SUPPORTED && writes==0);
    fresh(1048576);missing=true;assert(card_fs_format(&card,&info)!=ESP_OK && !info.present && writes==0);
    const int faults[]={0,1,35,70,100};
    for(unsigned i=0;i<sizeof(faults)/sizeof(*faults);i++) {
        fresh(250000);fail_after=faults[i];
        assert(card_fs_format(&card,&info)!=ESP_OK && !info.readable && !registered);
        fail_after=-1;
        assert(card_fs_format(&card,&info)==ESP_OK && info.fat32 && !registered);
    }
    rename_preserves_files();
    fclose(media);puts("Card formatter: real FatFs, capacity boundaries, read-only inspection, write failures, rename file preservation and recovery passed");
}
