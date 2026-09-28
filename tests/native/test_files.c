#include "link_files.h"
#include "storage.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];
static uint64_t available = 128 * 1024 * 1024;
static int rename_count, fail_rename_at;
static const char *abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
const char *storage_base_path(void)
{
    return root;
}
esp_err_t esp_vfs_fat_info(const char *p, uint64_t *total, uint64_t *free)
{
    (void)p;
    *total = 128 * 1024 * 1024;
    *free = available;
    return ESP_OK;
}
int link_test_rename(const char *a, const char *b)
{
    if (++rename_count == fail_rename_at) {
        errno = EIO;
        return -1;
    }
    return rename(a, b);
}
static void write_file(const char *name, const char *text)
{
    FILE *f = fopen(name, "wb");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(!fclose(f));
}
static void expect(const char *name, const char *text)
{
    char buf[256] = {0};
    FILE *f = fopen(name, "rb");
    assert(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    assert(n == strlen(text) && !strcmp(buf, text));
}
static void absent(const char *name)
{
    assert(access(name, F_OK) != 0);
}
static void clean(void)
{
    fail_rename_at = rename_count = 0;
    remove("rose.pes");
    remove("~link.commit");
    remove("~link.backup");
    remove("~link.upload");
}
static link_file_write_t begin(void)
{
    link_file_write_t w;
    assert(link_file_begin(&w, "rose.pes", 3) == ESP_OK);
    return w;
}

int main(void)
{
    snprintf(root, sizeof(root), "/tmp/ember-link-files-XXXXXX");
    assert(mkdtemp(root));
    assert(!chdir(root));
    link_file_write_t w;
    write_file("rose.pes", "old");
    w = begin();
    assert(link_file_write(&w, "abc", 3) == ESP_OK);
    assert(link_file_finish(&w, abc_hash) == ESP_OK);
    expect("rose.pes", "abc");
    absent("~link.commit");
    absent("~link.backup");
    absent("~link.upload");
    char hash[65];
    assert(link_file_hash("rose.pes", hash) == ESP_OK && !strcmp(hash, abc_hash));

    w = begin();
    assert(link_file_write(&w, "bad", 3) == ESP_OK);
    assert(link_file_finish(&w, abc_hash) == ESP_ERR_INVALID_CRC);
    expect("rose.pes", "abc");
    w = begin();
    assert(link_file_write(&w, "a", 1) == ESP_OK);
    assert(link_file_finish(&w, abc_hash) == ESP_ERR_INVALID_SIZE);
    expect("rose.pes", "abc");
    w = begin();
    assert(link_file_write(&w, "abcd", 4) == ESP_ERR_INVALID_SIZE);
    link_file_abort(&w);
    available = 2;
    assert(link_file_begin(&w, "rose.pes", 3) == ESP_ERR_NO_MEM);
    available = 128 * 1024 * 1024;
    assert(link_file_begin(&w, "../rose.pes", 3) == ESP_ERR_INVALID_ARG);

    // Fail each rename: original must survive, never remove-before-replace.
    for (int fail = 1; fail <= 2; fail++) {
        clean();
        write_file("rose.pes", "old");
        w = begin();
        assert(link_file_write(&w, "abc", 3) == ESP_OK);
        fail_rename_at = fail;
        assert(link_file_finish(&w, abc_hash) != ESP_OK);
        fail_rename_at = 0;
        assert(link_files_recover() == ESP_OK);
        expect("rose.pes", "old");
    }
    // Reboot before moving old file.
    clean();
    write_file("rose.pes", "old");
    write_file("~link.upload", "abc");
    write_file("~link.commit", "rose.pes");
    assert(link_files_recover() == ESP_OK);
    expect("rose.pes", "old");
    absent("~link.upload");
    // Reboot between old->backup and new->destination.
    clean();
    write_file("~link.backup", "old");
    write_file("~link.upload", "abc");
    write_file("~link.commit", "rose.pes");
    assert(link_files_recover() == ESP_OK);
    expect("rose.pes", "old");
    absent("~link.backup");
    // Reboot after install, before cleanup.
    clean();
    write_file("rose.pes", "abc");
    write_file("~link.backup", "old");
    write_file("~link.commit", "rose.pes");
    assert(link_files_recover() == ESP_OK);
    expect("rose.pes", "abc");
    absent("~link.backup");
    // Corrupt recovery metadata: preserve every file and fail closed.
    clean();
    write_file("~link.backup", "old");
    write_file("~link.commit", "../bad.pes");
    assert(link_files_recover() == ESP_ERR_INVALID_STATE);
    expect("~link.backup", "old");
    clean();
    write_file("~link.backup", "old");
    assert(link_files_recover() == ESP_ERR_INVALID_STATE);
    expect("~link.backup", "old");
    // Longest valid filename must also recover.
    clean();
    char long_name[128];
    memset(long_name, 'a', 123);
    strcpy(long_name + 123, ".pes");
    write_file("~link.commit", long_name);
    write_file("~link.backup", "old");
    assert(link_files_recover() == ESP_OK);
    expect(long_name, "old");
    remove(long_name);
    clean();
    assert(!chdir("/"));
    assert(!rmdir(root));
    puts("file transaction tests passed (integrity, space, faults, reboot recovery)");
    return 0;
}
