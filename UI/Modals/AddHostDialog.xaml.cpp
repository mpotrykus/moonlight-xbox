#include "pch.h"
#include "UI\Modals\AddHostDialog.xaml.h"

using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::Foundation::Collections;

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

void AddHostDialog::SetRecentHostnames(IVector<Platform::String^>^ hostnames)
{
    try {
        if (hostnames == nullptr || hostnames->Size == 0) return;
        RecentHostsBorder->Visibility = Windows::UI::Xaml::Visibility::Visible;
        for (unsigned int i = 0; i < hostnames->Size; i++) {
            auto hostname = hostnames->GetAt(i);
            auto btn = ref new Button();
            btn->Content = hostname;
            btn->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily("Bahnschrift");
            btn->FontSize = 12;
            btn->Height = 36;
            btn->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
            btn->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
            Windows::UI::Xaml::CornerRadius cr;
            cr.TopLeft = cr.TopRight = cr.BottomRight = cr.BottomLeft = 8.0;
            btn->CornerRadius = cr;
            btn->Padding = Windows::UI::Xaml::Thickness{ 12, 0, 12, 0 };
            Windows::UI::Color bgColor;
            bgColor.A = 0x88; bgColor.R = 0; bgColor.G = 0; bgColor.B = 0;
            btn->Background = ref new Windows::UI::Xaml::Media::SolidColorBrush(bgColor);
            btn->Foreground = ref new Windows::UI::Xaml::Media::SolidColorBrush(Windows::UI::Colors::White);
            btn->Click += ref new RoutedEventHandler([this, hostname](Platform::Object^ sender, RoutedEventArgs^ args) {
                try {
                    HostnameTextBox->Text = hostname;
                    if (ErrorText != nullptr) ErrorText->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
                    if (m_onAdd != nullptr) m_onAdd->Invoke(sender, args);
                } catch (...) {}
            });
            RecentHostsPanel->Children->Append(btn);
        }
    } catch (...) {}
}

} // namespace moonlight_xbox_dx
