/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#ifdef LIGHTJACK_REMOTE_SSH_POC
#include "lightjack/remote_policy.h"
#ifdef LIGHTJACK_REMOTE_CONFIGURED
#include "lightjack_remote_config_private.h"
#endif
/* Only an explicitly selected external private bundle enables configuration.
 * The ordinary source build has no credentials and cannot connect. */
extern "C" __attribute__((weak, noinline))
const lightjack_remote_config *lightjack_remote_get_config(void)
{
#ifdef LIGHTJACK_REMOTE_CONFIGURED
    return &lightjack_remote_provisioned;
#else
    static const lightjack_remote_config missing = {};
    return &missing;
#endif
}
#endif
