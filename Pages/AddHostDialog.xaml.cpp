#include "pch.h"
#include "Pages\AddHostDialog.xaml.h"

using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;

namespace moonlight_xbox_dx
{

AddHostDialog::AddHostDialog()
{
    InitializeComponent();
}

void AddHostDialog::Configure(
    RoutedEventHandler^ onAdd,
    RoutedEventHandler^ onCancel)
{
    m_onAdd = onAdd;
    m_onCancel = onCancel;

    try {
        AddButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            if (ErrorText != nullptr) ErrorText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
            if (m_onAdd != nullptr) m_onAdd->Invoke(sender, args);
        });

        CancelButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            try { this->Hide(); } catch (...) {}
            if (m_onCancel != nullptr) m_onCancel->Invoke(sender, args);
        });

        HostnameTextBox->KeyDown += ref new KeyEventHandler([this](Platform::Object^, KeyRoutedEventArgs^ e) {
            if (e->Key == Windows::System::VirtualKey::Enter) {
                try { Windows::UI::ViewManagement::InputPane::GetForCurrentView()->TryHide(); } catch (...) {}
                if (ErrorText != nullptr) ErrorText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
                if (m_onAdd != nullptr) m_onAdd->Invoke(this, ref new RoutedEventArgs());
            }
        });
    } catch (...) {}
}

Platform::String^ AddHostDialog::GetHostname()
{
    try { if (HostnameTextBox != nullptr) return HostnameTextBox->Text; } catch (...) {}
    return nullptr;
}

void AddHostDialog::ShowError(Platform::String^ message)
{
    try {
        if (ErrorText != nullptr) {
            ErrorText->Text = message;
            ErrorText->Visibility = Windows::UI::Xaml::Visibility::Visible;
        }
    } catch (...) {}
}

void AddHostDialog::SetAddButtonEnabled(bool enabled)
{
    try { if (AddButton != nullptr) AddButton->IsEnabled = enabled; } catch (...) {}
}

} // namespace moonlight_xbox_dx
