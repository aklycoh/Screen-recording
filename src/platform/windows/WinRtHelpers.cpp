#include "platform/windows/WinRtHelpers.h"

#include <Windows.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

GraphicsCaptureItem createCaptureItemForWindow(quintptr nativeHandle)
{
    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item {nullptr};
    check_hresult(interop->CreateForWindow(
        reinterpret_cast<HWND>(nativeHandle),
        guid_of<GraphicsCaptureItem>(),
        reinterpret_cast<void**>(put_abi(item))));
    return item;
}

GraphicsCaptureItem createCaptureItemForMonitor(quintptr nativeHandle)
{
    auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item {nullptr};
    check_hresult(interop->CreateForMonitor(
        reinterpret_cast<HMONITOR>(nativeHandle),
        guid_of<GraphicsCaptureItem>(),
        reinterpret_cast<void**>(put_abi(item))));
    return item;
}

IDirect3DDevice createDirect3DDevice(ID3D11Device* device)
{
    com_ptr<IDXGIDevice> dxgiDevice;
    check_hresult(device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void()));

    IDirect3DDevice winrtDevice {nullptr};
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
        dxgiDevice.get(),
        reinterpret_cast<IInspectable**>(put_abi(winrtDevice))));
    return winrtDevice;
}

IDirect3DSurface createDirect3DSurface(ID3D11Texture2D* texture)
{
    com_ptr<IDXGISurface> dxgiSurface;
    check_hresult(texture->QueryInterface(__uuidof(IDXGISurface), dxgiSurface.put_void()));

    IDirect3DSurface winrtSurface {nullptr};
    check_hresult(CreateDirect3D11SurfaceFromDXGISurface(
        dxgiSurface.get(),
        reinterpret_cast<IInspectable**>(put_abi(winrtSurface))));
    return winrtSurface;
}

QSize toQSize(const winrt::Windows::Graphics::SizeInt32& size)
{
    return {size.Width, size.Height};
}
