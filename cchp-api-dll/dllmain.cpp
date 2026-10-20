#include <windows.h>

// No network, thread creation or teardown under the Windows loader lock.
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
