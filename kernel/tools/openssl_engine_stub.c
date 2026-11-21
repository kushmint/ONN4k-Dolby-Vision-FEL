/* This host's OpenSSL has no ENGINE API, but the kernel's extract-cert and
 * sign-file helpers still link against it. Neither helper's ENGINE path is used
 * when building this out-of-tree module, so any call is a hard failure. */
#include <stdio.h>
#include <stdlib.h>

#define STUB(name) \
	void *name(void) { fprintf(stderr, #name ": ENGINE API unavailable\n"); abort(); }

STUB(ENGINE_load_builtin_engines)
STUB(ENGINE_by_id)
STUB(ENGINE_init)
STUB(ENGINE_ctrl_cmd_string)
STUB(ENGINE_ctrl_cmd)
STUB(ENGINE_load_private_key)
STUB(ENGINE_finish)
STUB(ENGINE_free)
