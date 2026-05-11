#include "pch.h"
#include "Converters\WindowHeightConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml;

Object^ WindowHeightConverter::Convert(Object^ value, TypeName targetType, Object^ parameter, String^ language)
{
    double height = Window::Current->Bounds.Height;

    double p = 1.0;
    if (parameter != nullptr) {
        try { p = std::stod(std::wstring(static_cast<String^>(parameter)->Data())); }
        catch (...) { p = 1.0; }
    }

    return ref new Platform::Box<double>(height * p);
}

Object^ WindowHeightConverter::ConvertBack(Object^, TypeName, Object^, String^)
{
    return nullptr;
}
