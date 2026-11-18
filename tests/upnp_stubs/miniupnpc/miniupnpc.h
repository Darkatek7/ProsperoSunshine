// SPDX-License-Identifier: GPL-3.0-only
// Test-only replacement for the external interface.
#pragma once
struct UPNPDev { UPNPDev *pNext; char *descURL; };
struct UPNPUrls { char *controlURL; };
struct IGDdatas { struct { char servicetype[128]; } first; };
UPNPDev *upnpDiscoverDevices(const char *const *, int, const char *, const char *, int, int, unsigned char, int *, int);
int UPNP_GetIGDFromUrl(const char *, UPNPUrls *, IGDdatas *, char *, int);
void FreeUPNPUrls(UPNPUrls *);
void freeUPNPDevlist(UPNPDev *);
