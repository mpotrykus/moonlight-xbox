#include "pch.h"
#include "Controls/TabsLayout.xaml.h"

using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Markup;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::Foundation::Collections;
#include <collection.h>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml;

TabsLayout::TabsLayout()
{
    InitializeComponent();
    this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &TabsLayout::OnLoaded);
    this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &TabsLayout::OnUnloaded);
    m_tabButtons = ref new Platform::Collections::Vector<Button^>();
    m_clickTokens = ref new Platform::Collections::Vector<long long>();
    m_focusTokens = ref new Platform::Collections::Vector<long long>();
}

UIElement^ TabsLayout::LeftContent::get()
{
    return safe_cast<UIElement^>(ContentPresenter->Content);
}

void TabsLayout::LeftContent::set(UIElement^ value)
{
    ContentPresenter->Content = value;
}

UIElement^ TabsLayout::TabsContent::get()
{
    return safe_cast<UIElement^>(TabsPresenter->Content);
}

void TabsLayout::TabsContent::set(UIElement^ value)
{
    TabsPresenter->Content = value;
}

// DependencyProperty registration for TargetPanelName
Windows::UI::Xaml::DependencyProperty^ TabsLayout::TargetPanelNameProperty = nullptr;

void TabsLayout::SetTargetPanelName(UIElement^ element, Platform::String^ value)
{
    if (TargetPanelNameProperty == nullptr) {
        TargetPanelNameProperty = DependencyProperty::RegisterAttached(
            "TargetPanelName",
            Platform::String::typeid,
            TabsLayout::typeid,
            nullptr);
    }
    element->SetValue(TargetPanelNameProperty, value);
}

Platform::String^ TabsLayout::GetTargetPanelName(UIElement^ element)
{
    if (TargetPanelNameProperty == nullptr) return nullptr;
    auto val = element->GetValue(TargetPanelNameProperty);
    return safe_cast<Platform::String^>(val);
}

// DependencyProperty registration for TargetPanel (direct reference)
Windows::UI::Xaml::DependencyProperty^ TabsLayout::TargetPanelProperty = nullptr;

// IsSelected attached property (bool)
Windows::UI::Xaml::DependencyProperty^ TabsLayout::IsSelectedProperty = nullptr;

void TabsLayout::SetTargetPanel(UIElement^ element, UIElement^ value)
{
    if (TargetPanelProperty == nullptr) {
        TargetPanelProperty = DependencyProperty::RegisterAttached(
            "TargetPanel",
            Windows::UI::Xaml::UIElement::typeid,
            TabsLayout::typeid,
            nullptr);
    }
    element->SetValue(TargetPanelProperty, value);
}

UIElement^ TabsLayout::GetTargetPanel(UIElement^ element)
{
    if (TargetPanelProperty == nullptr) return nullptr;
    auto val = element->GetValue(TargetPanelProperty);
    return safe_cast<UIElement^>(val);
}

void TabsLayout::SetIsSelected(UIElement^ element, bool value)
{
    if (IsSelectedProperty == nullptr) {
        IsSelectedProperty = DependencyProperty::RegisterAttached(
            "IsSelected",
            bool::typeid,
            TabsLayout::typeid,
            nullptr);
    }
    element->SetValue(IsSelectedProperty, value);

    // Programmatic styling: set Button.Background and Button.Foreground directly so selection
    // always shows correctly regardless of VisualState application. Store original brushes on
    // the Button's Tag (as a small tuple-like object) so they can be restored when unselected.
    auto btn = dynamic_cast<Button^>(element);
    if (btn != nullptr) {
        // Retrieve any stored original brushes from Tag (we'll store as a PropertySet)
        Windows::Foundation::Collections::IPropertySet^ props = nullptr;
        auto existingTag = btn->Tag;
        props = dynamic_cast<Windows::Foundation::Collections::IPropertySet^>(existingTag);
        if (props == nullptr) {
            props = ref new Windows::Foundation::Collections::PropertySet();
            btn->Tag = props;
        }

        if (value) {
            // Save original brushes if not already saved
            if (!props->HasKey("origBackground")) {
                props->Insert("origBackground", btn->Background);
            }
            if (!props->HasKey("origForeground")) {
                props->Insert("origForeground", btn->Foreground);
            }
            // Apply selected brushes
            btn->Foreground = ref new SolidColorBrush(Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF));
            // Create a horizontal gradient foreground from transparent #333333 (left) to opaque #333333 (right)
            auto lgBrush = ref new LinearGradientBrush();
            lgBrush->StartPoint = Windows::Foundation::Point(0, 0.5);
            lgBrush->EndPoint = Windows::Foundation::Point(1, 0.5);
            auto stop1 = ref new GradientStop();
            stop1->Color = Windows::UI::ColorHelper::FromArgb(0x00, 0x33, 0x33, 0x33); // transparent #333333
            stop1->Offset = 0.0;
            lgBrush->GradientStops->Append(stop1);
            auto stop2 = ref new GradientStop();
            stop2->Color = Windows::UI::ColorHelper::FromArgb(0xFF, 0x33, 0x33, 0x33); // opaque #333333
            stop2->Offset = 1.0;
            lgBrush->GradientStops->Append(stop2);
			btn->Background = lgBrush;
        }
        else {
            // Restore original brushes if available, otherwise clear to defaults
            if (props->HasKey("origBackground")) {
                btn->Background = safe_cast<Brush^>(props->Lookup("origBackground"));
                props->Remove("origBackground");
            }
            else {
                btn->Background = nullptr;
            }
            if (props->HasKey("origForeground")) {
                btn->Foreground = safe_cast<Brush^>(props->Lookup("origForeground"));
                props->Remove("origForeground");
            }
            else {
                btn->Foreground = nullptr;
            }
        }
    }
}

