#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <foundation/logging/logging.h>
#include <foundation/memory/allocators.h>

int main(int argc, char* argv[])
{
    HMODULE runtime = LoadLibraryA("runtime.dll");

    auto entryPoint = reinterpret_cast<int(*)(int, char**)>(GetProcAddress(runtime, "win32_main"));
    
  
    return entryPoint(argc - 1, argv + 1);
}