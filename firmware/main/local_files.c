#include "local_files.h"
#include "storage.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_CAP 256
#define FULL_CAP 320
#define PAGE_SIZE 32
#define SCAN_LIMIT 4096
#ifdef ESP_PLATFORM
#include "esp_random.h"
#define safe_stat stat // FAT has no symlinks.
#else
#define safe_stat lstat
#endif

static bool component_valid(const char *s)
{
    size_t n = strlen(s), stem = strcspn(s, ".");
    if (!n || n > 127 || s[0] == '.' || s[0] == '~' || s[n-1] == '.' || s[n-1] == ' ') return false;
    for (size_t i=0; i<n; i++) if ((unsigned char)s[i]<32 || (unsigned char)s[i]>126 || strchr("\\/:*?\"<>|",s[i])) return false;
    if ((stem==3 && (!strncasecmp(s,"CON",3)||!strncasecmp(s,"PRN",3)||!strncasecmp(s,"AUX",3)||!strncasecmp(s,"NUL",3))) ||
        (stem==4 && (!strncasecmp(s,"COM",3)||!strncasecmp(s,"LPT",3)) && s[3]>='1' && s[3]<='9')) return false;
    return strcasecmp(s,"START HERE.html") && strcasecmp(s,"START HERE - Get Ember Bridge.html");
}
bool local_path_valid(const char *p, bool root)
{
    if (!p || strlen(p)>=PATH_CAP) return false;
    if (!*p) return root;
    char copy[PATH_CAP]; strcpy(copy,p);
    unsigned depth=0;
    char *part=copy;
    for (;;) {
        char *slash=strchr(part,'/'); if (slash) *slash=0;
        if (++depth>8 || !component_valid(part)) return false;
        if (!slash) return true;
        part=slash+1;
    }
}
static void full(char out[FULL_CAP],const char *p) { snprintf(out,FULL_CAP,"%s/%s",storage_base_path(),p); }
static void parent_of(const char *p,char out[PATH_CAP]) { strcpy(out,p); char *slash=strrchr(out,'/'); if(slash)*slash=0; else *out=0; }
// Reject noncanonical spellings and short-name aliases by walking real entries.
// Every existing ancestor must be a directory; symlinks never leave the root.
static bool canonical(const char *p, bool missing_leaf)
{
    char copy[PATH_CAP], walked[PATH_CAP]=""; strcpy(copy,p);
    char *part=copy;
    if (!*part) return true;
    for (;;) {
        char *slash=strchr(part,'/'); if(slash)*slash=0;
        char base[FULL_CAP]; full(base,walked);
        DIR *d=opendir(base); if(!d)return false;
        bool found=false; struct dirent *e;
        while((e=readdir(d))) if(!strcasecmp(e->d_name,part)) { found=!strcmp(e->d_name,part); break; }
        bool collision=e!=NULL; closedir(d);
        if(!found) return !slash && missing_leaf && !collision;
        size_t len=strlen(walked), part_len=strlen(part);
        if(len+part_len+(len?1:0)>=sizeof(walked))return false;
        if(len)walked[len++]='/';
        memcpy(walked+len,part,part_len+1);
        full(base,walked); struct stat st;
        if(safe_stat(base,&st) || (!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)) || (slash&&!S_ISDIR(st.st_mode))) return false;
        if(!slash)return true;
        part=slash+1;
    }
}
// A snapshot from a previous boot/card session must never authorize a write.
static uint64_t boot_nonce(void)
{
    static uint64_t nonce;
    if(!nonce) {
#ifdef ESP_PLATFORM
        nonce=((uint64_t)esp_random()<<32)|esp_random();
#else
        nonce=(uint64_t)getpid();
#endif
        if(!nonce)nonce=1;
    }
    return nonce;
}
static uint64_t hash_bytes(uint64_t h,const void *p,size_t n) { const unsigned char *s=p; while(n--)h=(h^*s++)*UINT64_C(1099511628211); return h; }
// Bounded-memory directory scan. Revision covers actionable entries; hidden
// host metadata may change whenever USB reconnects. Pagination is stable
// only while this revision matches; no implicit refresh/retry is performed.
static cJSON *listing(const char *path,unsigned offset,const char **error)
{
    char base[FULL_CAP]; full(base,path);
    DIR *d=opendir(base); if(!d) { *error="folder_unavailable"; return NULL; }
    cJSON *out=cJSON_CreateObject(), *entries=cJSON_AddArrayToObject(out,"entries");
    unsigned count=0, scanned=0, hidden=0; uint64_t hash=UINT64_C(14695981039346656037);
    uint64_t nonce=boot_nonce(); hash=hash_bytes(hash,&nonce,sizeof(nonce));
    hash=hash_bytes(hash,path,strlen(path));
    struct dirent *e; errno=0;
    while((e=readdir(d))) {
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        if(++scanned>SCAN_LIMIT) { *error="folder_too_large"; goto fail; }
        if(!component_valid(e->d_name) || strlen(path)+strlen(e->d_name)+(*path?1:0)>=PATH_CAP) {
            hidden++; continue;
        }
        char child[FULL_CAP]; int len=snprintf(child,sizeof(child),"%s/%s",base,e->d_name);
        if(len<0 || (size_t)len>=sizeof(child)) { *error="path_too_long"; goto fail; }
        struct stat st; if(safe_stat(child,&st)) { *error="storage_error"; goto fail; }
        if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)) { hidden++; continue; }
        hash=hash_bytes(hash,e->d_name,strlen(e->d_name)+1);
        int64_t meta[]={st.st_size,st.st_mtime,st.st_mode}; hash=hash_bytes(hash,meta,sizeof(meta));
        if(count>=offset && count-offset<PAGE_SIZE) {
            cJSON *item=cJSON_CreateObject(); cJSON_AddStringToObject(item,"name",e->d_name);
            cJSON_AddStringToObject(item,"kind",S_ISDIR(st.st_mode)?"folder":"file");
            cJSON_AddNumberToObject(item,"size",S_ISREG(st.st_mode)?(double)st.st_size:0);
            cJSON_AddItemToArray(entries,item);
        }
        count++; errno=0;
    }
    if(errno) { *error="storage_error"; goto fail; }
    closedir(d);
    char revision[17]; snprintf(revision,sizeof(revision),"%016" PRIx64,hash);
    cJSON_AddStringToObject(out,"revision",revision); cJSON_AddStringToObject(out,"path",path);
    cJSON_AddNumberToObject(out,"total",count); cJSON_AddNumberToObject(out,"hidden",hidden);
    if(offset+PAGE_SIZE<count)cJSON_AddNumberToObject(out,"nextOffset",offset+PAGE_SIZE); else cJSON_AddNullToObject(out,"nextOffset");
    return out;
