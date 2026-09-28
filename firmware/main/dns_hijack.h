// Captive-portal DNS: while the setup AP is active, answer every DNS query
// with the dongle's own address (192.168.4.1). Joining devices run a
// connectivity check against a known URL; hijacked DNS + the HTTP
// catch-all redirect make that check land on the setup page, which pops
// the OS's "sign in to network" sheet automatically.
#pragma once

void dns_hijack_start(void);
void dns_hijack_stop(void);
