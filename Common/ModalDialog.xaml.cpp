#include "pch.h"
#include "ModalDialog.xaml.h"
#include "Utils.hpp"
#include <atomic>
#include <ppltasks.h>
#include <collection.h>
#include <agile.h>
#include <map>
#include <mutex>
#include <memory>

using namespace moonlight_xbox_dx;
using namespace concurrency;
using namespace Windows::UI::Xaml::Media;

ModalDialog::ModalDialog() {
    InitializeComponent();
}

void ModalDialog::SetTitle(Platform::String^ title) {
    if (this->TitleText != nullptr && title != nullptr) this->TitleText->Text = title;
}

void ModalDialog::SetMessage(Platform::String^ message) {
    if (this->MessageText != nullptr && message != nullptr) this->MessageText->Text = message;
}

Platform::String^ ModalDialog::GetMessage() {
    if (this->MessageText != nullptr) return this->MessageText->Text;
    return nullptr;
}

void ModalDialog::ShowProgress(bool show) {
    if (this->LoadingGif == nullptr) return;
    this->LoadingGif->Visibility = show ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
}

bool ModalDialog::IsProgressShown() {
    if (this->LoadingGif == nullptr) return false;
    return this->LoadingGif->Visibility == Windows::UI::Xaml::Visibility::Visible;
}

// Small wrapper used to publish the UI-owned clone asynchronously. We insert an
// entry before dispatching the UI handler so HideDialog can attach a continuation
// if it runs before the UI handler has created the clone.
struct DialogEntry {
    Windows::UI::Xaml::Controls::ContentDialog^ uiDialog;
    concurrency::task_completion_event<void> ready;
    uint64_t id;
    DialogEntry() : uiDialog(nullptr) {}
};

static std::map<void*, std::shared_ptr<DialogEntry>> s_dialogClones;
static std::mutex s_dialogClonesMutex;
static std::atomic<uint64_t> s_dialogEntryCounter{1};
static std::map<uint64_t, std::shared_ptr<DialogEntry>> s_dialogByToken;
static std::mutex s_dialogByTokenMutex;

Windows::UI::Xaml::Controls::ContentDialog^ ModalDialog::ShowProgressDialog(Platform::String^ title, Platform::String^ message)
{
    auto dlg = ref new ::moonlight_xbox_dx::ModalDialog();
    dlg->SetTitle(title);
    dlg->SetMessage(message);
    dlg->ShowProgress(true);
    // Use the internal guard
    ModalDialog::ShowOnceAsync(dlg);
    moonlight_xbox_dx::Utils::Logf("ShowProgressDialog: created original dialog %p\n", (void*)dlg);

    // Verify that ShowOnceAsync published an entry for this dialog. If it did not
    // (because another dialog was already open and the guard prevented showing),
    // return nullptr so callers do not attempt to Hide an untracked dialog.
    bool found = false;
    try {
        std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
        found = (s_dialogClones.find((void*)dlg) != s_dialogClones.end());
    } catch(...) { found = false; }

    if (!found) {
        moonlight_xbox_dx::Utils::Logf("ShowProgressDialog: ShowOnceAsync did not publish entry for %p — returning nullptr\n", (void*)dlg);
        return nullptr;
    }

    return reinterpret_cast<Windows::UI::Xaml::Controls::ContentDialog^>(dlg);
}

