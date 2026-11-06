#include "runtime.h"

/* Linkage is intentionally unresolved: nm proves whether the generated-call text survives preprocessing. */
void RunSelectedGuestProbes(Ctx* context) {
    (void)context;  /* Ordinary preprocessing removes both uses; generated functions otherwise use c themselves. */
    RecompGuestFunctionEnter(context, 0x00838b40u);
    RecompGuestFunctionExit(context, 0x00838b40u);
}