bool TabsLayout::GetIsSelected(UIElement^ element)
{
    if (IsSelectedProperty == nullptr) return false;
    auto val = element->GetValue(IsSelectedProperty);
    return safe_cast<bool>(val);
}

void TabsLayout::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    HookUpTabs();
}

void TabsLayout::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    UnhookTabs();
}

static void WalkVisualChildren(UIElement^ root, IVector<UIElement^>^ out)
{
    auto fe = dynamic_cast<FrameworkElement^>(root);
    if (fe == nullptr) return;
    int count = VisualTreeHelper::GetChildrenCount(fe);
    for (int i = 0; i < count; i++) {
        auto child = safe_cast<UIElement^>(VisualTreeHelper::GetChild(fe, i));
        if (child == nullptr) continue;
        out->Append(child);
        WalkVisualChildren(child, out);
    }
}

void TabsLayout::HookUpTabs()
{
    UnhookTabs();
    if (TabsPresenter == nullptr) return;
    auto content = TabsPresenter->Content;
    if (content == nullptr) return;

    // Collect buttons inside TabsPresenter content
    auto found = ref new Platform::Collections::Vector<UIElement^>();
    WalkVisualChildren(safe_cast<UIElement^>(content), found);
    for (unsigned int i = 0; i < found->Size; i++) {
        auto btn = dynamic_cast<Button^>(found->GetAt(i));
        if (btn != nullptr) {
            // Force buttons to take full available width in the left column
            btn->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
            // Ensure the button's content is left-aligned so text sits on the left edge
            btn->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
            // Remove explicit Right alignment from markup if present by clearing Margin/Alignment
            btn->Margin = Windows::UI::Xaml::Thickness(0);
            // Ensure vectors exist (they may have been cleared)
            if (m_tabButtons == nullptr) m_tabButtons = ref new Platform::Collections::Vector<Button^>();
            if (m_clickTokens == nullptr) m_clickTokens = ref new Platform::Collections::Vector<long long>();
            if (m_focusTokens == nullptr) m_focusTokens = ref new Platform::Collections::Vector<long long>();
            // Hook handlers using member methods so we can unhook later
            auto clickToken = btn->Click += ref new RoutedEventHandler(this, &TabsLayout::OnTabButtonClick);
            auto focusToken = btn->GotFocus += ref new RoutedEventHandler(this, &TabsLayout::OnTabButtonGotFocus);
            m_clickTokens->Append(clickToken.Value);
            m_focusTokens->Append(focusToken.Value);
            m_tabButtons->Append(btn);
        }
    }

    // If there are any tab buttons, default-select the first one so the shown
    // left content matches the visible panel on load.
    if (m_tabButtons != nullptr && m_tabButtons->Size > 0) {
        auto first = m_tabButtons->GetAt(0);
        if (first != nullptr) {
            // If the button has a direct TargetPanel, try to select by that panel's name
            auto direct = GetTargetPanel(first);
            if (direct != nullptr) {
                auto fe = dynamic_cast<FrameworkElement^>(direct);
                if (fe != nullptr && fe->Name != nullptr && fe->Name->Length() > 0) {
                    SelectTabByName(fe->Name);
                }
                else {
                    // Make direct panel visible as a fallback
                    direct->Visibility = Windows::UI::Xaml::Visibility::Visible;
                    TabsLayout::SetIsSelected(first, true);
                    m_selectedButton = first;
                }
            }
            else {
                // Try to select by TargetPanelName attached property
                auto name = GetTargetPanelName(first);
                if (name != nullptr) {
                    SelectTabByName(name);
                }
                else {
                    // No mapping info; still mark the button selected so it's visually active
                    TabsLayout::SetIsSelected(first, true);
                    m_selectedButton = first;
                }
            }
        }
    }
}