uint64_t ModalDialog::ShowProgressDialogToken(Platform::String^ title, Platform::String^ message)
{
    auto dlg = ref new ::moonlight_xbox_dx::ModalDialog();
    if (title != nullptr && title->Length() > 0) dlg->Title = title;
    if (message != nullptr && message->Length() > 0) dlg->SetMessage(message);
    dlg->ShowProgress(true);
    moonlight_xbox_dx::Utils::Logf("ShowProgressDialogToken: created original dialog %p\n", (void*)dlg);

    auto preEntry = std::make_shared<DialogEntry>();
    preEntry->id = s_dialogEntryCounter.fetch_add(1);
    try {
        std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
        s_dialogClones[(void*)dlg] = preEntry;
    } catch(...) {}
    moonlight_xbox_dx::Utils::Logf("ShowProgressDialogToken: published entry id=%llu for %p\n", (unsigned long long)preEntry->id, (void*)dlg);

    // Dispatch to UI thread to create the UI-owned clone and show it. This mirrors
    // the inner handler used by ShowOnceAsync but skips the global guard logic.
    try {
        auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
        if (dispatcher == nullptr) {
            try {
                auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView;
                if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher;
            } catch(...) { dispatcher = nullptr; }
        }

        if (dispatcher != nullptr) {
            auto entry = preEntry;
            auto handler = ref new Windows::UI::Core::DispatchedHandler([dlg, entry]() mutable {
                try {
                    Windows::UI::Xaml::Controls::ContentDialog^ uiDialog = nullptr;
                    auto srcModal = dynamic_cast<::moonlight_xbox_dx::ModalDialog^>(dlg);
                    if (srcModal != nullptr) {
                        auto newModal = ref new ::moonlight_xbox_dx::ModalDialog();
                        try { auto tstr = dynamic_cast<Platform::String^>(srcModal->Title); if (tstr != nullptr && tstr->Length() > 0) newModal->Title = tstr; } catch(...) {}
                        try { auto msg = srcModal->GetMessage(); if (msg != nullptr) newModal->SetMessage(msg); } catch(...) {}
                        try { auto pstr = dynamic_cast<Platform::String^>(srcModal->PrimaryButtonText); if (pstr != nullptr && pstr->Length() > 0) newModal->PrimaryButtonText = pstr; } catch(...) {}
                        try { auto sstr = dynamic_cast<Platform::String^>(srcModal->SecondaryButtonText); if (sstr != nullptr && sstr->Length() > 0) newModal->SecondaryButtonText = sstr; } catch(...) {}
                        try { auto cstr = dynamic_cast<Platform::String^>(srcModal->CloseButtonText); if (cstr != nullptr && cstr->Length() > 0) newModal->CloseButtonText = cstr; } catch(...) {}
                        try { if (srcModal->IsProgressShown()) newModal->ShowProgress(true); } catch(...) {}
                        uiDialog = reinterpret_cast<Windows::UI::Xaml::Controls::ContentDialog^>(newModal);
                    } else {
                        auto newDlg = ref new Windows::UI::Xaml::Controls::ContentDialog();
                        try { newDlg->Title = dlg->Title; } catch(...) {}
                        try {
                            auto textContent = dynamic_cast<Windows::UI::Xaml::Controls::TextBlock^>(dlg->Content);
                            if (textContent != nullptr) {
                                auto tb = ref new Windows::UI::Xaml::Controls::TextBlock();
                                tb->Text = textContent->Text;
                                newDlg->Content = tb;
                            } else {
                                auto boxed = dynamic_cast<Platform::String^>(dlg->Content);
                                if (boxed != nullptr) {
                                    auto tb = ref new Windows::UI::Xaml::Controls::TextBlock();
                                    tb->Text = boxed;
                                    newDlg->Content = tb;
                                }
                            }
                        } catch(...) {}
                        try { newDlg->PrimaryButtonText = dlg->PrimaryButtonText; } catch(...) {}
                        try { newDlg->SecondaryButtonText = dlg->SecondaryButtonText; } catch(...) {}
                        try { newDlg->CloseButtonText = dlg->CloseButtonText; } catch(...) {}
                        try { newDlg->IsSecondaryButtonEnabled = dlg->IsSecondaryButtonEnabled; } catch(...) {}
                        uiDialog = newDlg;
                    }

                    if (uiDialog == nullptr) return;

                    try { entry->uiDialog = uiDialog; } catch(...) {}
                    try { entry->ready.set(); } catch(...) {}
                    try {
                        std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                        s_dialogClones[(void*)uiDialog] = entry;
                    } catch(...) {}
                    moonlight_xbox_dx::Utils::Logf("ShowProgressDialogToken: created UI clone %p for original %p (id=%llu)\n", (void*)uiDialog, (void*)dlg, (unsigned long long)entry->id);

                    auto op = uiDialog->ShowAsync();
                    create_task(op).then([dlg, entry](Windows::UI::Xaml::Controls::ContentDialogResult r) mutable {
                        try {
                            std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                            try { s_dialogClones.erase((void*)dlg); moonlight_xbox_dx::Utils::Logf("ShowProgressDialogToken: erased mapping for original %p (id=%llu)\n", (void*)dlg, (unsigned long long)entry->id); } catch(...) {}
                            try { if (entry && entry->uiDialog != nullptr) { s_dialogClones.erase((void*)entry->uiDialog); moonlight_xbox_dx::Utils::Logf("ShowProgressDialogToken: erased mapping for ui clone %p (id=%llu)\n", (void*)entry->uiDialog, (unsigned long long)entry->id); } } catch(...) {}
                        } catch(...) {}
                    });
                } catch(...) {}
            });

            (void)dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, handler);
        } else {
            // If no dispatcher, we can't create a UI clone. Clean up the pre-published mapping.
            try { std::lock_guard<std::mutex> lock(s_dialogClonesMutex); s_dialogClones.erase((void*)dlg); } catch(...) {}
            moonlight_xbox_dx::Utils::Log("ShowProgressDialogToken: no UI dispatcher available, aborting\n");
            return 0;
        }
    } catch(...) {}

    uint64_t id = preEntry->id;

    // Publish token mapping for callers who will use HideDialogByToken.
    try {
        std::lock_guard<std::mutex> lock(s_dialogByTokenMutex);
        auto entry = s_dialogClones[(void*)dlg];
        if (entry) s_dialogByToken[entry->id] = entry;
    } catch(...) {}

    return id;
}

