#include "pch.h"
#include "XamlHelper.h"
#include "Utils.hpp"
#include <robuffer.h>

using namespace concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Platform;
using namespace Windows::ApplicationModel;

namespace moonlight_xbox_dx {
    namespace XamlHelper {
        concurrency::task<Platform::String^> LoadXamlFileAsStringAsync(Platform::String^ relativePath) {
            // Expect input like L"/Pages/HelpDialog.xaml" or L"ms-appx:///Pages/HelpDialog.xaml"
            std::wstring orig = (relativePath == nullptr) ? std::wstring() : std::wstring(relativePath->Data());
            std::wstring uriW;
            if (orig.rfind(L"ms-appx:///", 0) == 0) {
                uriW = orig;
            } else {
                if (!orig.empty() && orig[0] == L'/') {
                    uriW = std::wstring(L"ms-appx:///") + orig.substr(1);
                } else {
                    uriW = std::wstring(L"ms-appx:///") + orig;
                }
            }
            auto uriStr = ref new Platform::String(uriW.c_str());

            // Diagnostic log: attempted ms-appx URI
            try {
                auto uriLog = Utils::PlatformStringToStdString(uriStr);
                Utils::Logf("XamlHelper: attempting ms-appx URI %s\n", uriLog.c_str());
            } catch(...) {}

            // Try to get the StorageFile. If GetFileFromApplicationUriAsync fails (file missing), return an error string containing the message.
            return create_task(StorageFile::GetFileFromApplicationUriAsync(ref new Windows::Foundation::Uri(uriStr)))
                .then([orig, uriStr](task<StorageFile^> getFileTask) -> task<Platform::String^> {
                    try {
                        auto file = getFileTask.get(); // may throw if not found
                        return create_task(FileIO::ReadTextAsync(file));
                    } catch (Platform::Exception^ ex) {
                        try {
                            auto msg = Utils::PlatformStringToStdString(ex->Message);
                            Utils::Logf("XamlHelper: GetFileFromApplicationUriAsync failed: HRESULT=0x%08X message=%s\n", ex->HResult, msg.c_str());
                        } catch(...) {}

                        // Try fallback: load from package installed location using relative path
                        try {
                            StorageFolder^ installed = Package::Current->InstalledLocation;
                            std::wstring path = orig;
                            if (!path.empty() && path[0] == L'/') path = path.substr(1);
                            auto pathStr = ref new Platform::String(path.c_str());
                            try {
                                auto s = Utils::PlatformStringToStdString(pathStr);
                                Utils::Logf("XamlHelper: attempting fallback path %s\n", s.c_str());
                            } catch(...) {}

                            auto fallbackTask = create_task(installed->GetFileAsync(pathStr)).then([](StorageFile^ file) -> task<Platform::String^> {
                                return create_task(FileIO::ReadTextAsync(file));
                            });
                            return fallbackTask;
                        } catch (Platform::Exception^ ex2) {
                            try {
                                auto msg2 = Utils::PlatformStringToStdString(ex2->Message);
                                Utils::Logf("XamlHelper: fallback GetFile failed: HRESULT=0x%08X message=%s\n", ex2->HResult, msg2.c_str());
                            } catch(...) {}

                            // Return an error-marked string so callers can show a friendly dialog.
                            std::wstring prefix = L"__ERROR__:";
                            std::wstring msg = ex2->Message == nullptr ? std::wstring(L"Unknown error") : std::wstring(ex2->Message->Data());
                            std::wstring out = prefix + msg;
                            auto outStr = ref new Platform::String(out.c_str());
                            return create_task([outStr]() -> Platform::String^ { return outStr; });
                        } catch (...) {
                            auto outStr = ref new Platform::String(L"__ERROR__:Unknown error");
                            return create_task([outStr]() -> Platform::String^ { return outStr; });
                        }
                    } catch (...) {
                        auto outStr = ref new Platform::String(L"__ERROR__:Unknown error");
                        return create_task([outStr]() -> Platform::String^ { return outStr; });
                    }
                }).then([](task<Platform::String^> t) -> Platform::String^ {
                    try {
                        return t.get();
                    } catch (Platform::Exception^ ex) {
                        try {
                            auto msg = Utils::PlatformStringToStdString(ex->Message);
                            Utils::Logf("XamlHelper: ReadTextAsync failed: HRESULT=0x%08X message=%s\n", ex->HResult, msg.c_str());
                        } catch(...) {}
                        // If reading text failed, return an error string with message
                        std::wstring prefix = L"__ERROR__:";
                        std::wstring msg = ex->Message == nullptr ? std::wstring(L"Unknown error") : std::wstring(ex->Message->Data());
                        std::wstring out = prefix + msg;
                        return ref new Platform::String(out.c_str());
                    } catch (...) {
                        return ref new Platform::String(L"__ERROR__:Unknown error");
                    }
                });
        }
    }
}
