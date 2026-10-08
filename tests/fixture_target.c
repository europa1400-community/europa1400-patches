/* Test target module: stands in for server.dll in the tests (no game files needed). */
#include <windows.h>

__declspec(dllexport) __declspec(noinline) int __cdecl target_add(int a, int b)
{
    volatile int sum = a;
    sum += b;
    return sum;
}

__declspec(dllexport) __declspec(noinline) int __cdecl target_mul(int a, int b)
{
    volatile int product = a;
    product *= b;
    return product;
}

__declspec(dllexport) DWORD __cdecl target_ticks(void)
{
    return GetTickCount();
}
