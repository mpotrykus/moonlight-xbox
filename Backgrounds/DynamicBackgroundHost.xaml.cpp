#include "pch.h"
#include "Backgrounds\DynamicBackgroundHost.xaml.h"
#include "Backgrounds\BackgroundRegistry.h"
#include "Backgrounds\NoneBackground.xaml.h"
#include "Backgrounds\GradientBackground.xaml.h"
#include "Backgrounds\ParticleBackground.xaml.h"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::Storage;

static void TryStartAnimations(UIElement^ el)
{
    if (auto g = dynamic_cast<GradientBackground^>(el))  { g->StartAnimations(); return; }
    if (auto p = dynamic_cast<ParticleBackground^>(el))  { p->StartAnimations(); return; }
}

static void TryStopAnimations(UIElement^ el)
{
    if (auto g = dynamic_cast<GradientBackground^>(el))  { g->StopAnimations(); return; }
    if (auto p = dynamic_cast<ParticleBackground^>(el))  { p->StopAnimations(); return; }
}

static UIElement^ CreateBackground(String^ key)
{
    if (key != nullptr) {
        if (key->Equals(ref new String(L"gradient")))   return ref new GradientBackground();
        if (key->Equals(ref new String(L"particles")))  return ref new ParticleBackground();
    }
    return ref new NoneBackground();
}

DynamicBackgroundHost::DynamicBackgroundHost()
{
    InitializeComponent();
    this->Loaded += ref new RoutedEventHandler(this, &DynamicBackgroundHost::OnLoaded);
}

void DynamicBackgroundHost::OnLoaded(Object^ sender, RoutedEventArgs^ e)
{
    Refresh();
}

void DynamicBackgroundHost::Refresh()
{
    try {
        auto localSettings = ApplicationData::Current->LocalSettings->Values;
        String^ key = L"none";
        if (localSettings->HasKey("background")) {
            key = safe_cast<String^>(localSettings->Lookup("background"));
        }

        if (m_currentKey != nullptr && m_currentKey->Equals(key)) return;

        auto oldContent = dynamic_cast<UIElement^>(BackgroundPresenter->Content);
        if (oldContent != nullptr) {
            TryStopAnimations(oldContent);
        }

        BackgroundPresenter->Content = CreateBackground(key);
        m_currentKey = key;
    } catch (...) {}
}

void DynamicBackgroundHost::StartAnimations()
{
    try {
        auto el = dynamic_cast<UIElement^>(BackgroundPresenter->Content);
        if (el != nullptr) TryStartAnimations(el);
    } catch (...) {}
}

void DynamicBackgroundHost::StopAnimations()
{
    try {
        auto el = dynamic_cast<UIElement^>(BackgroundPresenter->Content);
        if (el != nullptr) TryStopAnimations(el);
    } catch (...) {}
}
