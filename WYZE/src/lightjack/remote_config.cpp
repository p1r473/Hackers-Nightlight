/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#ifdef LIGHTJACK_REMOTE_SSH_POC
#include "lightjack/remote_policy.h"
/* This source-only build has no credentials and cannot connect. A future
 * provisioning implementation can replace this weak provider, keeping secrets
 * outside version control. No desktop SSH key is copied into this project. */
extern "C" __attribute__((weak, noinline))
const lightjack_remote_config *lightjack_remote_get_config(void)
{
    static const lightjack_remote_config missing = {};
    return &missing;
}
#endif
