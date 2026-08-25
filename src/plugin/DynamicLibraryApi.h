#pragma once

#include <filesystem>
struct library;

typedef library* library_t;


library_t OpenLibrary(const std::filesystem::path &p);

void ReleaseLibrary(library_t);

void* GetProcAddress(library_t, const char*);


template<class Func>
Func GetProcAddress(library_t lib, const char* name)
{
    return reinterpret_cast<Func>(GetProcAddress(lib, name));
}

