#pragma once

#include <QSize>
#include <QtGlobal>

#include <d3d11.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

winrt::Windows::Graphics::Capture::GraphicsCaptureItem createCaptureItemForWindow(quintptr nativeHandle);
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice createDirect3DDevice(ID3D11Device* device);
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface createDirect3DSurface(ID3D11Texture2D* texture);
QSize toQSize(const winrt::Windows::Graphics::SizeInt32& size);

