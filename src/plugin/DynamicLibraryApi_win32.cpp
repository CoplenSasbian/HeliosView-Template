#include "DynamicLibraryApi.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
struct library
{
    HMODULE handle{};
};

library_t OpenLibrary(const std::filesystem::path& p)
{
    HMODULE hmodule = LoadLibraryW(p.c_str());

    if (hmodule)
    {
        auto* lib = new library;
        lib->handle = hmodule;
        return lib;
    }
    return nullptr;
}

void ReleaseLibrary(library_t lib)
{
    if (!lib) return;
    if (lib->handle){
        FreeLibrary( lib->handle );
    }
    delete lib;
}

void* GetProcAddress(library_t lib, const char* name)
{
    if ( lib && lib->handle )
        return ::GetProcAddress(lib->handle, name);
    return nullptr;
}


#endif
