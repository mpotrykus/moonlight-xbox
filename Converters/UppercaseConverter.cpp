#include "pch.h"
#include "Converters\UppercaseConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;

Object^ UppercaseConverter::Convert(Object^ value, TypeName, Object^, String^)
{
    auto text = dynamic_cast<String^>(value);
    if (text == nullptr) return value;

    std::wstring s(text->Data());
    std::transform(s.begin(), s.end(), s.begin(), towupper);
    return ref new String(s.c_str());
}

Object^ UppercaseConverter::ConvertBack(Object^ value, TypeName, Object^, String^)
{
    return value;
}
