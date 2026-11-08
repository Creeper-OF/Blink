#include "Blink/Properties/PropertyReader.h"

#include <aclapi.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <sddl.h>
#include <shobjidl_core.h>

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shell32.lib")

namespace blink::properties
{
    namespace
    {
        struct ComApartment
        {
            HRESULT result{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
            ~ComApartment()
            {
                if (SUCCEEDED(result))
                {
                    CoUninitialize();
                }
            }
        };

        [[nodiscard]] std::wstring SidToString(PSID sid)
        {
            if (sid == nullptr || !IsValidSid(sid))
            {
                return L"invalid";
            }
            LPWSTR text = nullptr;
            if (!ConvertSidToStringSidW(sid, &text) || text == nullptr)
            {
                return L"unavailable";
            }
            std::wstring result(text);
            LocalFree(text);
            return result;
        }

        [[nodiscard]] std::wstring PropertyKeyName(const PROPERTYKEY& key)
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(PSGetNameFromPropertyKey(key, &name)) && name != nullptr)
            {
                std::wstring result(name);
                CoTaskMemFree(name);
                return result;
            }
            return L"PropertyId." + std::to_wstring(key.pid);
        }

        void ReadWindowsPropertyStore(const std::wstring& path, std::vector<PropertyValue>& result)
        {
            ComApartment apartment;
            if (FAILED(apartment.result))
            {
                return;
            }

            IPropertyStore* store = nullptr;
            const HRESULT open_result = SHGetPropertyStoreFromParsingName(
                path.c_str(),
                nullptr,
                GPS_DEFAULT | GPS_BESTEFFORT,
                IID_IPropertyStore,
                reinterpret_cast<void**>(&store));
            if (FAILED(open_result) || store == nullptr)
            {
                return;
            }

            DWORD count = 0;
            if (SUCCEEDED(store->GetCount(&count)))
            {
                for (DWORD index = 0; index < count; ++index)
                {
                    PROPERTYKEY key{};
                    if (FAILED(store->GetAt(index, &key)))
                    {
                        continue;
                    }

                    PROPVARIANT value;
                    PropVariantInit(&value);
                    const HRESULT get_result = store->GetValue(key, &value);
                    if (SUCCEEDED(get_result) && value.vt != VT_EMPTY)
                    {
                        PWSTR text = nullptr;
                        if (SUCCEEDED(PropVariantToStringAlloc(value, &text)) && text != nullptr)
                        {
                            result.push_back(PropertyValue{ PropertyKeyName(key), text });
                            CoTaskMemFree(text);
                        }
                    }
                    PropVariantClear(&value);
                }
            }
            store->Release();
        }

        void ReadSecurity(const std::wstring& path, SecuritySnapshot& security)
        {
            PSID owner = nullptr;
            PACL dacl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const DWORD result = GetNamedSecurityInfoW(
                path.c_str(),
                SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                &owner,
                nullptr,
                &dacl,
                nullptr,
                &descriptor);
            security.error = result;
            if (result != ERROR_SUCCESS || descriptor == nullptr)
            {
                return;
            }

            security.available = true;
            security.owner_sid = SidToString(owner);
            BOOL dacl_present = FALSE;
            BOOL dacl_defaulted = FALSE;
            if (GetSecurityDescriptorDacl(descriptor, &dacl_present, &dacl, &dacl_defaulted))
            {
                security.dacl_present = dacl_present != FALSE;
            }

            SECURITY_DESCRIPTOR_CONTROL control{};
            DWORD revision = 0;
            if (GetSecurityDescriptorControl(descriptor, &control, &revision))
            {
                security.dacl_protected = (control & SE_DACL_PROTECTED) != 0;
            }

            if (dacl != nullptr && security.dacl_present)
            {
                ACL_SIZE_INFORMATION size_info{};
                if (GetAclInformation(dacl, &size_info, sizeof(size_info), AclSizeInformation))
                {
                    for (DWORD index = 0; index < size_info.AceCount; ++index)
                    {
                        LPVOID raw_ace = nullptr;
                        if (!GetAce(dacl, index, &raw_ace) || raw_ace == nullptr)
                        {
                            continue;
                        }
                        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
                        AclEntry entry;
                        entry.type = header->AceType;
                        entry.flags = header->AceFlags;
                        entry.inherited = (header->AceFlags & INHERITED_ACE) != 0;

                        PSID sid = nullptr;
                        if (header->AceType == ACCESS_ALLOWED_ACE_TYPE)
                        {
                            const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
                            entry.mask = allowed->Mask;
                            sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&allowed->SidStart));
                        }
                        else if (header->AceType == ACCESS_DENIED_ACE_TYPE)
                        {
                            const auto* denied = static_cast<const ACCESS_DENIED_ACE*>(raw_ace);
                            entry.mask = denied->Mask;
                            sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&denied->SidStart));
                        }
                        entry.sid = SidToString(sid);
                        security.entries.push_back(std::move(entry));
                    }
                }
            }
            LocalFree(descriptor);
        }

        void ReadStreams(const std::wstring& path, std::vector<StreamInfo>& streams)
        {
            WIN32_FIND_STREAM_DATA data{};
            const HANDLE handle = FindFirstStreamW(path.c_str(), FindStreamInfoStandard, &data, 0);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return;
            }

            do
            {
                streams.push_back(StreamInfo{
                    data.cStreamName,
                    data.StreamSize.QuadPart < 0 ? 0 : static_cast<std::uint64_t>(data.StreamSize.QuadPart),
                    0 });
            } while (FindNextStreamW(handle, &data));
            FindClose(handle);
        }
    }

    FileProperties ReadFileProperties(const std::wstring& path, bool read_metadata, bool read_security)
    {
        FileProperties result;
        if (!blink::core::ReadEntryMetadata(path, result.entry))
        {
            result.error = GetLastError();
            return result;
        }

        const bool directory = result.entry.is_directory;
        blink::core::UniqueHandle handle = blink::core::OpenPathForMetadata(path, directory);
        if (!handle)
        {
            result.error = GetLastError();
            return result;
        }

        FILE_STANDARD_INFO standard{};
        if (GetFileInformationByHandleEx(handle.get(), FileStandardInfo, &standard, sizeof(standard)))
        {
            result.hard_link_count = standard.NumberOfLinks;
        }

        FILE_COMPRESSION_INFO compression{};
        if (GetFileInformationByHandleEx(handle.get(), FileCompressionInfo, &compression, sizeof(compression)))
        {
            result.compressed = compression.CompressionFormat != COMPRESSION_FORMAT_NONE;
            result.compression_format = compression.CompressionFormat;
        }

        ReadStreams(path, result.streams);
        if (read_metadata)
        {
            ReadWindowsPropertyStore(path, result.metadata);
        }
        if (read_security)
        {
            ReadSecurity(path, result.security);
        }

        result.available = true;
        result.error = ERROR_SUCCESS;
        return result;
    }

    std::future<FileProperties> ReadFilePropertiesAsync(std::wstring path, bool read_metadata, bool read_security)
    {
        return std::async(std::launch::async, [path = std::move(path), read_metadata, read_security] {
            return ReadFileProperties(path, read_metadata, read_security);
        });
    }
}