void ModalDialog::HideDialogByToken(uint64_t token)
{
    try {
        moonlight_xbox_dx::Utils::Logf("HideDialogByToken called for token=%llu\n", (unsigned long long)token);
        if (token == 0) return; // no-op

        std::shared_ptr<DialogEntry> entry;
        {
            std::lock_guard<std::mutex> lock(s_dialogByTokenMutex);
            auto it = s_dialogByToken.find(token);
            if (it != s_dialogByToken.end()) entry = it->second;
        }

        if (!entry) {
            moonlight_xbox_dx::Utils::Logf("HideDialogByToken: no entry for token=%llu\n", (unsigned long long)token);
            return;
        }

        // If the uiDialog already exists, hide it. Otherwise attach a continuation to hide once ready.
        if (entry->uiDialog != nullptr) {
            auto dlg = entry->uiDialog;
            auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
            if (dispatcher == nullptr) {
                try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) {}
            }
            if (dispatcher != nullptr) {
                dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([dlg]() {
                    try { dlg->Hide(); } catch(...) {}
                }));
            } else {
                try { dlg->Hide(); } catch(...) {}
            }
            return;
        }

        // Attach continuation to hide when UI clone is created.
        try {
            auto sharedEntry = entry;
            create_task(sharedEntry->ready).then([sharedEntry]() {
                try {
                    auto dlg = sharedEntry->uiDialog;
                    if (dlg == nullptr) return;
                    auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
                    if (dispatcher == nullptr) {
                        try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) {}
                    }
                    if (dispatcher != nullptr) {
                        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([dlg]() {
                            try { dlg->Hide(); } catch(...) {}
                        }));
                    } else {
                        try { dlg->Hide(); } catch(...) {}
                    }
                } catch(...) {}
            });
        } catch(...) {}

    } catch(...) {}
}

// static guard flag (definition)
std::atomic<bool> ModalDialog::s_globalDialogOpen{ false };

