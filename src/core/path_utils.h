#pragma once

#include <string>
#include <Windows.h>

namespace SkyrimHT {

// Get the directory containing our DLL
std::string GetModuleDirectory();

// Get full path to a file in the same directory as our DLL
std::string GetModulePath(const char* filename);

// The folder our DLL is in, ending in a separator, read wide from the loader so
// a character the ANSI code page cannot hold survives. Empty on failure.
std::wstring GetModuleDirectoryW();

} // namespace SkyrimHT
