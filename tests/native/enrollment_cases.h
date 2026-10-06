// Production cloud.c enrollment with fake NVS/HTTPS, including power-loss windows.
static void enrollment_boot(void)
{
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_receipt, 0, sizeof(s_receipt));
    memset(&s_enrollment, 0, sizeof(s_enrollment));
    memset(mutexes, 0, sizeof(mutexes));
    mutex_count = 1; // Preserve the display subsystem's first mutex.
    assert(cloud_init() == ESP_OK);
}

static void enrollment_fresh(void)
{
    have_config = have_receipt = have_enrollment = false;
    fail_save = fail_commit = ambiguous_commit = fail_config_write = fail_clear_write = false;
    incomplete_response = false;
    gate = update_pending = false;
    pending_kind = 0;
    random_calls = enrollment_requests = 0;
    first_candidate_token[0] = 0;
    api_status = 200;
    clock_us = 1000000;
    free(reply);
    reply = strdup("{\"protocolVersion\":1,\"sessionId\":\"enroll-1\",\"deviceId\":\"link-50787d2c5a1c\",\"downloadHost\":\"files.example.com\",\"claimed\":true,\"ownershipGeneration\":1}");
    enrollment_mode = true;
    enrollment_boot();
}

static void enrollment_status_private(void)
{
    cJSON *status = cloud_status();
    char *json = cJSON_PrintUnformatted(status);
    assert(json && !strstr(json, "bbbbbbbb") && !strstr(json, "cdcdcdcd") &&
           !strstr(json, "ticket") && !strstr(json, "token") && !strstr(json, "https://"));
    assert(strstr(json, "enrollmentProtocolVersion"));
    free(json);
    cJSON_Delete(status);
}

