#include "local_files.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char root[256];
static bool fail_rename;
int local_test_rename(const char *a,const char *b) { if(fail_rename) { errno=EIO; return -1; } return rename(a,b); }
const char *storage_base_path(void) { return root; }
static const char *str(cJSON *r,const char *key) { return cJSON_GetObjectItem(r,key)->valuestring; }
static cJSON *call(const char *op,const char *path,const char *dest,const char *revision,int offset,const char **error) {
    cJSON *r=cJSON_CreateObject(); cJSON_AddStringToObject(r,"op",op); cJSON_AddStringToObject(r,"path",path);
    if(dest)cJSON_AddStringToObject(r,"destination",dest);
    if(revision)cJSON_AddStringToObject(r,"revision",revision);
    if(offset)cJSON_AddNumberToObject(r,"offset",offset);
    cJSON *out=local_files_execute(r,error); cJSON_Delete(r); return out;
}
static void put(const char *p,const char *data) { FILE *f=fopen(p,"wb"); assert(f); assert(fwrite(data,1,strlen(data),f)==strlen(data)); assert(!fclose(f)); }
static void revision(char out[17],const char *path) { const char *error; cJSON *r=call("list",path,NULL,NULL,0,&error); assert(r); strcpy(out,str(r,"revision")); cJSON_Delete(r); }
int main(void) {
    snprintf(root,sizeof(root),"/tmp/link-browser-XXXXXX"); assert(mkdtemp(root)); assert(!chdir(root));
    const char *bad[]={"../a","/a","a/","a//b","a/../b","a/./b","CON","foo/NUL.pes","~link.upload","a/~LINK.UPLOAD","a\\b","x:stream","x.","a/START HERE.html","a/b/c/d/e/f/g/h/i"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!local_path_valid(bad[i],false));
    assert(local_path_valid("Projects/Flower 1.PES",false)); assert(local_path_valid("",true)); assert(!local_path_valid("",false));
    put("rose.pes","original"); put("~link.commit","internal");
    const char *error; char rev[17]; revision(rev,"");
    // USB host metadata churn must not invalidate an actionable snapshot.
    put(".metadata", "host"); assert(!symlink("/tmp", "HiddenLink"));
    char unchanged[17]; revision(unchanged, ""); assert(!strcmp(rev, unchanged));
    assert(!unlink(".metadata")); assert(!unlink("HiddenLink"));
    revision(unchanged, ""); assert(!strcmp(rev, unchanged));
    cJSON *r=call("mkdir","Flowers",NULL,rev,0,&error); assert(r); cJSON_Delete(r);
    // A saved directory revision cannot mutate a later directory state.
    assert(!call("delete","rose.pes",NULL,rev,0,&error)); assert(!strcmp(error,"stale_listing")); assert(!access("rose.pes",F_OK));
    revision(rev,""); r=call("move","rose.pes","Flowers/rose.pes",rev,0,&error); assert(r); cJSON_Delete(r);
    assert(access("rose.pes",F_OK)); assert(!access("Flowers/rose.pes",F_OK));
    // Replay cannot accidentally mutate a replacement entry.
    assert(!call("move","rose.pes","Flowers/rose.pes",rev,0,&error));
    revision(rev,""); assert(!call("delete","Flowers",NULL,rev,0,&error)); assert(!access("Flowers/rose.pes",F_OK));
    revision(rev,"Flowers"); r=call("move","Flowers/rose.pes","Flowers/new.pes",rev,0,&error); assert(r); cJSON_Delete(r);
    put("Flowers/other.pes","other"); revision(rev,"Flowers");
    assert(!call("move","Flowers/new.pes","Flowers/other.pes",rev,0,&error)); assert(!strcmp(error,"already_exists"));
    assert(!call("move","Flowers/new.pes","Flowers/OTHER.PES",rev,0,&error));
    assert(!call("move","Flowers/new.pes","flowers/new.pes",rev,0,&error));
    revision(rev,""); assert(!call("move","Flowers","Flowers/Nested",rev,0,&error));
    // A failed directory-entry update is uncertain, never retried or cleaned up.
    fail_rename=true; revision(rev,"Flowers");
    assert(!call("move","Flowers/new.pes","Flowers/maybe.pes",rev,0,&error));
    assert(!strcmp(error,"storage_error")); assert(!access("Flowers/new.pes",F_OK)); assert(access("Flowers/maybe.pes",F_OK));
    fail_rename=false;
    // Prevent a move from making descendants inaccessible beyond the depth cap.
    assert(!mkdir("a",0775)); assert(!mkdir("a/b",0775)); assert(!mkdir("a/b/c",0775));
    assert(!mkdir("a/b/c/d",0775)); assert(!mkdir("a/b/c/d/e",0775)); assert(!mkdir("a/b/c/d/e/f",0775)); assert(!mkdir("a/b/c/d/e/f/g",0775));
    revision(rev,""); assert(!call("move","Flowers","a/b/c/d/e/f/g/Flowers",rev,0,&error));
    assert(!strcmp(error,"unsupported_folder_tree"));
    assert(!rmdir("a/b/c/d/e/f/g")); assert(!rmdir("a/b/c/d/e/f")); assert(!rmdir("a/b/c/d/e"));
    assert(!rmdir("a/b/c/d")); assert(!rmdir("a/b/c")); assert(!rmdir("a/b")); assert(!rmdir("a"));
    revision(rev,"");
    // Directory moves preserve descendants; deletion never recurses.
    r=call("move","Flowers","Projects",rev,0,&error); assert(r); cJSON_Delete(r); assert(!access("Projects/new.pes",F_OK));
    // Host tests reject symlinks even in intermediate path components.
    assert(!symlink("/tmp","Escape")); assert(!call("list","Escape",NULL,NULL,0,&error));
    assert(!call("mkdir","Escape/new",NULL,rev,0,&error));
    assert(!unlink("Escape"));
    revision(rev,"Projects"); r=call("delete","Projects/new.pes",NULL,rev,0,&error); assert(r); cJSON_Delete(r);
    revision(rev,"Projects"); r=call("delete","Projects/other.pes",NULL,rev,0,&error); assert(r); cJSON_Delete(r);
    revision(rev,""); r=call("delete","Projects",NULL,rev,0,&error); assert(r); cJSON_Delete(r);
    for(int i=0;i<75;i++) { char name[24]; snprintf(name,sizeof(name),"file%03d.pes",i); put(name,"test"); }
    r=call("list","",NULL,NULL,0,&error); assert(r); assert(cJSON_GetArraySize(cJSON_GetObjectItem(r,"entries"))==32);
    assert(cJSON_GetObjectItem(r,"total")->valueint==75); assert(cJSON_GetObjectItem(r,"hidden")->valueint==1);
    strcpy(rev,str(r,"revision")); cJSON_Delete(r);
    r=call("list","",NULL,rev,32,&error); assert(r); assert(cJSON_GetArraySize(cJSON_GetObjectItem(r,"entries"))==32); cJSON_Delete(r);
    r=call("list","",NULL,rev,64,&error); assert(r); assert(cJSON_GetArraySize(cJSON_GetObjectItem(r,"entries"))==11); cJSON_Delete(r);
    assert(!call("list","",NULL,NULL,32,&error));
    put("file000.pes","changed length"); assert(!call("list","",NULL,rev,32,&error)); assert(!strcmp(error,"stale_listing"));
    for(int i=0;i<75;i++) { char name[24]; snprintf(name,sizeof(name),"file%03d.pes",i); assert(!unlink(name)); }
    assert(!unlink("~link.commit")); assert(!chdir("/tmp")); assert(!rmdir(root));
    puts("local file operations: paths, collisions, stale state, folders, pagination passed");
}
