#include "link_protocol.h"

#include <math.h>
#include <string.h>
#include <strings.h>

static bool ascii_alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool link_filename_valid(const char *s)
{
    if (!s)
        return false;
    size_t n = strlen(s);
    if (!n || n >= LINK_MAX_FILENAME || s[0] == '.' || s[0] == '~' || s[n - 1] == '.' ||
        s[n - 1] == ' ')
        return false;
    const char *dot = strrchr(s, '.');
    if (!dot || dot == s || !dot[1])
        return false;
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126 || strchr("\\/:*?\"<>|", s[i]))
            return false;
    }
    // Windows/FAT reserved device names remain reserved even with an extension.
    size_t stem = strcspn(s, ".");
    if ((stem == 3 && (!strncasecmp(s, "CON", 3) || !strncasecmp(s, "PRN", 3) ||
                       !strncasecmp(s, "AUX", 3) || !strncasecmp(s, "NUL", 3))) ||
        (stem == 4 && (!strncasecmp(s, "COM", 3) || !strncasecmp(s, "LPT", 3)) && s[3] >= '1' &&
         s[3] <= '9'))
        return false;
    return strcasecmp(s, "START HERE.html") != 0 &&
           strcasecmp(s, "START HERE - Get Ember Bridge.html") != 0;
}

bool link_id_valid(const char *s)
{
    if (!s || !*s || strlen(s) >= LINK_MAX_ID)
        return false;
    for (; *s; ++s)
        if (!ascii_alnum(*s) && *s != '-' && *s != '_')
            return false;
    return true;
}

bool link_sha256_valid(const char *s)
{
    if (!s || strlen(s) != 64)
        return false;
    for (; *s; ++s)
        if (!(*s >= '0' && *s <= '9') && !(*s >= 'a' && *s <= 'f'))
            return false;
    return true;
}

bool link_token_valid(const char *s)
{
    if (!s || strlen(s) != 64)
        return false;
    return link_sha256_valid(s); // 256-bit lowercase hex, no header injection.
}

bool link_host_valid(const char *s)
{
    if (!s || !*s || strlen(s) > 253)
        return false;
    size_t label = 0;
    char previous = 0;
    for (; *s; previous = *s++) {
        if (*s == '.') {
            if (!label || previous == '-')
                return false;
            label = 0;
        } else {
            if (!ascii_alnum(*s) && *s != '-')
                return false;
            if (!label && *s == '-')
                return false;
            if (++label > 63)
                return false;
        }
    }
    return label && previous != '-';
}

bool link_https_url_valid(const char *url, const char *host)
{
    if (!url || strlen(url) >= LINK_MAX_URL || strncmp(url, "https://", 8))
        return false;
    for (const char *p = url; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p > 126 || *p == '\\' || *p == '#')
            return false;
    const char *end = strchr(url + 8, '/');
    if (!end)
        return false;
    size_t n = (size_t)(end - (url + 8));
    if (!n || n > 253)
        return false;
    char actual[254];
    memcpy(actual, url + 8, n);
    actual[n] = 0;
    if (!link_host_valid(actual))
        return false; // rejects userinfo, ports, IPv6.
    return !host || (link_host_valid(host) && !strcasecmp(actual, host));
}

bool link_api_base_valid(const char *url)
{
    if (!link_https_url_valid(url, NULL) || strlen(url) > 240)
        return false;
    const char *path = strchr(url + 8, '/');
    return path && path[1] == 0; // origin only, trailing slash required.
}

bool link_integer_valid(double n, uint64_t min, uint64_t max)
{
    return isfinite(n) && n >= (double)min && n <= (double)max && floor(n) == n;
}
