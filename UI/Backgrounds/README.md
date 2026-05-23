# Dynamic Backgrounds

This directory contains all animated backgrounds for the moonlight-xbox app. Each background is an
independent C++/CX XAML UserControl that plugs into `DynamicBackgroundHost`.

---

## Architecture

`DynamicBackgroundHost` is a `UserControl` that manages two `ContentPresenter` slots:

- **`BackgroundPresenter`** — the currently visible, stable background.
- **`FadePresenter`** — the incoming background, crossfaded in over 300ms, then promoted to stable.

On load (and when `Refresh()` is called) the host reads
`ApplicationData::Current->LocalSettings->Values["background"]`, calls `CreateBackground(key)` to
instantiate the right class, and fades it in. It routes `StartAnimations()` / `StopAnimations()` to
the active child via `TryStartAnimations` / `TryStopAnimations` — manual `dynamic_cast` chains
because there is **no base class or interface**.

Each background exposes exactly two required public methods:

```cpp
void StartAnimations();
void StopAnimations();
```

Optional public methods (only implement if needed):

```cpp
void ReloadOptions();              // apply updated LocalSettings without recreating canvas
void SetHosts(IVector<MoonlightHost^>^ hosts);  // only if background renders app art
```

---

## Adding a New Background — Step-by-Step

### 1. Register the key

Open `BackgroundRegistry.h` and add one entry to the `kBackgrounds` array:

```cpp
{ L"mykey", L"My Display Name" },
```

`kBackgroundCount` is computed automatically via `sizeof` — do not touch it.

---

### 2. Create the three source files

```
UI\Backgrounds\FooBackground.xaml
UI\Backgrounds\FooBackground.xaml.h
UI\Backgrounds\FooBackground.xaml.cpp
```

