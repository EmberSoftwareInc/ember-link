// Real OTA/update/migration state machines, with fault-injected platform I/O.
#include "../../firmware/main/ota.c"
#include "../../firmware/main/firmware_update.c"
#include "../../firmware/main/cloud_settings.c"
#include <assert.h>
#include <stdlib.h>

static esp_partition_t slots[] = {{"ota_0", 3*1024*1024}, {"ota_1", 3*1024*1024}};
static esp_app_desc_t running_desc = {.version="0.3.0-dev"};
static unsigned running, boot, writes, restarts, rollbacks;
static bool gate, verify_fail, activate_fail, pending, confirm_fail, network_fail, save_fail;
static int http_status = 200;
static unsigned char binary[512];
static char binary_hash[65];
static update_record_t disk_update;
static bool have_update;
static char settings[256];
static size_t settings_len;
static unsigned legacy_writes, save_count, fail_save_at;
static void (*guard_fn)(void *);

bool operation_begin(void) { if (gate) return false; gate = true; return true; }
void operation_end(void) { assert(gate); gate = false; }
const esp_app_desc_t *esp_app_get_description(void) { return &running_desc; }
const esp_partition_t *esp_ota_get_running_partition(void) { return &slots[running]; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) { (void)p; return &slots[1-running]; }
const esp_partition_t *esp_partition_find_first(int t, int s, const char *n) { (void)t; (void)s; (void)n; return NULL; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *s) { (void)p; *s=pending?1:0; return ESP_OK; }
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t n, esp_ota_handle_t *h) { (void)p; (void)n; *h=1; return ESP_OK; }
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *p, size_t n) { (void)h; (void)p; writes+=(unsigned)n; return ESP_OK; }
esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; return verify_fail ? ESP_FAIL : ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { if (activate_fail) return ESP_FAIL; boot=p==&slots[1]; return ESP_OK; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { if (confirm_fail) return ESP_FAIL; pending=false; return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { ++rollbacks; return ESP_OK; }
int xTaskCreate(void (*f)(void *), const char *n, unsigned s, void *a, unsigned p, void *h) { (void)n; (void)s; (void)a; (void)p; (void)h; guard_fn=f; return pdPASS; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void vTaskDelete(void *p) { (void)p; }
void esp_restart(void) { ++restarts; }
uint32_t esp_get_free_heap_size(void) { return 100000; }
uint32_t esp_get_minimum_free_heap_size(void) { return 90000; }
int esp_reset_reason(void) { return 1; }
int64_t esp_timer_get_time(void) { return 1000000; }
esp_err_t esp_secure_boot_get_signature_blocks_for_running_app(bool digest, esp_image_sig_public_key_digests_t *keys) {
    assert(digest); keys->num_digests=1; memset(keys->key_digests[0], 0xaa, 32); return ESP_OK;
}
esp_err_t esp_crt_bundle_attach(void *p) { (void)p; return ESP_OK; }
struct test_http { size_t offset; };
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(!strncmp(cfg->url,"https://files.example/",22)); assert(cfg->disable_auto_redirect && cfg->crt_bundle_attach);
    return calloc(1,sizeof(struct test_http));
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int n) { (void)h; assert(!n); return network_fail?ESP_FAIL:ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) { (void)h; return sizeof(binary); }
int esp_http_client_get_status_code(esp_http_client_handle_t h) { (void)h; return http_status; }
int esp_http_client_read(esp_http_client_handle_t h, char *out, int n) {
    size_t take=sizeof(binary)-h->offset; if(take>(size_t)n)take=n; if(take>31)take=31;
    memcpy(out,binary+h->offset,take); h->offset+=take; return (int)take;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { return h->offset==sizeof(binary); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { free(h); return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) { assert(!strcmp(name,"link_cloud")); (void)mode; *h=1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n) {
    (void)h; ++save_count; if(save_fail || (fail_save_at && save_count == fail_save_at))return ESP_FAIL;
    if(!strcmp(key,"update_v1")) { assert(n==sizeof(disk_update)); memcpy(&disk_update,p,n); have_update=true; }
    else if(!strcmp(key,"settings_v2")) { assert(n<=sizeof(settings)); memcpy(settings,p,n); settings_len=n; }
    else { ++legacy_writes; assert(false); }
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n) {
    (void)h;
    if(!strcmp(key,"update_v1")) { if(!have_update)return ESP_ERR_NVS_NOT_FOUND; assert(*n>=sizeof(disk_update)); memcpy(p,&disk_update,sizeof(disk_update)); *n=sizeof(disk_update); }
    else if(!strcmp(key,"settings_v2")) { if(!settings_len)return ESP_ERR_NVS_NOT_FOUND; assert(*n>=settings_len); memcpy(p,settings,settings_len); *n=settings_len; }
    else assert(false);
    return ESP_OK;
}
static void reset(void) {
    gate=s_in_progress=s_prepared=pending=verify_fail=activate_fail=network_fail=save_fail=have_update=false;
    save_count=fail_save_at=0;
    s_header_checked=false; running=boot=writes=restarts=0; http_status=200;
    strcpy(running_desc.version,"0.3.0-dev"); memset(&s_update,0,sizeof(s_update)); memset(&disk_update,0,sizeof(disk_update));
}
static cJSON *offer(void) {
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"updateId","u1"); cJSON_AddStringToObject(o,"attemptId","a1");
    cJSON_AddStringToObject(o,"releaseId","r1"); cJSON_AddStringToObject(o,"targetVersion","0.4.0");
    cJSON_AddStringToObject(o,"boardId",LINK_BOARD_ID); cJSON_AddStringToObject(o,"layoutId",LINK_LAYOUT_ID);
    cJSON_AddStringToObject(o,"signingKeyId","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    cJSON_AddStringToObject(o,"sha256",binary_hash); cJSON_AddStringToObject(o,"downloadUrl","https://files.example/fw.bin");
    cJSON_AddNumberToObject(o,"ownershipGeneration",2); cJSON_AddNumberToObject(o,"size",sizeof(binary));
    cJSON_AddNumberToObject(o,"expiresAt",time(NULL)+600); return o;
}
static void run(cJSON *o) { firmware_update_run(o,2,"files.example"); }
int main(void) {
    esp_image_header_t header={.magic=ESP_IMAGE_HEADER_MAGIC,.chip_id=ESP_CHIP_ID_ESP32S3};
    esp_app_desc_t desc={.magic_word=ESP_APP_DESC_MAGIC_WORD,.project_name="ember-link",.version="0.4.0"};
    memcpy(binary,&header,sizeof(header)); memcpy(binary+sizeof(header)+sizeof(esp_image_segment_header_t),&desc,sizeof(desc));
    psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT; unsigned char digest[32]; size_t n;
    assert(psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS); psa_hash_update(&hash,binary,sizeof(binary)); psa_hash_finish(&hash,digest,32,&n);
    for(unsigned i=0;i<32;i++)snprintf(binary_hash+2*i,3,"%02x",digest[i]);
    reset(); save_fail=true; assert(cloud_settings_migrate()!=ESP_OK); assert(!settings_len);
    save_fail=false; assert(cloud_settings_migrate()==ESP_OK); assert(cloud_settings_migrate()==ESP_OK); assert(!legacy_writes);
    strcpy(settings,"{\"schema\":3,\"transport\":\"mqtt\"}"); settings_len=strlen(settings)+1;
    assert(cloud_settings_migrate()==ESP_ERR_INVALID_STATE); assert(strstr(settings,"mqtt"));
    reset(); assert(ota_begin(0)==ESP_ERR_INVALID_SIZE); assert(!gate);
    assert(ota_begin(sizeof(binary))==ESP_OK); assert(ota_write(binary,sizeof(binary)-1)==ESP_OK);
    assert(ota_finish()!=ESP_OK && !gate && !boot); // truncated image never boots
    reset(); assert(ota_begin(sizeof(binary))==ESP_OK); assert(ota_write(binary,sizeof(binary)+1)==ESP_ERR_INVALID_SIZE); ota_abort();
    reset(); cJSON *o=offer(); run(o); assert(restarts==1 && boot==1 && gate && !strcmp(disk_update.state,"rebooting"));
    // Simulate a reboot into the new slot, but do not report success before health confirmation.
    running=1; strcpy(running_desc.version,"0.4.0"); pending=true; gate=false;
    assert(firmware_update_init()==ESP_OK); cJSON *poll=cJSON_CreateObject(); firmware_update_add_poll(poll);
    assert(!strcmp(s_update.state,"rebooting")); cJSON_Delete(poll);
    assert(ota_start_boot_guard()==ESP_OK); assert(guard_fn); confirm_fail=true; ota_confirm(); assert(pending); guard_fn(NULL); assert(rollbacks==1);
    confirm_fail=false; ota_confirm(); assert(!pending); poll=cJSON_CreateObject(); firmware_update_add_poll(poll); cJSON_Delete(poll);
    assert(!strcmp(s_update.state,"installed") && firmware_update_pending());
    cJSON *ack=cJSON_CreateObject(); cJSON_AddStringToObject(ack,"updateId","u1"); cJSON_AddStringToObject(ack,"attemptId","a1"); cJSON_AddStringToObject(ack,"state","installed");
    save_fail=true; firmware_update_ack(ack); assert(firmware_update_pending()); save_fail=false; firmware_update_ack(ack); assert(!firmware_update_pending());
    unsigned previous=writes; run(o); assert(writes==previous); cJSON_Delete(ack);
    // Rollback retains the journal and reports failure from the old slot.
    running=0; strcpy(running_desc.version,"0.3.0-dev"); strcpy(disk_update.state,"rebooting"); disk_update.acknowledged=false;
    assert(firmware_update_init()==ESP_OK); poll=cJSON_CreateObject(); firmware_update_add_poll(poll); cJSON_Delete(poll); assert(!strcmp(s_update.state,"rolled_back"));
    strcpy(disk_update.state,"downloading"); assert(firmware_update_init()==ESP_OK); assert(!strcmp(s_update.state,"failed"));
    for(int fault=0;fault<5;fault++) {
        reset(); verify_fail=fault==0; network_fail=fault==1; activate_fail=fault==2; save_fail=fault==3; http_status=fault==4?302:200;
        run(o); assert(!restarts && !boot && !gate); assert(save_fail || !strcmp(s_update.state,"failed"));
    }
    reset(); fail_save_at=2; run(o); assert(!restarts && !boot && !gate && !strcmp(s_update.state,"failed"));
    reset(); cJSON_ReplaceItemInObject(o,"sha256",cJSON_CreateString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")); run(o);
    assert(!boot && !restarts && !strcmp(s_update.error,"checksum_mismatch"));
    reset(); cJSON_ReplaceItemInObject(o,"targetVersion",cJSON_CreateString("0.5.0")); run(o); assert(!restarts && !boot);
    reset(); cJSON_ReplaceItemInObject(o,"boardId",cJSON_CreateString("wrong-board")); run(o); assert(!writes && !have_update);
    // Local compatibility reads must not expose or settle the cloud journal.
    strcpy(s_update.state,"rebooting"); unsigned saved_writes=writes;
    cJSON *local=cJSON_CreateObject(); firmware_update_add_capabilities(local);
    assert(cJSON_GetObjectItemCaseSensitive(local,"capabilities"));
    assert(!cJSON_GetObjectItemCaseSensitive(local,"firmwareReceipt"));
    assert(!cJSON_GetObjectItemCaseSensitive(local,"diagnostics"));
    assert(!strcmp(s_update.state,"rebooting") && writes==saved_writes);
    cJSON_Delete(local);
    cJSON_Delete(o); puts("OTA, cloud update recovery, and additive settings migration tests passed");
    return 0;
}
