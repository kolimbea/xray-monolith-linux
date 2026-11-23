#!/usr/bin/env python3
"""Print the C/C++ sources a .vcxproj builds for Release|x64, one absolute path per line.
Skips items excluded for that configuration and Borland/BlackBox (Windows-only) files.
Usage: vcxproj_sources.py <project.vcxproj> [exclude-substring ...]"""
import os
import re
import sys

if sys.argv[1] == "--defines":
    # Release|x64 preprocessor definitions minus Windows/MSVC-build ones.
    text = open(sys.argv[2], encoding="utf-8", errors="replace").read()
    m = re.search(r"<ItemDefinitionGroup Condition=\"'\$\(Configuration\)\|\$\(Platform\)'=='Release\|x64'\">(.*?)</ItemDefinitionGroup>", text, re.S)
    d = re.search(r"<PreprocessorDefinitions>(.*?)</PreprocessorDefinitions>", m.group(1) if m else "", re.S)
    drop = {"WIN32", "_WINDOWS", "_USRDLL", "_SECURE_SCL=0", "MSVC", "NDEBUG", "_WIN32", "%(PreprocessorDefinitions)", ""}
    print("\n".join(x for x in (d.group(1).split(";") if d else []) if x not in drop))
    sys.exit(0)

proj = sys.argv[1]
skip = [s.lower() for s in sys.argv[2:]] + ["borland", "blackbox/"]
base = os.path.dirname(os.path.abspath(proj))
text = open(proj, encoding="utf-8", errors="replace").read()
for m in re.finditer(r'<ClCompile Include="([^"]+)"\s*(/>|>(.*?)</ClCompile>)', text, re.S):
    body = m.group(3) or ""
    if re.search(r"ExcludedFromBuild Condition=\"'\$\(Configuration\)\|\$\(Platform\)'=='Release\|x64'\">true", body):
        continue
    rel = m.group(1).replace("\\", "/")
    if any(s in rel.lower() for s in skip):
        continue
    path = os.path.normpath(os.path.join(base, rel))
    if not os.path.exists(path):
        # vcxproj paths are case-insensitive on Windows; find the real spelling
        d, n = os.path.split(path)
        hits = [f for f in os.listdir(d) if f.lower() == n.lower()] if os.path.isdir(d) else []
        if not hits:
            sys.exit(f"missing source {rel} in {proj}")
        path = os.path.join(d, hits[0])
    print(path)