static void check_enrollment(void)
{
    const char *serial = "50787D2C5A1C";
    cJSON *request = cJSON_Parse("{\"apiBaseUrl\":\"https://api.example.com/\",\"downloadHost\":\"files.example.com\",\"sessionId\":\"enroll-1\",\"ticket\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}");
    enrollment_fresh();
    assert(!cloud_enroll_error_code(CLOUD_ENROLL_OK));
    assert(!strcmp(cloud_enroll_error_code(CLOUD_ENROLL_BUSY), "busy"));
    assert(!strcmp(cloud_enroll_error_code(CLOUD_ENROLL_ALREADY_CONFIGURED), "already_configured"));
    assert(!strcmp(cloud_enroll_error_code(CLOUD_ENROLL_SERVICE_MISMATCH), "service_mismatch"));
    assert(!strcmp(cloud_enroll_error_code(CLOUD_ENROLL_STORAGE_ERROR), "storage_error"));
    assert(!strcmp(cloud_enroll_error_code(CLOUD_ENROLL_INVALID_REQUEST), "invalid_enrollment"));
    assert(strstr(cloud_enroll_error_message(CLOUD_ENROLL_STORAGE_ERROR), "restart"));
    assert(xSemaphoreTake(s_session, 0) == pdTRUE);
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_BUSY);
    xSemaphoreGive(s_session);
    assert(cloud_enroll(request, "not-a-serial") == CLOUD_ENROLL_INVALID_REQUEST);
    cJSON_ReplaceItemInObject(request, "apiBaseUrl", cJSON_CreateString("http://api.example.com/"));
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_INVALID_REQUEST && !have_enrollment);
    cJSON_ReplaceItemInObject(request, "apiBaseUrl", cJSON_CreateString("https://api.example.com/"));
    gate = true;
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_BUSY && !have_enrollment);
    gate = false;
    update_pending = true;
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_BUSY && !have_enrollment);
    update_pending = false;
    assert(operation_begin()); settings_fail_commit = settings_commit_ambiguous = true;
    assert(display_settings_save(display_settings_defaults()) == ESP_FAIL);
    settings_fail_commit = settings_commit_ambiguous = false; operation_end();
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_STORAGE_ERROR && !have_enrollment);
    assert(display_settings_init() == ESP_OK);
    // Reset the settings fixture before the existing settings revision tests.
    settings_size = settings_legacy_size = 0;
    assert(display_settings_init() == ESP_OK);
    fail_save = true;
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_STORAGE_ERROR && s_enrollment_fault && !have_enrollment);
    fail_save = false;
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_STORAGE_ERROR);
    enrollment_boot();
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK && have_enrollment && !have_config);
    assert(!random_calls && !saved_enrollment.candidate.token[0]);
    cJSON *factory = cJSON_Duplicate(request, true);
    cJSON_AddStringToObject(factory, "deviceId", "factory-device");
    cJSON_AddStringToObject(factory, "token", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(cloud_configure(factory) == ESP_ERR_INVALID_STATE && !have_config);
    cJSON_Delete(factory);
    int64_t deadline = s_enrollment_deadline;
    ++clock_us;
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK && s_enrollment_deadline == deadline);
    enrollment_status_private();
    cJSON_ReplaceItemInObject(request, "apiBaseUrl", cJSON_CreateString("https://other.example.com/"));
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_SERVICE_MISMATCH);
    cJSON_ReplaceItemInObject(request, "apiBaseUrl", cJSON_CreateString("https://api.example.com/"));
    cJSON_ReplaceItemInObject(request, "downloadHost", cJSON_CreateString("other.example.com"));
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_SERVICE_MISMATCH);
    cJSON_ReplaceItemInObject(request, "downloadHost", cJSON_CreateString("files.example.com"));
    cJSON_ReplaceItemInObject(request, "ticket", cJSON_CreateString("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_INVALID_REQUEST);
    cJSON_ReplaceItemInObject(request, "ticket", cJSON_CreateString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    // Busy status is an immutable copy, not a peek at unsynchronized live state.
    assert(xSemaphoreTake(s_session, 0) == pdTRUE);
    strcpy(s_enrollment.session_id, "not-published");
    cJSON *cached = cloud_status();
    assert(!strcmp(string(cached, "enrollmentSessionId"), "enroll-1"));
    cJSON_Delete(cached);
    strcpy(s_enrollment.session_id, "enroll-1");
    xSemaphoreGive(s_session);
    // Power cut before credential generation and then a lost HTTPS response.
    enrollment_boot();
    incomplete_response = true;
    inspect_busy_status = true; busy_status_reads = 0;
    assert(xSemaphoreTake(s_session, 0) == pdTRUE);
    assert(enroll_cloud() == 0 && !have_config && random_calls == 1 && enrollment_requests == 1);
    publish_status(); xSemaphoreGive(s_session);
    inspect_busy_status = false;
    assert(busy_status_reads >= 4);
    cJSON *fresh = cloud_status();
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(fresh, "cached")));
    assert(!strcmp(string(fresh, "enrollmentState"), "connection_error"));
    cJSON_Delete(fresh);
    enrollment_status_private();
    enrollment_boot();
    incomplete_response = false;
    assert(enroll_cloud() == 1 && have_config && s_config.enabled && !enrollment_pending());
    assert(random_calls == 1 && enrollment_requests == 2 && !strcmp(s_enrollment_state, "linked"));
    assert(!saved_enrollment.ticket[0] && !saved_enrollment.candidate.token[0]);
    enrollment_status_private();
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_ALREADY_CONFIGURED); // Cannot take over configured device.
    // Cloud disable/reset during enrollment preserves the journal and key.
    enrollment_fresh();
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    api_status = 503;
    assert(enroll_cloud() == 0 && s_enrollment.active);
    assert(cloud_disable_for_reset() == ESP_OK && !s_enrollment.active);
    enrollment_boot();
    assert(!s_enrollment.active && !strcmp(s_enrollment_state, "retry_required"));
    unsigned requests_before = enrollment_requests;
    assert(enroll_cloud() == 60 && enrollment_requests == requests_before);
    assert(cloud_set_enabled(true) == ESP_ERR_INVALID_STATE);
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    api_status = 410;
    assert(enroll_cloud() == 60 && !s_enrollment.active && !strcmp(s_enrollment_state, "expired"));
    // Expired ticket can be replaced without losing/revealing the key.
    cJSON_ReplaceItemInObject(request, "sessionId", cJSON_CreateString("enroll-2"));
    assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    cJSON *response = cJSON_Parse(reply);
    cJSON_ReplaceItemInObject(response, "sessionId", cJSON_CreateString("enroll-2"));
    free(reply); reply = cJSON_PrintUnformatted(response); cJSON_Delete(response);
    api_status = 200;
    assert(enroll_cloud() == 1 && random_calls == 1);
    cJSON_ReplaceItemInObject(request, "sessionId", cJSON_CreateString("enroll-1"));
    // Redirects, rejected auth, rate limits and server errors never configure.
    const int statuses[] = {201,204,301,302,400,401,403,404,409,410,429,500};
    for (unsigned i = 0; i < sizeof(statuses)/sizeof(*statuses); ++i) {
        enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
        api_status = statuses[i]; (void)enroll_cloud();
        assert(!have_config && enrollment_pending());
        enrollment_status_private();
    }
    // Malformed or mismatched acknowledgements cannot commit configuration.
    const char *fields[] = {"protocolVersion", "sessionId", "deviceId", "downloadHost", "claimed", "ownershipGeneration"};
    for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); ++i) {
        enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
        response = cJSON_Parse(reply);
        cJSON_ReplaceItemInObject(response, fields[i], cJSON_CreateString("wrong"));
        free(reply); reply = cJSON_PrintUnformatted(response); cJSON_Delete(response);
        (void)enroll_cloud(); assert(!have_config && enrollment_pending());
    }
    enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    clock_us = s_enrollment_deadline;
    assert(enroll_cloud() == 60 && !s_enrollment.active && !enrollment_requests);
    // Failed/ambiguous credential journal commits must not send anything.
    for (int ambiguous = 0; ambiguous < 2; ++ambiguous) {
        enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
        fail_commit = true; ambiguous_commit = ambiguous;
        (void)enroll_cloud();
        assert(s_enrollment_fault && !enrollment_requests && !have_config);
        fail_commit = ambiguous_commit = false;
        (void)enroll_cloud(); assert(!enrollment_requests);
        enrollment_boot(); assert(enroll_cloud() == 1);
        assert(random_calls == (ambiguous ? 1u : 2u));
    }
    // Backend accepted, but config write failed. Retry after reboot uses same key.
    enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    fail_config_write = true; (void)enroll_cloud();
    assert(s_enrollment_fault && !have_config && saved_enrollment.candidate.token[0]);
    fail_config_write = false; enrollment_boot();
    assert(enroll_cloud() == 1 && random_calls == 1);
    // Ambiguous config commit: it reached flash despite the reported failure.
    enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    api_status = 503; (void)enroll_cloud(); // Durably generate credential first.
    api_status = 200; fail_commit = ambiguous_commit = true;
    (void)enroll_cloud();
    assert(have_config && s_enrollment_fault);
    fail_commit = ambiguous_commit = false;
    enrollment_boot();
    assert(!enrollment_pending() && config_valid(&s_config));
    assert(enrollment_requests == 2 && random_calls == 1);
    // Power cut after config commit, before journal cleanup: recover without HTTP.
    enrollment_fresh(); assert(cloud_enroll(request, serial) == CLOUD_ENROLL_OK);
    fail_clear_write = true; (void)enroll_cloud();
    assert(s_enrollment_fault && have_config && enrollment_pending());
    fail_clear_write = false; enrollment_boot();
    assert(!enrollment_pending() && config_valid(&s_config) && !strcmp(s_enrollment_state, "linked"));
    assert(enrollment_requests == 1);
    // A conflicting persisted identity is not overwritten or transmitted.
    saved_enrollment = pending_enrollment;
    saved_enrollment.schema = 1;
    saved_enrollment.active = true;
    saved_enrollment.candidate = saved_config;
    strcpy(saved_enrollment.candidate.device_id, "other-device");
    strcpy(saved_enrollment.session_id, "enroll-1");
    memset(saved_enrollment.ticket, 'b', 64); saved_enrollment.ticket[64] = 0;
    assert(cloud_init() == ESP_ERR_INVALID_STATE);
    enrollment_fresh();
    enrollment_mode = false;
    free(reply); reply = NULL;
    cJSON_Delete(request);
    puts("cloud enrollment tests passed (private credentials, pinned origin, expiry, retries, power loss, storage faults)");
}