void ModalDialog::HideDialog(Windows::UI::Xaml::Controls::ContentDialog^ dialog)
{
    try {
        moonlight_xbox_dx::Utils::Logf("HideDialog called for %p\n", (void*)dialog);
        // If caller passed a null pointer, nothing to do.
        if (dialog == nullptr) {
            moonlight_xbox_dx::Utils::Log("HideDialog: called with null dialog, ignoring\n");
            return;
        }
        Windows::UI::Xaml::Controls::ContentDialog^ uiDialog = nullptr;
        std::shared_ptr<DialogEntry> entry;
        {
            std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
            auto it = s_dialogClones.find((void*)dialog);
            if (it != s_dialogClones.end()) entry = it->second;
            // If not found by key, try to locate any entry whose uiDialog matches the passed pointer.
            if (entry == nullptr) {
                for (auto &p : s_dialogClones) {
                    if (p.second && p.second->uiDialog == dialog) { entry = p.second; break; }
                }
            }
        }

        if (entry != nullptr) {
            moonlight_xbox_dx::Utils::Logf("HideDialog: found entry for %p (uiDialog=%p)\n", (void*)dialog, (void*)entry->uiDialog);
            // If the UI clone is already available, use it. Otherwise attach a
            // continuation that will hide the clone once it's created.
            if (entry->uiDialog != nullptr) {
                uiDialog = entry->uiDialog;
            }
            else {
                // We'll post a continuation which will run when the UI handler signals readiness.
                auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
                if (dispatcher == nullptr) {
                    try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) {}
                }

                // Create a continuation that will run once the UI clone is created.
                try {
                    auto sharedEntry = entry; // capture
                    create_task(sharedEntry->ready).then([sharedEntry]() {
                        try {
                            auto dlg = sharedEntry->uiDialog;
                            moonlight_xbox_dx::Utils::Logf("HideDialog continuation: ready for original %p, ui clone %p\n", (void*)sharedEntry.get(), (void*)dlg);
                            if (dlg == nullptr) return;
                            // Resolve dispatcher at the time we run so we prefer the correct UI thread.
                            auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
                            if (dispatcher == nullptr) {
                                try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) {}
                            }
                            if (dispatcher != nullptr) {
                                dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([dlg]() {
                                    try { dlg->Hide(); } catch(...) {}
                                }));
                            } else {
                                try { dlg->Hide(); moonlight_xbox_dx::Utils::Logf("HideDialog continuation: hid ui clone %p\n", (void*)dlg); } catch(...) {}
                            }
                        } catch(...) {}
                    });
                } catch(...) {}

                // We already scheduled the hide; nothing more to do in this call.
                return;
            }
        }
        else {
            // No mapping entry found; log and fall back to attempting to hide the original dialog object.
            try {
                std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                moonlight_xbox_dx::Utils::Logf("HideDialog: no mapping for %p (map size=%zu)\n", (void*)dialog, s_dialogClones.size());
            } catch(...) {}
            uiDialog = dialog;
        }

        auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
        if (dispatcher == nullptr) {
            try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) {}
        }

        // If we don't have a uiDialog to hide, just log and return.
        if (uiDialog == nullptr) {
            moonlight_xbox_dx::Utils::Log("HideDialog: resolved uiDialog is null, nothing to hide\n");
            return;
        }

        if (dispatcher != nullptr) {
            // Capture uiDialog and null-check again on the UI thread before calling Hide.
            auto captured = uiDialog;
            dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([captured]() {
                try {
                    if (captured == nullptr) {
                        moonlight_xbox_dx::Utils::Log("HideDialog dispatched: uiDialog is null, skipping Hide\n");
                        return;
                    }
                    captured->Hide();
                } catch(...) {}
            }));
        } else {
            try { uiDialog->Hide(); } catch(...) {}
        }
	} catch(...) {}
}

Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Controls::ContentDialogResult>^ ModalDialog::ShowOnceAsync(Windows::UI::Xaml::Controls::ContentDialog^ dialog)
{
    bool expected = false;
    if (!ModalDialog::s_globalDialogOpen.compare_exchange_strong(expected, true)) {
        moonlight_xbox_dx::Utils::Log("ContentDialogGuard (modal): dialog already open, skipping ShowAsync\n");
        // Return completed IAsyncOperation with None
        return concurrency::create_async([]() -> Windows::UI::Xaml::Controls::ContentDialogResult { return Windows::UI::Xaml::Controls::ContentDialogResult::None; });
    }

    // Ensure ShowAsync is invoked on the UI thread. We'll dispatch a small handler to the
    // UI dispatcher which starts the ShowAsync operation and completes a task_completion_event
    // when the dialog completes. The background worker waits on that event so we do not
    // block the UI thread or call UI APIs from a non-UI thread (which causes WrongThreadException).
    // Create and publish a DialogEntry synchronously so callers (e.g. HideDialog)
    // can find the mapping even if they run immediately after ShowProgressDialog.
    auto preEntry = std::make_shared<DialogEntry>();
    preEntry->id = s_dialogEntryCounter.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
        s_dialogClones[(void*)dialog] = preEntry;
    }
    moonlight_xbox_dx::Utils::Logf("ShowOnceAsync: published entry id=%llu for %p\n", (unsigned long long)preEntry->id, (void*)dialog);

    return concurrency::create_async([dialog, preEntry]() -> Windows::UI::Xaml::Controls::ContentDialogResult {
        using namespace concurrency;
        Windows::UI::Xaml::Controls::ContentDialogResult result = Windows::UI::Xaml::Controls::ContentDialogResult::None;

        task_completion_event<Windows::UI::Xaml::Controls::ContentDialogResult> tce;

        try {
            // Prefer the Window dispatcher; fall back to CoreApplication main view's dispatcher.
            auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
            if (dispatcher == nullptr) {
                try {
                    auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView;
                    if (coreView != nullptr && coreView->CoreWindow != nullptr) {
                        dispatcher = coreView->CoreWindow->Dispatcher;
                    }
                }
                catch (...) {
                    dispatcher = nullptr;
                }
            }

            // If we have a dispatcher, dispatch the ShowAsync call there so it's executed on the UI thread.
            if (dispatcher != nullptr) {
                // Use a simple dispatched handler that starts ShowAsync and completes tce when done.
                // Capture tce by value (it is copyable) so the dispatched handler can set it safely.
                auto entry = preEntry; // use the pre-published entry

                auto handler = ref new Windows::UI::Core::DispatchedHandler([dialog, tce, entry]() mutable {
                    try {
                        // Create a UI-thread owned dialog instance and copy basic dialog properties.
                        Windows::UI::Xaml::Controls::ContentDialog^ uiDialog = nullptr;

                        // If the original is our ModalDialog type, try to create the same type.
                        auto srcModal = dynamic_cast<::moonlight_xbox_dx::ModalDialog^>(dialog);
                        if (srcModal != nullptr) {
                            auto newModal = ref new ::moonlight_xbox_dx::ModalDialog();
                            // Copy standard ContentDialog properties, but avoid moving UI elements which are already
                            // children of another visual. Copy plain text from our named fields instead of copying
                            // the Content property directly which may contain a UI element.
                            try { auto tstr = dynamic_cast<Platform::String^>(srcModal->Title); if (tstr != nullptr && tstr->Length() > 0) newModal->Title = tstr; } catch(...) {}
                            try {
                                auto msg = srcModal->GetMessage();
                                if (msg != nullptr) newModal->SetMessage(msg);
                            } catch (...) {}
                            try { auto p = srcModal->PrimaryButtonText; if (p != nullptr && p->Length() > 0) newModal->PrimaryButtonText = p; } catch (...) {}
                            try { auto s = srcModal->SecondaryButtonText; if (s != nullptr && s->Length() > 0) newModal->SecondaryButtonText = s; } catch (...) {}
                            try { auto c = srcModal->CloseButtonText; if (c != nullptr && c->Length() > 0) newModal->CloseButtonText = c; } catch (...) {}
                            try { if (srcModal->IsProgressShown()) newModal->ShowProgress(true); } catch(...) {}
                            uiDialog = reinterpret_cast<Windows::UI::Xaml::Controls::ContentDialog^>(newModal);
                        }
                        else {
                            // Generic ContentDialog: create a fresh ContentDialog and copy basic properties.
                            auto newDlg = ref new Windows::UI::Xaml::Controls::ContentDialog();
                            try { newDlg->Title = dialog->Title; } catch (...) {}
                            // Don't blindly copy dialog->Content since it may be a UI element already parented elsewhere.
                            // If the content is a plain string or TextBlock, copy its text instead.
                            try {
                                auto textContent = dynamic_cast<Windows::UI::Xaml::Controls::TextBlock^>(dialog->Content);
                                if (textContent != nullptr) {
                                    auto tb = ref new Windows::UI::Xaml::Controls::TextBlock();
                                    tb->Text = textContent->Text;
                                    newDlg->Content = tb;
                                }
                                else {
                                    // If Content is a Platform::String, set a TextBlock. Otherwise skip copying content.
                                    auto boxed = dynamic_cast<Platform::String^>(dialog->Content);
                                    if (boxed != nullptr) {
                                        auto tb = ref new Windows::UI::Xaml::Controls::TextBlock();
                                        tb->Text = boxed;
                                        newDlg->Content = tb;
                                    }
                                }
                            } catch(...) {}

                            try { auto p = dialog->PrimaryButtonText; if (p != nullptr && p->Length() > 0) newDlg->PrimaryButtonText = p; } catch (...) {}
                            try { auto s = dialog->SecondaryButtonText; if (s != nullptr && s->Length() > 0) newDlg->SecondaryButtonText = s; } catch (...) {}
                            try { auto c = dialog->CloseButtonText; if (c != nullptr && c->Length() > 0) newDlg->CloseButtonText = c; } catch (...) {}
                            try { newDlg->IsSecondaryButtonEnabled = dialog->IsSecondaryButtonEnabled; } catch (...) {}
                            uiDialog = newDlg;
                        }

                        if (uiDialog == nullptr) {
                            tce.set(Windows::UI::Xaml::Controls::ContentDialogResult::None);
                            return;
                        }

                        // Publish the created UI clone into the pre-published entry and signal waiters.
                        try { entry->uiDialog = uiDialog; } catch(...) {}
                        try { entry->ready.set(); } catch(...) {}
                        // Also publish a reverse mapping so HideDialog can find the entry when called with
                        // the UI-owned clone pointer instead of the original dialog pointer.
                        try {
                            std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                            s_dialogClones[(void*)uiDialog] = entry;
                        } catch(...) {}
                        moonlight_xbox_dx::Utils::Logf("ShowOnceAsync: created UI clone %p for original %p (id=%llu)\n", (void*)uiDialog, (void*)dialog, (unsigned long long)entry->id);

                        auto op = uiDialog->ShowAsync();
                        create_task(op).then([tce, dialog, entry](Windows::UI::Xaml::Controls::ContentDialogResult r) mutable {
                            // Remove mapping on completion: erase both original and ui clone keys if present.
                            try {
                                std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                                try { s_dialogClones.erase((void*)dialog); moonlight_xbox_dx::Utils::Logf("ShowOnceAsync: erased mapping for original %p (id=%llu)\n", (void*)dialog, (unsigned long long)entry->id); } catch(...) {}
                                try { if (entry && entry->uiDialog != nullptr) { s_dialogClones.erase((void*)entry->uiDialog); moonlight_xbox_dx::Utils::Logf("ShowOnceAsync: erased mapping for ui clone %p (id=%llu)\n", (void*)entry->uiDialog, (unsigned long long)entry->id); } } catch(...) {}
                            } catch(...) {}
                            tce.set(r);
                        });
                    }
                    catch (...) {
                        tce.set(Windows::UI::Xaml::Controls::ContentDialogResult::None);
                    }
                });

                // Post the handler to the UI dispatcher. We don't need to wait for RunAsync to finish
                // because tce will be set when the dialog completes.
                (void)dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, handler);

                // Wait on the completion event on this background thread only.
                try {
                    result = create_task(tce).get();
                }
                catch (...) {
                    result = Windows::UI::Xaml::Controls::ContentDialogResult::None;
                }
            }
            else {
                // No UI dispatcher available in this context. Calling ShowAsync from a
                // non-UI thread will raise WrongThreadException; return None and log.
                moonlight_xbox_dx::Utils::Log("ModalDialog::ShowOnceAsync: no UI dispatcher available, skipping ShowAsync\n");
                result = Windows::UI::Xaml::Controls::ContentDialogResult::None;
                // Remove the pre-published mapping since we won't create a UI clone.
                try {
                    std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                    s_dialogClones.erase((void*)dialog);
                } catch(...) {}
            }
        }
        catch (...) {
            result = Windows::UI::Xaml::Controls::ContentDialogResult::None;
        }

        ModalDialog::s_globalDialogOpen.store(false);
        return result;
    });
}

