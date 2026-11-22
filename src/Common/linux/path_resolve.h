// Windows-style path resolution for the Linux port.
//
// The engine builds paths with '\' and often lower-cases them (file registry keys), while mods ship
// files in arbitrary case. Every OS-facing call in the compat layer passes its path through
// xr_resolve_path(), which
//   1. turns '\' into '/' and collapses repeated separators;
//   2. returns at once if that path exists (the common case: one stat());
//   3. otherwise matches each component case-insensitively against a cached directory index.
// For a path that does not exist yet (file creation), the existing parent directories are resolved
// and the remaining components are kept as given.
#pragma once
#include <string>

std::string xr_resolve_path(const char* path);

// Drops cached listings (all, or the directory containing `path`) after the engine creates or
// removes entries, so later lookups see them.
void xr_resolve_invalidate(const char* path = nullptr);
