#include "pch.h"
#include "Converters\WindowWidthConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml;

Object^ WindowWidthConverter::Convert(Object^ value, TypeName targetType, Object^ parameter, String^ language)
{
    double width = Window::Current->Bounds.Width;

    double p = 1.0;
    if (parameter != nullptr) {
        try { p = std::stod(std::wstring(static_cast<String^>(parameter)->Data())); }
        catch (...) { p = 1.0; }
    }

    return ref new Platform::Box<double>(width * p);
}

Object^ WindowWidthConverter::ConvertBack(Object^, TypeName, Object^, String^)
{
    return nullptr;
}