Windows::Foundation::IAsyncOperation<Windows::UI::Xaml::Controls::ContentDialogResult>^ ModalDialog::ShowProgressDialogAsync(Platform::String^ title, Platform::String^ message)
{
    auto dlg = ref new ::moonlight_xbox_dx::ModalDialog();
    dlg->SetTitle(title);
    dlg->SetMessage(message);
    dlg->ShowProgress(true);
    return ModalDialog::ShowOnceAsync(dlg);
}

Windows::Foundation::IAsyncOperation<int>^ ModalDialog::ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup, Platform::String^ title, Platform::String^ primaryButtonText)
{
    return ModalDialog::ShowOnceAsyncWithXaml(xamlMarkup, title, primaryButtonText, true, true, true);
}

Windows::Foundation::IAsyncOperation<int>^ ModalDialog::ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup, Platform::String^ title, Platform::String^ primaryButtonText, bool resumeVisible, bool closeVisible, bool closeAndStartVisible)
{
    auto original = ref new ::moonlight_xbox_dx::ModalDialog();
    try { auto t = title; if (t != nullptr && t->Length() > 0) original->Title = t; } catch(...) {}
    try { auto p = primaryButtonText; if (p != nullptr && p->Length() > 0) original->PrimaryButtonText = p; } catch(...) {}

    auto preEntry = std::make_shared<DialogEntry>();
    preEntry->id = s_dialogEntryCounter.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
        s_dialogClones[(void*)original] = preEntry;
    }

    moonlight_xbox_dx::Utils::Logf("ShowOnceAsyncWithXaml: published entry id=%llu for %p\n", (unsigned long long)preEntry->id, (void*)original);

    return concurrency::create_async([original, preEntry, xamlMarkup, resumeVisible, closeVisible, closeAndStartVisible]() -> int {
        using namespace concurrency;
        int resultCode = 0; // 0 = Cancel/None
        task_completion_event<int> tce;

        try {
            auto dispatcher = Windows::UI::Xaml::Window::Current != nullptr ? Windows::UI::Xaml::Window::Current->Dispatcher : nullptr;
            if (dispatcher == nullptr) {
                try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) dispatcher = coreView->CoreWindow->Dispatcher; } catch(...) { dispatcher = nullptr; }
            }

            if (dispatcher != nullptr) {
                auto entry = preEntry;
                auto handler = ref new Windows::UI::Core::DispatchedHandler([original, tce, entry, xamlMarkup, resumeVisible, closeVisible, closeAndStartVisible]() mutable {
                    try {
                        Windows::UI::Xaml::Controls::ContentDialog^ uiDialog = nullptr;
                        // We'll create a fresh ModalDialog instance and populate its Content from XAML.
                        auto newModal = ref new ::moonlight_xbox_dx::ModalDialog();
                        try { auto tstr = dynamic_cast<Platform::String^>(original->Title); if (tstr != nullptr && tstr->Length() > 0) newModal->Title = tstr; } catch(...) {}
                        try { auto pstr = dynamic_cast<Platform::String^>(original->PrimaryButtonText); if (pstr != nullptr && pstr->Length() > 0) newModal->PrimaryButtonText = pstr; } catch(...) {}
                        uiDialog = reinterpret_cast<Windows::UI::Xaml::Controls::ContentDialog^>(newModal);

                        if (uiDialog == nullptr) { tce.set(0); return; }

                        // If XAML was provided, parse it and set as Content.
                        try {
                            if (xamlMarkup != nullptr && xamlMarkup->Length() > 0) {
                                auto parsed = Windows::UI::Xaml::Markup::XamlReader::Load(xamlMarkup);
                                auto element = dynamic_cast<Windows::UI::Xaml::UIElement^>(parsed);
                                if (element != nullptr) uiDialog->Content = element;
                            }
                        } catch(...) {}

                        // Provide default mapping of buttons inside the XAML to integer result codes and apply visibility flags.
                        try {
                            // Robustly traverse the parsed content tree and attach handlers to any Buttons matching our expected names.
                            auto content = dynamic_cast<DependencyObject^>(uiDialog->Content);
                            std::function<void(DependencyObject^)> walk;
                            walk = [&](DependencyObject^ node) {
                                if (node == nullptr) return;
                                try {
                                    auto btn = dynamic_cast<Windows::UI::Xaml::Controls::Button^>(node);
                                    if (btn != nullptr) {
                                        auto nm = btn->Name;
                                        if (nm == "ResumeButton") {
                                            btn->Visibility = resumeVisible ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                                            // Capture uiDialog and set result then hide dialog so it closes immediately.
                                            auto captured = uiDialog;
                                            btn->Click += ref new Windows::UI::Xaml::RoutedEventHandler([tce, captured](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
                                                try { tce.set(1); } catch(...) {}
                                                try { if (captured != nullptr) captured->Hide(); } catch(...) {}
                                            });
                                        } else if (nm == "CloseButton") {
                                            btn->Visibility = closeVisible ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                                            auto captured = uiDialog;
                                            btn->Click += ref new Windows::UI::Xaml::RoutedEventHandler([tce, captured](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
                                                try { tce.set(2); } catch(...) {}
                                                try { if (captured != nullptr) captured->Hide(); } catch(...) {}
                                            });
                                        } else if (nm == "CloseAndStartButton") {
                                            btn->Visibility = closeAndStartVisible ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                                            auto captured = uiDialog;
                                            btn->Click += ref new Windows::UI::Xaml::RoutedEventHandler([tce, captured](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
                                                try { tce.set(3); } catch(...) {}
                                                try { if (captured != nullptr) captured->Hide(); } catch(...) {}
                                            });
                                        } else if (nm == "CancelButton") {
                                            // Always show CancelButton by default unless XAML itself hides it
                                            btn->Visibility = btn->Visibility; // no-op
                                            auto captured = uiDialog;
                                            btn->Click += ref new Windows::UI::Xaml::RoutedEventHandler([tce, captured](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
                                                try { tce.set(0); } catch(...) {}
                                                try { if (captured != nullptr) captured->Hide(); } catch(...) {}
                                            });
                                        }
                                    }
                                } catch(...) {}

                                try {
                                    int cnt = VisualTreeHelper::GetChildrenCount(node);
                                    for (int i = 0; i < cnt; ++i) {
                                        auto child = VisualTreeHelper::GetChild(node, i);
                                        walk(child);
                                    }
                                } catch(...) {}
                            };

                            // Walk the content subtree if present
                            if (content != nullptr) walk(content);
                        } catch(...) {}

                        // Publish the created UI clone into the pre-published entry and signal waiters.
                        try { entry->uiDialog = uiDialog; } catch(...) {}
                        try { entry->ready.set(); } catch(...) {}
                        try {
                            std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                            s_dialogClones[(void*)uiDialog] = entry;
                        } catch(...) {}
                        moonlight_xbox_dx::Utils::Logf("ShowOnceAsyncWithXaml: created UI clone %p for original %p (id=%llu)\n", (void*)uiDialog, (void*)original, (unsigned long long)entry->id);

                        // Start showing the dialog. If no explicit button handler sets tce, fall back to mapping
                        // the ContentDialogResult to conservative integer codes.
                        auto op = uiDialog->ShowAsync();
                        create_task(op).then([tce, entry, uiDialog, original](Windows::UI::Xaml::Controls::ContentDialogResult r) mutable {
                            try {
                                int code = 0;
                                try {
                                    if (r == Windows::UI::Xaml::Controls::ContentDialogResult::Primary) code = 1;
                                    else if (r == Windows::UI::Xaml::Controls::ContentDialogResult::Secondary) code = 2;
                                    else code = 0;
                                } catch(...) { code = 0; }
                                // Try to set the tce if not already set by an in-content button handler
                                try { tce.set(code); } catch(...) {}

                                // Remove mappings for cleanup
                                try {
                                    std::lock_guard<std::mutex> lock(s_dialogClonesMutex);
                                    try { s_dialogClones.erase((void*)original); } catch(...) {}
                                    try { if (entry && entry->uiDialog != nullptr) s_dialogClones.erase((void*)entry->uiDialog); } catch(...) {}
                                } catch(...) {}
                            } catch(...) {}
                        });
                    } catch(...) { try { tce.set(0); } catch(...) {} }
                });

                (void)dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, handler);
                try { resultCode = create_task(tce).get(); } catch(...) { resultCode = 0; }
            } else {
                moonlight_xbox_dx::Utils::Log("ModalDialog::ShowOnceAsyncWithXaml: no UI dispatcher available, skipping ShowAsync\n");
                try { std::lock_guard<std::mutex> lock(s_dialogClonesMutex); s_dialogClones.erase((void*)original); } catch(...) {}
                resultCode = 0;
            }
        } catch(...) { resultCode = 0; }

        ModalDialog::s_globalDialogOpen.store(false);
        return resultCode;
    });
}

Windows::Foundation::IAsyncOperation<int>^ ModalDialog::ShowOnceAsyncWithXaml(Platform::String^ xamlMarkup)
{
     return ModalDialog::ShowOnceAsyncWithXaml(xamlMarkup, nullptr, nullptr, true, true, true);
 }

// End of file
