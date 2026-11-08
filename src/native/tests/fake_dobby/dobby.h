#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int DobbyHook(void* address, void* fake_func, void** out_origin_func);
#ifdef __cplusplus
}
#endif
