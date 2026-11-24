// DXC's COM headers only (see vkDxc.h). One compiler + utils instance per thread: DXC instances are not
// meant to be shared between threads, and creating them per call would cost more than a small shader.
#include "vkDxc.h"

#include <dxc/dxcapi.h>

#include <atomic>
#include <cstring>

namespace xrvk::dxc
{
    namespace
    {
        std::wstring widen(const std::string &s)
        {
            // UTF-8 -> UTF-32 (wchar_t is 4 bytes here); a byte that does not start a valid sequence is
            // taken as Latin-1
            std::wstring w;
            w.reserve(s.size());
            size_t i = 0;
            while (i < s.size())
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const size_t n = (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
                uint32_t cp = n ? c & (0x3f >> n) : c;
                bool ok = n > 0 && i + n < s.size();
                for (size_t k = 1; ok && k <= n; ++k)
                {
                    const unsigned char d = static_cast<unsigned char>(s[i + k]);
                    ok = (d >> 6) == 2;
                    cp = (cp << 6) | (d & 0x3f);
                }
                if (ok)
                {
                    w.push_back(wchar_t(cp));
                    i += n + 1;
                }
                else
                {
                    w.push_back(wchar_t(c));
                    ++i;
                }
            }
            return w;
        }

        std::string narrow(const wchar_t *w)
        {
            std::string s;
            for (; w && *w; ++w)
            {
                const uint32_t c = uint32_t(*w);
                if (c < 0x80)
                    s.push_back(char(c));
                else if (c < 0x800)
                {
                    s.push_back(char(0xc0 | (c >> 6)));
                    s.push_back(char(0x80 | (c & 0x3f)));
                }
                else if (c < 0x10000)
                {
                    s.push_back(char(0xe0 | (c >> 12)));
                    s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
                    s.push_back(char(0x80 | (c & 0x3f)));
                }
                else
                {
                    s.push_back(char(0xf0 | (c >> 18)));
                    s.push_back(char(0x80 | ((c >> 12) & 0x3f)));
                    s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
                    s.push_back(char(0x80 | (c & 0x3f)));
                }
            }
            return s;
        }

        struct Instances
        {
            CComPtr<IDxcUtils> utils;
            CComPtr<IDxcCompiler3> compiler;
            HRESULT status = E_FAIL;
            Instances()
            {
                status = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
                if (SUCCEEDED(status))
                    status = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
            }
        };

        Instances &instances()
        {
            thread_local Instances i;
            return i;
        }

        // Lives on the stack for one Compile call; DXC only holds it during that call.
        class Includer final : public IDxcIncludeHandler
        {
        public:
            Includer(IDxcUtils *utils, const IncludeFn &fn) : m_utils(utils), m_fn(fn)
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
            {
                if (!out)
                    return E_POINTER;
                if (IsEqualIID(riid, __uuidof(IDxcIncludeHandler)) || IsEqualIID(riid, __uuidof(IUnknown)))
                {
                    AddRef();
                    *out = static_cast<IDxcIncludeHandler *>(this);
                    return S_OK;
                }
                *out = nullptr;
                return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++m_refs;
            }
            ULONG STDMETHODCALLTYPE Release() override
            {
                return --m_refs; // owned by the caller's stack frame
            }
            HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR filename, IDxcBlob **out) override
            {
                if (!out)
                    return E_POINTER;
                *out = nullptr;
                std::string name = narrow(filename);
                while (name.compare(0, 2, "./") == 0)
                    name.erase(0, 2);
                std::string text;
                if (!m_fn || !m_fn(name, text))
                    return E_FAIL;
                IDxcBlobEncoding *blob = nullptr;
                const HRESULT hr = m_utils->CreateBlob(text.data(), UINT32(text.size()), DXC_CP_ACP, &blob);
                if (FAILED(hr))
                    return hr;
                *out = blob;
                return S_OK;
            }

        private:
            IDxcUtils *m_utils;
            const IncludeFn &m_fn;
            std::atomic<ULONG> m_refs{1};
        };

        std::string utf8_output(IDxcResult *result, DXC_OUT_KIND kind)
        {
            CComPtr<IDxcBlobUtf8> text;
            if (result->HasOutput(kind) && SUCCEEDED(result->GetOutput(kind, IID_PPV_ARGS(&text), nullptr)) && text)
                return std::string(text->GetStringPointer(), text->GetStringLength());
            return {};
        }

        // Runs one Compile call; `result` is set when DXC produced a result object at all.
        bool run(const std::string &source, const std::vector<std::wstring> &wargs, const IncludeFn *include,
                 CComPtr<IDxcResult> &result, std::string &errors)
        {
            Instances &in = instances();
            if (FAILED(in.status))
            {
                errors = "DXC: DxcCreateInstance failed";
                return false;
            }
            std::vector<LPCWSTR> argv;
            for (const auto &a : wargs)
                argv.push_back(a.c_str());
            DxcBuffer buf{source.data(), source.size(), DXC_CP_ACP};
            const IncludeFn none;
            Includer includer(in.utils, include ? *include : none);
            const HRESULT hr = in.compiler->Compile(&buf, argv.data(), UINT32(argv.size()), &includer, IID_PPV_ARGS(&result));
            if (FAILED(hr) || !result)
            {
                errors = "DXC: Compile call failed";
                return false;
            }
            errors = utf8_output(result, DXC_OUT_ERRORS);
            HRESULT status = E_FAIL;
            result->GetStatus(&status);
            return SUCCEEDED(status);
        }
    } // namespace

    bool preprocess(const std::string &source, const std::vector<Define> &defines, const IncludeFn &include,
                    std::string &out, std::string &errors)
    {
        std::vector<std::wstring> args{L"-P", L"-HV", L"2018"};
        for (const auto &d : defines)
        {
            args.push_back(L"-D");
            args.push_back(widen(d.value.empty() ? d.name : d.name + "=" + d.value));
        }
        CComPtr<IDxcResult> result;
        if (!run(source, args, &include, result, errors))
            return false;
        out = utf8_output(result, DXC_OUT_HLSL);
        return true;
    }

    bool compile(const std::string &source, const std::vector<std::string> &args, std::vector<uint32_t> &spirv,
                 std::string &errors)
    {
        std::vector<std::wstring> wargs;
        for (const auto &a : args)
            wargs.push_back(widen(a));
        CComPtr<IDxcResult> result;
        if (!run(source, wargs, nullptr, result, errors))
            return false;
        CComPtr<IDxcBlob> object;
        if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object ||
            object->GetBufferSize() % 4 != 0)
        {
            errors += "DXC: no SPIR-V output";
            return false;
        }
        spirv.resize(object->GetBufferSize() / 4);
        memcpy(spirv.data(), object->GetBufferPointer(), object->GetBufferSize());
        return true;
    }
} // namespace xrvk::dxc
