// SPDX-License-Identifier: GPL-3.0-only
// Test-only replacement for the external interface.
#pragma once
#define UPNPCOMMAND_SUCCESS 0
int UPNP_GetExternalIPAddress(const char *, const char *, char *);
int UPNP_GetSpecificPortMappingEntry(const char *, const char *, const char *, const char *, const char *, char *, char *, char *, char *, char *);
int UPNP_GetGenericPortMappingEntry(const char *, const char *, const char *, char *, char *, char *, char *, char *, char *, char *, char *);
int UPNP_AddPortMapping(const char *, const char *, const char *, const char *, const char *, const char *, const char *, const char *, const char *);
int UPNP_DeletePortMapping(const char *, const char *, const char *, const char *, const char *);