void TabsLayout::UnhookTabs()
{
    // Unregister tokens paired with buttons (guard against null vectors)
    if (m_tabButtons != nullptr && m_clickTokens != nullptr && m_focusTokens != nullptr) {
        unsigned int count = m_tabButtons->Size;
        if (m_clickTokens->Size < count) count = m_clickTokens->Size;
        if (m_focusTokens->Size < count) count = m_focusTokens->Size;
        for (unsigned int i = 0; i < count; i++) {
            auto btn = m_tabButtons->GetAt(i);
            auto clickTokenVal = m_clickTokens->GetAt(i);
            auto focusTokenVal = m_focusTokens->GetAt(i);
            if (btn != nullptr) {
                Windows::Foundation::EventRegistrationToken clickToken{ clickTokenVal };
                Windows::Foundation::EventRegistrationToken focusToken{ focusTokenVal };
                btn->Click -= clickToken;
                btn->GotFocus -= focusToken;
            }
        }
    }
    if (m_tabButtons != nullptr) m_tabButtons->Clear();
    if (m_clickTokens != nullptr) m_clickTokens->Clear();
    if (m_focusTokens != nullptr) m_focusTokens->Clear();
    m_selectedButton = nullptr;
}

// Helper to search for an element by name in the LeftContent visual tree
static UIElement^ FindElementByNameRecursive(UIElement^ root, Platform::String^ name)
{
    auto fe = dynamic_cast<FrameworkElement^>(root);
    if (fe == nullptr) return nullptr;
    if (fe->Name == name) return fe;
    int count = VisualTreeHelper::GetChildrenCount(fe);
    for (int i = 0; i < count; i++) {
        auto child = safe_cast<UIElement^>(VisualTreeHelper::GetChild(fe, i));
        auto res = FindElementByNameRecursive(child, name);
        if (res != nullptr) return res;
    }
    return nullptr;
}

void TabsLayout::OnTabButtonClick(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    auto btn = dynamic_cast<Button^>(safe_cast<UIElement^>(sender));
    if (btn == nullptr) return;
    auto direct = GetTargetPanel(btn);
    if (direct != nullptr) {
        // If direct panel reference provided, try to find its name to call SelectTabByName
        auto fe = dynamic_cast<FrameworkElement^>(direct);
        if (fe != nullptr && fe->Name != nullptr && fe->Name->Length() > 0) {
            SelectTabByName(fe->Name);
            return;
        }
        // As fallback, make direct visible and collapse siblings
        direct->Visibility = Windows::UI::Xaml::Visibility::Visible;
        return;
    }
    auto name = GetTargetPanelName(btn);
    if (name != nullptr) {
        SelectTabByName(name);
    }
}

void TabsLayout::OnTabButtonGotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    // Mirror click behavior on focus
    OnTabButtonClick(sender, e);
}

void TabsLayout::SelectTabByName(Platform::String^ panelName)
{
    if (panelName == nullptr) return;

    // Find left content root
    auto left = this->LeftContent;
    if (left == nullptr) return;

    // Try to find by name in the visual tree of left content
    auto target = FindElementByNameRecursive(left, panelName);
    if (target == nullptr) return;

    // Collapse all sibling panels under the same parent (LeftContent logical children)
    auto parent = dynamic_cast<Panel^>(safe_cast<FrameworkElement^>(left));
    if (parent != nullptr) {
        for (unsigned int i = 0; i < parent->Children->Size; i++) {
            auto child = parent->Children->GetAt(i);
            if (child == target) {
                child->Visibility = Windows::UI::Xaml::Visibility::Visible;
                // Try to mark the corresponding button as selected (bold)
                for (unsigned int b = 0; b < m_tabButtons->Size; b++) {
                    auto btn = m_tabButtons->GetAt(b);
                    auto direct = GetTargetPanel(btn);
                    auto name = GetTargetPanelName(btn);
                    bool match = false;
                    if (direct != nullptr) {
                        match = (direct == target);
                    }
                    else if (name != nullptr) {
                        match = (name == panelName);
                    }
                    if (match) {
                        if (m_selectedButton != nullptr) {
                            // unset previous selection
                            TabsLayout::SetIsSelected(m_selectedButton, false);
                        }
                        // mark this button selected
                        TabsLayout::SetIsSelected(btn, true);
                        m_selectedButton = btn;
                        break;
                    }
                }
            }
            else {
                child->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
            }
        }
    }
    else {
        // If LeftContent isn't a Panel, just set visibility on the found element
        auto fe = dynamic_cast<FrameworkElement^>(target);
        if (fe != nullptr) fe->Visibility = Windows::UI::Xaml::Visibility::Visible;
    }
}
