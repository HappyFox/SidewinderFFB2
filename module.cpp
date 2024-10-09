#include <pybind11/pybind11.h>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")



#include <Windows.h>

#include <basetsd.h>
#include <dinput.h>
#include <dinputd.h>



#include <string>
#include <stdexcept>
#include <tuple>
#include <list>
#include <strsafe.h>
#include <iostream>

#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h> 
#include <pybind11/chrono.h>



using namespace pybind11::literals;
namespace py = pybind11;


extern "C" {
    IMAGE_DOS_HEADER __ImageBase;
}


HMODULE GetCurrentModuleHandle() {
    HMODULE ImageBase;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetCurrentModuleHandle,
        &ImageBase)) {
        return ImageBase;
    }
    return 0;
}

#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase);

#define SAFE_DELETE(p)  { if(p) { delete (p);     (p)=NULL; } }
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }

LPDIRECTINPUT8          g_pDI = NULL;
LPDIRECTINPUTDEVICE8    g_pJoystick = NULL;

HWND hwnd = nullptr;


typedef std::tuple <bool, bool, bool, bool, bool, bool, bool, bool> buttons;

void acquire();
void reset_ffb();

struct DI_ENUM_CONTEXT
{
    DIJOYCONFIG* pPreferredJoyCfg;
    bool bPreferredJoyCfgValid;
};


struct _JoyState {
    _JoyState(
        const long x,
        const long y,
        const long Rz,
        const long throttle,
        py::object buttons,
        py::object pov
    ) : x(x), y(y), Rz(Rz), throttle(throttle), buttons(buttons), pov(pov) { }
    const long x, y, Rz, throttle;
    py::object buttons, pov;
};


//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string GetLastErrorAsString()
{
    //Get the error message ID, if any.
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0) {
        return std::string(); //No error message has been recorded
    }

    LPSTR messageBuffer = nullptr;

    //Ask Win32 to give us the string version of that message ID.
    //The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    //Copy the error message into a std::string.
    std::string message(messageBuffer, size);

    //Free the Win32's string's buffer.
    LocalFree(messageBuffer);

    return message;
}


BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance,
    VOID* pContext)
{
    DI_ENUM_CONTEXT* pEnumContext = (DI_ENUM_CONTEXT*)pContext;
    HRESULT hr;


    // Skip anything other than the perferred joystick device as defined by the control panel.  
    // Instead you could store all the enumerated joysticks and let the user pick.
    if (pEnumContext->bPreferredJoyCfgValid &&
        !IsEqualGUID(pdidInstance->guidInstance, pEnumContext->pPreferredJoyCfg->guidInstance))
        return DIENUM_CONTINUE;

    // Obtain an interface to the enumerated joystick.
    hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, NULL);

    // If it failed, then we can't use this joystick. (Maybe the user unplugged
    // it while we were in the middle of enumerating it.)
    if (FAILED(hr))
        return DIENUM_CONTINUE;

    // Stop enumeration. Note: we're just taking the first joystick we get. You
    // could store all the enumerated joysticks and let the user pick.
    return DIENUM_STOP;
}

