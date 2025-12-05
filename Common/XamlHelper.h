#pragma once
#include "pch.h"
#include <ppltasks.h>

namespace moonlight_xbox_dx {
    namespace XamlHelper {
        // Asynchronously load a XAML file from the app package (ms-appx:/// relative path)
        // and return its contents as a Platform::String^.
        concurrency::task<Platform::String^> LoadXamlFileAsStringAsync(Platform::String^ relativePath);
    }
}
