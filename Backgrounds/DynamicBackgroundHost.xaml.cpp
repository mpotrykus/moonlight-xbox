#include "pch.h"
#include "Backgrounds\DynamicBackgroundHost.xaml.h"
#include "Backgrounds\BackgroundRegistry.h"
#include "Backgrounds\NoneBackground.xaml.h"
#include "Backgrounds\GradientBackground.xaml.h"
#include "Backgrounds\ParticleBackground.xaml.h"
#include "Backgrounds\BubblesBackground.xaml.h"
#include "Backgrounds\SpheresBackground.xaml.h"
#include "Backgrounds\StreaksBackground.xaml.h"
#include "Backgrounds\WavyLinesBackground.xaml.h"
#include "Backgrounds\AppGridBackground.xaml.h"
#include "Backgrounds\CornerBloomBackground.xaml.h"
#include "Backgrounds\BlobsBackground.xaml.h"
#include "Backgrounds\MoonBackground.xaml.h"
#include "Backgrounds\PaperCutBackground.xaml.h"
#include "Backgrounds\SwipeRevealBackground.xaml.h"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::Storage;

static void TryStartAnimations(UIElement^ el)
{
    if (auto g = dynamic_cast<GradientBackground^>(el))   { g->StartAnimations(); return; }
    if (auto p = dynamic_cast<ParticleBackground^>(el))   { p->StartAnimations(); return; }
    if (auto b = dynamic_cast<BubblesBackground^>(el))    { b->StartAnimations(); return; }
    if (auto s = dynamic_cast<SpheresBackground^>(el))    { s->StartAnimations(); return; }
    if (auto k = dynamic_cast<StreaksBackground^>(el))    { k->StartAnimations(); return; }
    if (auto w = dynamic_cast<WavyLinesBackground^>(el))  { w->StartAnimations(); return; }
    if (auto a = dynamic_cast<AppGridBackground^>(el))    { a->StartAnimations(); return; }
    if (auto c = dynamic_cast<CornerBloomBackground^>(el)) { c->StartAnimations(); return; }
    if (auto l = dynamic_cast<BlobsBackground^>(el))       { l->StartAnimations(); return; }
    if (auto m = dynamic_cast<MoonBackground^>(el))         { m->StartAnimations(); return; }
    if (auto pc = dynamic_cast<PaperCutBackground^>(el))   { pc->StartAnimations(); return; }
    if (auto sr = dynamic_cast<SwipeRevealBackground^>(el)) { sr->StartAnimations(); return; }
}

static void TryStopAnimations(UIElement^ el)
{
    if (auto g = dynamic_cast<GradientBackground^>(el))   { g->StopAnimations(); return; }
    if (auto p = dynamic_cast<ParticleBackground^>(el))   { p->StopAnimations(); return; }
    if (auto b = dynamic_cast<BubblesBackground^>(el))    { b->StopAnimations(); return; }
    if (auto s = dynamic_cast<SpheresBackground^>(el))    { s->StopAnimations(); return; }
    if (auto k = dynamic_cast<StreaksBackground^>(el))    { k->StopAnimations(); return; }
    if (auto w = dynamic_cast<WavyLinesBackground^>(el))  { w->StopAnimations(); return; }
    if (auto a = dynamic_cast<AppGridBackground^>(el))    { a->StopAnimations(); return; }
    if (auto c = dynamic_cast<CornerBloomBackground^>(el)) { c->StopAnimations(); return; }
    if (auto l = dynamic_cast<BlobsBackground^>(el))       { l->StopAnimations(); return; }
    if (auto m = dynamic_cast<MoonBackground^>(el))         { m->StopAnimations(); return; }
    if (auto pc = dynamic_cast<PaperCutBackground^>(el))   { pc->StopAnimations(); return; }
    if (auto sr = dynamic_cast<SwipeRevealBackground^>(el)) { sr->StopAnimations(); return; }
}

static UIElement^ CreateBackground(String^ key)
{
    if (key != nullptr) {
        if (key->Equals(ref new String(L"gradient")))   return ref new GradientBackground();
        if (key->Equals(ref new String(L"particles")))  return ref new ParticleBackground();
        if (key->Equals(ref new String(L"bubbles")))    return ref new BubblesBackground();
        if (key->Equals(ref new String(L"spheres")))    return ref new SpheresBackground();
        if (key->Equals(ref new String(L"streaks")))    return ref new StreaksBackground();
        if (key->Equals(ref new String(L"wavylines")))  return ref new WavyLinesBackground();
        if (key->Equals(ref new String(L"appgrid")))    return ref new AppGridBackground();
        if (key->Equals(ref new String(L"cornerboom"))) return ref new CornerBloomBackground();
        if (key->Equals(ref new String(L"blobs")))      return ref new BlobsBackground();
        if (key->Equals(ref new String(L"moonnight")))  return ref new MoonBackground();
        if (key->Equals(ref new String(L"papercut")))    return ref new PaperCutBackground();
        if (key->Equals(ref new String(L"swipereveal"))) return ref new SwipeRevealBackground();
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

        auto newBg = CreateBackground(key);
        if (auto a = dynamic_cast<AppGridBackground^>(newBg))     { a->SetHosts(m_hosts); }
        if (auto c = dynamic_cast<CornerBloomBackground^>(newBg))  { c->SetHosts(m_hosts); }
        if (auto sr = dynamic_cast<SwipeRevealBackground^>(newBg)) { sr->SetHosts(m_hosts); }
        BackgroundPresenter->Content = newBg;
        m_currentKey = key;
    } catch (...) {}
}

void DynamicBackgroundHost::SetHosts(Windows::Foundation::Collections::IVector<MoonlightHost^>^ hosts)
{
    m_hosts = hosts;
    try {
        auto el = dynamic_cast<UIElement^>(BackgroundPresenter->Content);
        if (auto a = dynamic_cast<AppGridBackground^>(el))     { a->SetHosts(hosts); }
        if (auto c = dynamic_cast<CornerBloomBackground^>(el))  { c->SetHosts(hosts); }
        if (auto sr = dynamic_cast<SwipeRevealBackground^>(el)) { sr->SetHosts(hosts); }
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

void DynamicBackgroundHost::ResetBackground()
{
    try {
        auto el = dynamic_cast<UIElement^>(BackgroundPresenter->Content);
        if (auto c = dynamic_cast<CornerBloomBackground^>(el)) { c->ResetAndReload(); }
    } catch (...) {}
}
