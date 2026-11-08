#include <unknwn.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "App.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Application::Start([](auto&&)
    {
        static auto app = make<winrt::Blink::WinUiSmoke::implementation::App>();
        (void)app;
    });
    return 0;
}
