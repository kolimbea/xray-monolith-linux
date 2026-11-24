// In-process DXC (libdxcompiler.so) for the Vulkan renderer: HLSL preprocessing and HLSL -> SPIR-V.
// The interface uses std types only: the implementation includes DXC's own COM headers, which must not
// meet the engine's Win32/D3D stand-ins in one translation unit.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xrvk::dxc
{
    struct Define
    {
        std::string name, value;
    };

    // Returns false when the file cannot be opened. `name` is the include as DXC resolved it, with any
    // leading "./" removed.
    using IncludeFn = std::function<bool(const std::string &name, std::string &contents)>;

    // Runs the preprocessor (dxc -P). `errors` receives the diagnostics (also on success: warnings).
    bool preprocess(const std::string &source, const std::vector<Define> &defines, const IncludeFn &include,
                    std::string &out, std::string &errors);

    // Compiles a self-contained (already preprocessed) source with the given dxc command-line arguments.
    bool compile(const std::string &source, const std::vector<std::string> &args, std::vector<uint32_t> &spirv,
                 std::string &errors);
} // namespace xrvk::dxc
