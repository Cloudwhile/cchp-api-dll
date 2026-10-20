#include <windows.h>
// No network, allocation, thread creation or joining under the loader lock.
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
