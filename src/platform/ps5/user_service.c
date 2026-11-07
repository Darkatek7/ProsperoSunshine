// SPDX-License-Identifier: GPL-3.0-only
#include "user_service.h"
#include <pthread.h>
#include <stdbool.h>

int sceUserServiceInitialize(void *);

// Something else in the process may have initialized it already: that counts as up.
#define ALREADY_INITIALIZED ((int)0x80960003)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool ready;

int ps_user_service_ready(void)
{
    pthread_mutex_lock(&lock);
    int rc = 0;
    if (!ready) {
        rc = sceUserServiceInitialize(NULL);
        if (rc == ALREADY_INITIALIZED) rc = 0;
        ready = rc == 0;
    }
    pthread_mutex_unlock(&lock);
    return rc;
}
