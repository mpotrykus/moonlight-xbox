#include "pch.h"
#include "Controls\SwatchPicker.xaml.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::ViewManagement;
using namespace Windows::Foundation::Collections;

static const Windows::UI::Color kDefaultSwatches[] = {
    { 255, 255, 185,   0 }, // Yellow gold
    { 255, 247,  99,  12 }, // Orange
    { 255, 202,  80,  16 }, // Dark orange
    { 255, 232,  17,  35 }, // Red
    { 255, 234,   0,  94 }, // Rose
    { 255, 195,   0,  82 }, // Raspberry
    { 255, 135, 100, 184 }, // Violet
    { 255, 116,  77, 169 }, // Purple
    { 255, 107, 105, 214 }, // Indigo
    { 255,   0, 120, 215 }, // Windows blue
    { 255,   0, 153, 188 }, // Teal
    { 255,   3, 131, 135 }, // Dark teal
    { 255,  73, 130,   5 }, // Green
    { 255,  16, 137,  62 }, // Emerald
    { 255, 105, 121, 126 }, // Slate
};
static const int kDefaultSwatchCount = ARRAYSIZE(kDefaultSwatches);

SwatchPicker::SwatchPicker()
{
    InitializeComponent();
    m_entries = ref new Vector<SwatchEntry^>();
    SwatchGrid->ItemsSource = m_entries;
    BuildDefaultSwatches();
}

Windows::UI::Color SwatchPicker::GetSystemAccentColor()
{
    auto uiSettings = ref new UISettings();
    return uiSettings->GetColorValue(UIColorType::Accent);
}

void SwatchPicker::BuildDefaultSwatches()
{
    m_entries->Clear();
    m_selectedEntry = nullptr;

    auto sysColor = GetSystemAccentColor();
    auto sysEntry = ref new SwatchEntry(sysColor, true);
    m_entries->Append(sysEntry);

    for (int i = 0; i < kDefaultSwatchCount; ++i)
        m_entries->Append(ref new SwatchEntry(kDefaultSwatches[i], false));

    SelectEntry(sysEntry, false);
}

void SwatchPicker::SelectEntry(SwatchEntry^ entry, bool fireEvent)
{
    if (m_selectedEntry != nullptr)
        m_selectedEntry->IsSelected = false;

    m_selectedEntry = entry;

    if (entry != nullptr) {
        entry->IsSelected = true;
        m_useSystemAccent = entry->IsSystemAccent;
        m_selectedColor   = entry->DisplayColor;
    }

    if (fireEvent && entry != nullptr)
        ColorChanged(this, m_selectedColor, m_useSystemAccent);
}

void SwatchPicker::SwatchGrid_ItemClick(Platform::Object^ sender, ItemClickEventArgs^ e)
{
    auto entry = dynamic_cast<SwatchEntry^>(e->ClickedItem);
    if (entry != nullptr)
        SelectEntry(entry, true);
}

void SwatchPicker::SetSwatches(IVector<Windows::UI::Color>^ colors)
{
    m_entries->Clear();
    m_selectedEntry = nullptr;

    auto sysColor = GetSystemAccentColor();
    auto sysEntry = ref new SwatchEntry(sysColor, true);
    m_entries->Append(sysEntry);

    for (auto c : colors)
        m_entries->Append(ref new SwatchEntry(c, false));

    SelectEntry(sysEntry, false);
}

void SwatchPicker::SelectColor(Windows::UI::Color color, bool useSystem)
{
    for (unsigned int i = 0; i < m_entries->Size; ++i) {
        auto entry = m_entries->GetAt(i);
        if (useSystem && entry->IsSystemAccent) {
            SelectEntry(entry, false);
            return;
        }
        if (!useSystem && !entry->IsSystemAccent
            && entry->DisplayColor.R == color.R
            && entry->DisplayColor.G == color.G
            && entry->DisplayColor.B == color.B)
        {
            SelectEntry(entry, false);
            return;
        }
    }
    // Fallback: system swatch
    if (m_entries->Size > 0)
        SelectEntry(m_entries->GetAt(0), false);
}