void init() {
    HRESULT hr;
    const wchar_t CLASS_NAME[] = L"SidewinderFFB2 Message Window Class";
    WNDCLASSEX wx = {};
    wx.cbSize = sizeof(WNDCLASSEX);
    wx.lpfnWndProc = DefWindowProc;
    //wx.hInstance = HINST_THISCOMPONENT;
    wx.hInstance = GetCurrentModuleHandle();
    wx.lpszClassName = (LPCSTR)CLASS_NAME;

    if (RegisterClassEx(&wx)) {
        hwnd = CreateWindowEx(0, (LPCSTR)CLASS_NAME, (LPCSTR)"dummy_name", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    }

    if (NULL == hwnd) {
        std::string err_str = "Unable to Start Message window: " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    if (FAILED(hr = DirectInput8Create(GetModuleHandle(NULL), 0x0800, IID_IDirectInput8, (VOID**)&g_pDI, NULL))) {
        std::string err_str = "Unable to Start Message window: " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    DIJOYCONFIG PreferredJoyCfg = { 0 };
    DI_ENUM_CONTEXT enumContext;
    enumContext.pPreferredJoyCfg = &PreferredJoyCfg;
    enumContext.bPreferredJoyCfgValid = false;

    IDirectInputJoyConfig8* pJoyConfig = NULL;
    if (FAILED(hr = g_pDI->QueryInterface(IID_IDirectInputJoyConfig8, (void**)&pJoyConfig))) {
        std::string err_str = "Unable to query joysticks :" + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    PreferredJoyCfg.dwSize = sizeof(PreferredJoyCfg);
    if (SUCCEEDED(pJoyConfig->GetConfig(0, &PreferredJoyCfg, DIJC_GUIDINSTANCE))) // This function is expected to fail if no joystick is attached
        enumContext.bPreferredJoyCfgValid = true;
    else {
        std::string err_str = "Unable to get Joystick config : " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }
    SAFE_RELEASE(pJoyConfig);

    if (FAILED(hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, &enumContext, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK))) {
        std::string err_str = "Unable to find a joystick with force feedback: " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    if (NULL == g_pJoystick) {
        std::string err_str = "Unable to find a joystick with force feedback: " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    if (FAILED(hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2))) {
        std::string err_str = "Unable to find a joystick with force feedback: " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }
    if (FAILED(hr = g_pJoystick->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND))) {
        std::string err_str = "Unable to set the Cooperative level to exclusive + background. :" + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }

    acquire();
    reset_ffb();
}



void acquire() {
    HRESULT hr;
    if (NULL == g_pJoystick) {
        throw std::exception("Not initialized.");
    }

    if (FAILED(hr = g_pJoystick->Acquire())) {
        std::string err_str = "Runtime error acquiring joystick. : " + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }
}

void reset_ffb() {
    HRESULT hr;

    if (NULL == g_pJoystick) {
        throw std::exception("Not initialized.");
    }

    if (FAILED(hr = g_pJoystick->SendForceFeedbackCommand(DISFFC_RESET))) {
        std::string err_str = "Unable to reset the joystick :" + GetLastErrorAsString();
        throw std::exception(err_str.c_str());
    }
}

py::object build_py_joy_state(DIJOYSTATE2 js) {
    py::object SidewinderFFB2 = py::module::import("SidewinderFFB2");
    py::object joy_state = SidewinderFFB2.attr("JoyState");

    unsigned char pressed = 128;

    py::tuple buttons = py::make_tuple(
        (bool)js.rgbButtons[0] && pressed,
        (bool)js.rgbButtons[1] && pressed,
        (bool)js.rgbButtons[2] && pressed,
        (bool)js.rgbButtons[3] && pressed,
        (bool)js.rgbButtons[4] && pressed,
        (bool)js.rgbButtons[5] && pressed,
        (bool)js.rgbButtons[6] && pressed,
        (bool)js.rgbButtons[7] && pressed
    );

    py::object pov = py::none();
    if (!(LOWORD(js.rgdwPOV[0]) == 0xFFFF)) {
        pov = py::int_(js.rgdwPOV[0]);
    }

    return joy_state(js.lX, js.lY, js.lRz, js.rglSlider[0], buttons, pov);
}


py::object poll() {
    HRESULT hr;
    DIJOYSTATE2 js;

    if (NULL == g_pJoystick) {
        throw std::exception("Not initialized.");
    }

    while (true) {
        hr = g_pJoystick->Poll();
        if (FAILED(hr))
        {
            // DInput is telling us that the input stream has been
            // interrupted. We aren't tracking any state between polls, so
            // we don't have any special reset that needs to be done. We
            // just re-acquire and try again.s
            hr = g_pJoystick->Acquire();
            while (hr == DIERR_INPUTLOST) {
                hr = g_pJoystick->Acquire();
            }
            continue;
        }
        // Get the input's device state
        if (FAILED(hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js))) {
            continue;
        }

        return build_py_joy_state(js);
    }
}


void release() {
    if (g_pJoystick) {
        g_pJoystick->Unacquire();
    }

    // Release any DirectInput objects.

    SAFE_RELEASE(g_pJoystick);
    SAFE_RELEASE(g_pDI);
}


class _ConstantForce {
public:
    _ConstantForce() {
        HRESULT hr;
        DWORD rgdwAxes[2] = { DIJOFS_X, DIJOFS_Y };
        LONG rglDirection[2] = { 0, 0 };

        DICONSTANTFORCE di_cf;
        DIEFFECT eff;

        di_cf.lMagnitude = 0;
        ZeroMemory(&eff, sizeof(eff));

        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = INFINITE;
        eff.dwSamplePeriod = 0;
        eff.dwGain = DI_FFNOMINALMAX;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.dwTriggerRepeatInterval = 0;
        eff.cAxes = 2;
        eff.rgdwAxes = rgdwAxes;
        eff.rglDirection = rglDirection;
        eff.lpEnvelope = 0;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &di_cf;
        eff.dwStartDelay = 0;

        if (FAILED(hr = g_pJoystick->CreateEffect(GUID_ConstantForce, &eff, &this->pdiEffect, NULL))) {
            std::string err_str = "Unable to set the Cooperative level to exclusive + background. :" + GetLastErrorAsString();
            throw std::exception(err_str.c_str());
        }

        this->pdiEffect->Start(1, 0);
    };

    ~_ConstantForce() {
        // Yes,  I am going to ignore any errors, this is best effort at this point.
        this->pdiEffect->Unload();
    }


    void set_direction(LONG x, LONG y) {
        HRESULT hr;

        DICONSTANTFORCE cf;

        LONG rglDirection[2] = { x, y };

        DIEFFECT eff;


        if (DI_FFNOMINALMAX < x || -DI_FFNOMINALMAX > x) {
            std::string err_str = "X of " + std::to_string(x) + "is out of range 10000 to - 10000";
            throw std::exception(err_str.c_str());
        };

        if (DI_FFNOMINALMAX < y || -DI_FFNOMINALMAX > y) {
            std::string err_str = "Y of " + std::to_string(y) + " is out of range 10000 to - 10000";
            throw std::exception(err_str.c_str());
        };

        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.cAxes = 2;
        eff.rglDirection = rglDirection;
        eff.lpEnvelope = 0;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;
        eff.dwStartDelay = 0;
        cf.lMagnitude = (DWORD)sqrt((double)x * (double)x + (double)y * (double)y);
        if (FAILED(hr = this->pdiEffect->SetParameters(&eff, DIEP_DIRECTION |
            DIEP_TYPESPECIFICPARAMS |
            DIEP_START))) {
            std::string err_str = "Unable to set the parameters on effect : " + GetLastErrorAsString();
            throw std::exception(err_str.c_str());
        }
    }

    void set_gain(DWORD gain) {
        HRESULT hr;

        DIEFFECT eff;

        if (DI_FFNOMINALMAX < gain || 0 > gain) {
            std::string err_str = "Gain of " + std::to_string(gain) + " is out of range 10000 to 0";
            throw std::exception(err_str.c_str());
        };

        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwGain = gain;
        if (FAILED(hr = this->pdiEffect->SetParameters(&eff, DIEP_GAIN | DIEP_START))) {
            std::string err_str = "Unable to set the gain on effect : " + GetLastErrorAsString();
            throw std::exception(err_str.c_str());
        }
    }

    LPDIRECTINPUTEFFECT  pdiEffect;

};


class _BuzzForce {
public:
    _BuzzForce() {

        HRESULT hr;

        DIPERIODIC di_period;
        DIEFFECT eff;

        DWORD      dwAxes[2] = { DIJOFS_X, DIJOFS_Y };
        LONG       lDirection[2] = { 1000, 1000 };

        di_period.dwPeriod = DI_FFNOMINALMAX;
        di_period.dwMagnitude = DI_FFNOMINALMAX;
        di_period.dwPhase = 0;
        di_period.lOffset = 0;

        ZeroMemory(&eff, sizeof(eff));

        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = (DWORD)(0.1 * DI_SECONDS);;
        eff.dwSamplePeriod = 0;
        eff.dwGain = DI_FFNOMINALMAX;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.dwTriggerRepeatInterval = 0;
        eff.cAxes = 2;
        eff.rgdwAxes = dwAxes;
        eff.rglDirection = &lDirection[0];
        eff.lpEnvelope = 0;
        eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
        eff.lpvTypeSpecificParams = &di_period;
        eff.dwStartDelay = 0;

        if (FAILED(hr = g_pJoystick->CreateEffect(GUID_Sine, &eff, &this->pdiEffect, NULL))) {
            std::string err_str = "Unable to create an effect : " + GetLastErrorAsString();
            throw std::exception(err_str.c_str());
        }
    };

    ~_BuzzForce() {
        // Yes,  I am going to ignore any errors, this is best effort at this point.
        this->pdiEffect->Unload();
    }

    void start() {
        HRESULT hr;
        if (FAILED(hr = this->pdiEffect->Start(1, 0))) {
            std::string err_str = "Unable to start the buzz effect : " + GetLastErrorAsString();
            throw std::exception(err_str.c_str());
        };
    };

    LPDIRECTINPUTEFFECT  pdiEffect;
};


PYBIND11_MODULE(SidewinderFFB2, m) {
    py::class_<_JoyState>(m, "JoyState")
        .def(py::init<const long, const long, const long, const long, py::object, py::object>())
        .def_readonly("x", &_JoyState::x)
        .def_readonly("y", &_JoyState::y)
        .def_readonly("r_z", &_JoyState::Rz)
        .def_readonly("throttle", &_JoyState::throttle)
        .def_readonly("buttons", &_JoyState::buttons)
        .def_readonly("pov", &_JoyState::pov)
        .def("__repr__",
            [](const _JoyState& a) {

                return "<JoyState: '" + std::to_string(a.x) + ", " + std::to_string(a.y) + ", " + std::to_string(a.Rz) + ", " + std::to_string(a.throttle) + ", " + py::repr(a.buttons).cast<std::string>() + "," + py::repr(a.pov).cast<std::string>() + "'>";
            });

    py::class_<_ConstantForce>(m, "ConstantForce")
        .def(py::init<>())
        .def("set_direction", &_ConstantForce::set_direction)
        .def("set_gain", &_ConstantForce::set_gain);

    py::class_<_BuzzForce>(m, "BuzzForce")
        .def(py::init<>())
        .def("start", &_BuzzForce::start);

    m.def("init", &init, R"pbdoc(
        Initialize the joystick.
    )pbdoc");
    m.def("poll", &poll, R"pbdoc(
        Poll and return the axis values.
    )pbdoc");
    m.def("acquire", &acquire, R"pbdoc(
        Acquire the joystick, exception on failure.
    )pbdoc");
    m.def("release", &release, R"pbdoc(
        Release the Joystick.
    )pbdoc");

    m.def("reset", &reset_ffb, R"pbdoc(
        Reset the force feedback system.
    )pbdoc");

    m.attr("AXIS_X") = py::int_(DIJOFS_X);
    m.attr("AXIS_Y") = py::int_(DIJOFS_Y);
    m.attr("DI_FFNOMINALMAX") = py::int_(DI_FFNOMINALMAX);

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}