fail: closedir(d); cJSON_Delete(out); return NULL;
}
static bool subtree_fits(const char *source,const char *destination,unsigned depth,unsigned *budget)
{
    if(depth>8 || !(*budget)--)return false;
    char *paths=malloc(FULL_CAP+2*PATH_CAP); if(!paths)return false;
    char *a=paths, *b=a+FULL_CAP, *c=b+PATH_CAP; full(a,source);
    DIR *d=opendir(a); if(!d) { free(paths); return false; }
    bool ok=true; struct dirent *e; errno=0;
    while((e=readdir(d))) {
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        int nb=snprintf(b,PATH_CAP,"%s/%s",source,e->d_name), nc=snprintf(c,PATH_CAP,"%s/%s",destination,e->d_name);
        if(nb<0||nc<0||nb>=PATH_CAP||nc>=PATH_CAP||!local_path_valid(b,false)||!local_path_valid(c,false)) { ok=false; break; }
        full(a,b); struct stat st;
        if(safe_stat(a,&st)||(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode))) { ok=false; break; }
        if(S_ISDIR(st.st_mode)) { if(!subtree_fits(b,c,depth+1,budget)) { ok=false; break; } }
        else if(!(*budget)--) { ok=false; break; }
        errno=0;
    }
    if(errno)ok=false;
    closedir(d); free(paths); return ok;
}
static const char *str(const cJSON *r,const char *k) { const cJSON *v=cJSON_GetObjectItemCaseSensitive(r,k); return cJSON_IsString(v)?v->valuestring:NULL; }
cJSON *local_files_execute(const cJSON *r,const char **error)
{
    *error="invalid_request";
    const char *op=str(r,"op"), *path=str(r,"path"), *revision=str(r,"revision");
    if(!op || !local_path_valid(path,!strcmp(op,"list")) || !canonical(path,!strcmp(op,"mkdir")))return NULL;
    if(!strcmp(op,"list")) {
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(r,"offset");
        if(v && (!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>SCAN_LIMIT||v->valuedouble!=(unsigned)v->valuedouble))return NULL;
        unsigned offset=v?(unsigned)v->valuedouble:0;
        cJSON *out=listing(path,offset,error); if(!out)return NULL;
        if((offset&&!revision) || (revision&&strcmp(revision,str(out,"revision")))) { cJSON_Delete(out); *error="stale_listing"; return NULL; }
        return out;
    }
    if(strcmp(op,"mkdir")&&strcmp(op,"move")&&strcmp(op,"delete"))return NULL;
    if(!revision || strlen(revision)!=16)return NULL;
    char parent[PATH_CAP]; parent_of(path,parent);
    cJSON *before=listing(parent,0,error); if(!before)return NULL;
    bool matches=!strcmp(revision,str(before,"revision")); cJSON_Delete(before);
    if(!matches) { *error="stale_listing"; return NULL; }
    char source[FULL_CAP]; full(source,path);
    int result;
    if(!strcmp(op,"mkdir")) result=mkdir(source,0775);
    else if(!strcmp(op,"move")) {
        const char *dest=str(r,"destination");
        if(!local_path_valid(dest,false)||!canonical(dest,true))return NULL;
        size_t n=strlen(path);
        if(!strncasecmp(path,dest,n) && (dest[n]==0||dest[n]=='/')) { *error="invalid_destination"; return NULL; }
        char target[FULL_CAP]; full(target,dest); struct stat st;
        if(!safe_stat(target,&st)) { *error="already_exists"; return NULL; }
        if(errno!=ENOENT) { *error="storage_error"; return NULL; }
        struct stat source_stat;
        if(safe_stat(source,&source_stat)) { *error="storage_error"; return NULL; }
        unsigned budget=SCAN_LIMIT;
        if(S_ISDIR(source_stat.st_mode) && !subtree_fits(path,dest,1,&budget)) { *error="unsupported_folder_tree"; return NULL; }
        // Same FAT volume: rename moves the directory entry, not file bytes.
        result=rename(source,target);
    } else {
        struct stat st; if(safe_stat(source,&st)) { *error="not_found"; return NULL; }
        result=S_ISDIR(st.st_mode)?rmdir(source):unlink(source); // Never recursive.
    }
    if(result) { *error=(errno==EEXIST)?"already_exists":(errno==ENOTEMPTY)?"folder_not_empty":"storage_error"; return NULL; }
    cJSON *out=listing(parent,0,error);
    if(!out)*error="result_unknown";
    return out;
}
