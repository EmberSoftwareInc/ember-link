// Pairing + bearer-token auth for the HTTP API.
//
// A headless dongle can't pop an "allow this computer?" dialog, so pairing
// is gated on proof of physical presence instead: new clients may pair for
// a few minutes after power-on (unplugging the dongle IS the consent
// gesture), after a short tap of the BOOT button, and any time the dongle
// is in WiFi setup mode. Outside the window, POST /api/pair is refused.
//
// Tokens are 128-bit random hex strings persisted in NVS; clients present
// them as `Authorization: Bearer <token>` on every other API call.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define AUTH_TOKEN_LEN 32  // hex chars
#define AUTH_NAME_MAX 32
#define AUTH_MAX_CLIENTS 8

typedef struct {
    char token[AUTH_TOKEN_LEN + 1];
    char name[AUTH_NAME_MAX + 1];
} auth_client_t;

esp_err_t auth_init(void);

// True while new clients may pair (see header comment for when).
bool auth_pairing_open(void);
// Re-open the pairing window (button tap).
void auth_open_window(void);

// Mint, persist, and return a token for a new client.
// ESP_ERR_INVALID_STATE: pairing window closed.
// ESP_ERR_NO_MEM: client table full (factory reset clears it).
esp_err_t auth_pair(const char *name, char out_token[AUTH_TOKEN_LEN + 1]);

// Does the request carry a valid `Authorization: Bearer <token>`?
bool auth_check(httpd_req_t *req);

// Revoke one token (a client un-pairing itself). ESP_ERR_NOT_FOUND if absent.
esp_err_t auth_revoke(const char *token);

// Paired-client names (not tokens), for GET /api/pair.
size_t auth_list_names(char out[][AUTH_NAME_MAX + 1], size_t max);

size_t auth_client_count(void);

// Forget every paired client (factory reset).
void auth_clear_all(void);
