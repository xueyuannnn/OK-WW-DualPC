// Deliberately mimics a previously built DLL. Loading must reject it before
// calling its manifest or factory across an incompatible object layout.
#include <cstdlib>
extern "C" __declspec(dllexport) const char *okww_team_manifest_v1() { std::abort(); }
extern "C" __declspec(dllexport) void *okww_team_create_v1(void *) { std::abort(); }