See the [canonical templates](#canonical-templates) below.

---

### 3. Wire into DynamicBackgroundHost.xaml.cpp

Add the `#include` at the top:

```cpp
#include "UI\Backgrounds\FooBackground.xaml.h"
```

Add one branch to each of the three static functions:

```cpp
// TryStartAnimations
if (auto f = dynamic_cast<FooBackground^>(el)) { f->StartAnimations(); return; }

// TryStopAnimations
if (auto f = dynamic_cast<FooBackground^>(el)) { f->StopAnimations(); return; }

// CreateBackground
if (key->Equals(ref new String(L"mykey"))) return ref new FooBackground();
```

---

### 4. Register in moonlight-xbox-dx.vcxproj

Add three XML entries (copy the pattern from any existing background):

```xml
<!-- in the ClInclude ItemGroup -->
<ClInclude Include="UI\Backgrounds\FooBackground.xaml.h" />

<!-- in the Page ItemGroup -->
<Page Include="UI\Backgrounds\FooBackground.xaml">
  <SubType>Designer</SubType>
</Page>

<!-- in the ClCompile ItemGroup -->
<ClCompile Include="UI\Backgrounds\FooBackground.xaml.cpp">
  <DependentUpon>FooBackground.xaml</DependentUpon>
</ClCompile>
```

---

### 5. Register in moonlight-xbox-dx.vcxproj.filters

```xml
<ClInclude Include="UI\Backgrounds\FooBackground.xaml.h">
  <Filter>Header Files</Filter>
</ClInclude>

<Page Include="UI\Backgrounds\FooBackground.xaml">
  <Filter>Resource Files</Filter>
</Page>

<ClCompile Include="UI\Backgrounds\FooBackground.xaml.cpp">
  <Filter>Source Files</Filter>
</ClCompile>
```

---

### 6. (Optional) SetHosts — only if your background renders app art

If your background needs the list of `MoonlightHost^` objects (like `SwipeRevealBackground`), add
to **both** `Refresh()` and `SetHosts()` in `DynamicBackgroundHost.xaml.cpp`:

```cpp
if (auto f = dynamic_cast<FooBackground^>(newBg)) { f->SetHosts(m_hosts); }
```

And declare the method on the class:

```cpp
void SetHosts(Windows::Foundation::Collections::IVector<MoonlightHost^>^ hosts);
```

---

### 7. (Optional) Custom options — only if your background exposes user-configurable settings

If users can adjust settings from `HostSettingsPage` (colors, speed, density, etc.), expose a
public reload method on the class and add a branch in the appropriate host dispatch function in
`DynamicBackgroundHost.xaml.cpp`.

See [Adding Custom Options](#adding-custom-options) for the full pattern.

---

## Canonical Templates

### FooBackground.xaml

```xml
<UserControl
    x:Class="moonlight_xbox_dx.FooBackground"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:d="http://schemas.microsoft.com/expression/blend/2008"
    xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
    mc:Ignorable="d">
    <Canvas x:Name="FooCanvas"
            HorizontalAlignment="Stretch"
            VerticalAlignment="Stretch"
            SizeChanged="Canvas_SizeChanged">
        <!-- background color / gradient / static visuals go here -->
    </Canvas>
</UserControl>
```

### FooBackground.xaml.h

```cpp
#pragma once
#include "UI\Backgrounds\FooBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct FooState {
    float x, y;
    float vx, vy;
};

public ref class FooBackground sealed {
public:
    FooBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^       m_timer;
    Windows::Foundation::EventRegistrationToken m_tickToken;
    std::vector<FooState> m_items;
    std::mt19937          m_rng;
    float m_canvasW    = 0.0f;
    float m_canvasH    = 0.0f;
    bool  m_initialized = false;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitItems();
};

}
```

### FooBackground.xaml.cpp

```cpp
#include "pch.h"
#include "UI\Backgrounds\FooBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

FooBackground::FooBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;  // 16ms ≈ 60fps; use 33*10000LL for 30fps
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_tickToken = m_timer->Tick += ref new EventHandler<Object^>(this, &FooBackground::OnTick);
}

void FooBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);
    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitItems();
        m_initialized = true;
    }
}

void FooBackground::InitItems()
{
    m_items.clear();
    FooCanvas->Children->Clear();
    // Create shapes, push to m_items, append to FooCanvas->Children in a fixed order.
    // OnTick retrieves children by index — order here must match retrieval order there.
}

void FooBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;
    int count = static_cast<int>(m_items.size());
    for (int i = 0; i < count; ++i) {
        auto& s = m_items[i];
        // update s.x, s.y, etc.
        auto el = safe_cast<Ellipse^>(FooCanvas->Children->GetAt(i));
        Canvas::SetLeft(el, s.x);
        Canvas::SetTop(el,  s.y);
    }
}

void FooBackground::StartAnimations() { if (m_timer) m_timer->Start(); }
void FooBackground::StopAnimations()  { if (m_timer) m_timer->Stop();  }
```

---

## Key Patterns and Invariants

### Timer intervals

| Duration constant | Frame rate | Used by |
|---|---|---|
| `16 * 10000LL` | ~60 fps | Streaks, Spheres, Orbs |
| `33 * 10000LL` | ~30 fps | Particles, GlobeGrid |

### Canvas children index contract

`InitItems()` appends children in a **fixed, known order**. `OnTick` retrieves them by index via
`safe_cast`. Never shuffle or conditionally skip insertions. If each item has multiple visual layers
(e.g., glow + core), push all glows first (indices `0..N-1`) then all cores (indices `N..2N-1`).
See `StreaksBackground` for an example using `kGlowBase` / `kCoreBase` constants.

### SizeChanged / lazy initialization

Canvas dimensions are unknown at construction time. Always gate `InitItems()`:

```cpp
if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
    InitItems();
    m_initialized = true;
}
```

### RNG

```cpp
// Seed in constructor:
m_rng = std::mt19937(std::random_device{}());

// Use:
std::uniform_real_distribution<float> dist(minVal, maxVal);
float val = dist(m_rng);
```

### WinRT color construction

```cpp
ColorHelper::FromArgb(alpha, r, g, b);  // all uint8_t; A, R, G, B order
```

### Color helpers (copy from ParticleBackground.xaml.cpp if needed)

```cpp
// Linear interpolate two Colors
static Color LerpRGB(Color a, Color b, float t, uint8_t alpha = 255);

// Scale (darken) a Color
static Color ScaleRGB(Color c, float s, uint8_t alpha = 255);
```

### C++/CX palette array restrictions

Palette arrays **must be non-const** — `const Windows::UI::Color` fails to compile in C++/CX.
Also do **not** use `ARRAYSIZE` on member arrays; use a literal count instead.

```cpp
// .h
Windows::UI::Color m_palette[4];  // non-const, literal count

// .cpp — OK
int n = 4;
```

---

## Adding Custom Options

Backgrounds can expose user-configurable settings (colors, speed, density, presets, etc.) that are
saved to `LocalSettings` and surfaced in `HostSettingsPage`.

### 1. Persist settings in LocalSettings

Choose a consistent key prefix based on your background's registry key (e.g. `"mykey.speed"`,
`"mykey.scheme"`, `"mykey.custom.0"`). Read and write via:

```cpp
auto localSettings = ApplicationData::Current->LocalSettings->Values;

// write
localSettings->Insert("mykey.speed", dynamic_cast<Object^>(speed));

// read with default
float speed = 1.0f;
if (localSettings->HasKey("mykey.speed"))
    speed = static_cast<float>(safe_cast<double>(localSettings->Lookup("mykey.speed")));
```

For color values, store as `RRGGBB` hex strings and parse on load.

### 2. Add a reload method to the background class

Expose a public method that reads current `LocalSettings` values and applies them **without
recreating the canvas**:

```cpp
// .h
void ReloadOptions();

// .cpp
void FooBackground::ReloadOptions()
{
    LoadOptions();          // re-read LocalSettings into member fields
    ApplyOptionsInPlace();  // update existing canvas elements / timer interval / etc.
}
```

Call `LoadOptions()` at the top of `InitItems()` as well so a cold start picks up saved settings.

### 3. Wire into DynamicBackgroundHost.xaml.cpp

Add a branch in the appropriate dispatch function. For settings that don't require a full
background swap, add to `ReloadBackgroundColors()` (or introduce a new parallel function if your
options go beyond colors):

```cpp
if (auto f = dynamic_cast<FooBackground^>(el)) { f->ReloadOptions(); return; }
```

> **Important:** Never call `Refresh()` to apply option changes. `Refresh()` is a no-op when the
> background key is unchanged. Always go through the dedicated reload path.

### 4. Add UI to HostSettingsPage

All per-background options live **inside the existing BACKGROUND Border group** in
`HostSettingsPage.xaml` — not in a separate section:

- Add a `StackPanel x:Name="FooOptionsPanel" Visibility="Collapsed"` after the background ComboBox row.
- Show/hide it with a helper `UpdateFooOptionsPanelVisibility()` that checks whether the current background key equals `"mykey"`. Call this helper from `OnNavigatedTo` and from `BackgroundSelector_SelectionChanged`.
- Include a Reset button that writes the default values back to `LocalSettings` and calls `ReloadBackgroundColors()`.

### 5. Init guard pattern (HostSettingsPage.xaml.cpp)

Use a `bool m_fooInitialized = false` flag. Set it `true` at the end of your init function.
Gate every `SelectionChanged` / `ValueChanged` handler with:

```cpp
if (!m_fooInitialized) return;
```

This prevents spurious saves when controls have their `SelectedIndex` or value set
programmatically during page initialization.

---

## Files Modified for Every New Background

| File | What to add |
|---|---|
| `UI\Backgrounds\BackgroundRegistry.h` | New `{ L"key", L"Display Name" }` entry |
| `UI\Backgrounds\DynamicBackgroundHost.xaml.cpp` | `#include` + branches in `TryStartAnimations`, `TryStopAnimations`, `CreateBackground` |
| `UI\Backgrounds\FooBackground.xaml` | New file |
| `UI\Backgrounds\FooBackground.xaml.h` | New file |
| `UI\Backgrounds\FooBackground.xaml.cpp` | New file |
| `moonlight-xbox-dx.vcxproj` | `ClInclude` + `Page` + `ClCompile` entries |
| `moonlight-xbox-dx.vcxproj.filters` | `ClInclude` + `Page` + `ClCompile` entries |
