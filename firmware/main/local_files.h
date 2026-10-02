#pragma once
#include "cJSON.h"
#include <stdbool.h>
// Shared filesystem operations. Caller holds operation gate AND APP storage.
// No cloud/HTTP dependencies. Paths are relative to storage_base_path().
bool local_path_valid(const char *path, bool root);
// Returns a JSON result, or NULL with a stable error code. Never retries writes.
cJSON *local_files_execute(const cJSON *request, const char **error);
