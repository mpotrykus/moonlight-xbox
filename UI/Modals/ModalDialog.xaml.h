#pragma once
#include "UI\Modals\ModalDialog.g.h"
#include <collection.h>

namespace moonlight_xbox_dx {
    public ref class ModalDialog sealed {
    public:
        ModalDialog();
        void SetTitle(Platform::String^ title);
        void SetMessage(Platform::String^ message);
        Platform::String^ GetMessage();
        void ShowProgress(bool show);
    bool IsProgressShown();
    static Windows::UI::Xaml::Controls::ContentDialog^ ShowProgressDialog(Platform::String^ title, Platform::String^ message);
    // New API: returns an opaque token identifying the shown dialog. Returns 0 if no dialog was shown (guard prevented it).
    static uint64_t ShowProgressDialogToken(Platform::String^ title, Platform::String^ message);
    // Generic guard wrapper: shows only one ContentDialog at a time
    static Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Controls::ContentDialogResult>^ ShowOnceAsync(Windows::UI::Xaml::Controls::ContentDialog^ dialog);
        // Show a dialog where content is provided as XAML markup. The markup will be parsed on the UI thread with XamlReader::Load.
        // Returns an integer code indicating which action the user took. Mapping is dialog-specific but conventionally:
        // 0 = Cancel/None, 1 = Resume, 2 = Close, 3 = CloseAndStart, other values reserved.
        static Windows::Foundation::IAsyncOperation<int>^ ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup, Platform::String^ title, Platform::String^ primaryButtonText);
        // Overload: show XAML-only dialog. If title or primary button text are omitted, they will not be rendered.
        static Windows::Foundation::IAsyncOperation<int>^ ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup);
        // Overload allowing control of visibility for common in-content buttons inside provided XAML.
        static Windows::Foundation::IAsyncOperation<int>^ ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup, Platform::String^ title, Platform::String^ primaryButtonText, bool resumeVisible, bool closeVisible, bool closeAndStartVisible);
    static Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Controls::ContentDialogResult>^ ShowProgressDialogAsync(Platform::String^ title, Platform::String^ message);
    // Hide a dialog safely on the UI dispatcher. Use if you created or received
    // a dialog instance on a non-UI thread and need to hide it later from UI code.
    static void HideDialog(Windows::UI::Xaml::Controls::ContentDialog^ dialog);
    // Hide by token (no pointer semantics). Token 0 is no-op.
    static void HideDialogByToken(uint64_t token);

    private:
        static std::atomic<bool> s_globalDialogOpen;
    };
}