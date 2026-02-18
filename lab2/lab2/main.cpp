#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")


ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;

int g_ClientWidth = 800;
int g_ClientHeight = 600;

// Vertex shader
const char* g_VSShaderCode = R"(
struct VSInput {
    float3 pos : POSITION;
    float4 color : COLOR;
};
struct VSOutput {
    float4 pos : SV_Position;
    float4 color : COLOR;
};
VSOutput vs(VSInput vertex) {
    VSOutput result;
    result.pos = float4(vertex.pos, 1.0);
    result.color = vertex.color;
    return result;
}
)";

// pixel shader
const char* g_PSShaderCode = R"(
struct VSOutput {
    float4 pos : SV_Position;
    float4 color : COLOR;
};
float4 ps(VSOutput pixel) : SV_Target0 {
    return pixel.color;
}
)";

struct Vertex
{
    float x, y, z;
    uint32_t color;        
};


static const Vertex Vertices[] = {
    { -0.5f, -0.5f, 0.0f, 0x00FF0000 }, // red
    {  0.5f, -0.5f, 0.0f, 0x0000FF00 }, // green
    {  0.0f,  0.5f, 0.0f, 0x000000FF }  // blue
};

static const uint16_t Indices[] = { 0, 2, 1 };

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }


ID3DBlob* CompileShader(const char* shaderSource, const char* entryPoint, const char* target, const char* debugName)
{
    UINT flags1 = 0;
#ifdef _DEBUG
    flags1 |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* pCode = nullptr;
    ID3DBlob* pErrMsg = nullptr;
    HRESULT hr = D3DCompile(shaderSource, strlen(shaderSource),
        debugName, nullptr, nullptr,
        entryPoint, target, flags1, 0,
        &pCode, &pErrMsg);
    if (FAILED(hr))
    {
        if (pErrMsg)
        {
            OutputDebugStringA((char*)pErrMsg->GetBufferPointer());
            MessageBoxA(nullptr, (char*)pErrMsg->GetBufferPointer(), "Shader Compilation Error", MB_OK);
            SAFE_RELEASE(pErrMsg);
        }
        return nullptr;
    }
    return pCode;
}

HRESULT InitD3D(HWND hWnd)
{
    HRESULT hr = S_OK;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = g_ClientWidth;
    scd.BufferDesc.Height = g_ClientHeight;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL outFeatureLevel;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                          
        D3D_DRIVER_TYPE_WARP,
        nullptr,                          
        createDeviceFlags,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &scd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &outFeatureLevel,
        &g_pd3dDeviceContext
    );
    if (FAILED(hr)) 
        return hr;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    SAFE_RELEASE(pBackBuffer);
    if (FAILED(hr)) 
        return hr;

    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(Vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;          
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = Vertices;

    hr = g_pd3dDevice->CreateBuffer(&vbDesc, &vbData, &g_pVertexBuffer);
    if (FAILED(hr)) 
        return hr;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(Indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = Indices;

    hr = g_pd3dDevice->CreateBuffer(&ibDesc, &ibData, &g_pIndexBuffer);
    if (FAILED(hr)) 
        return hr;

    ID3DBlob* pVSBlob = CompileShader(g_VSShaderCode, "vs", "vs_5_0", "triangle.vs");
    if (!pVSBlob) return E_FAIL;
    hr = g_pd3dDevice->CreateVertexShader(pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        nullptr,
        &g_pVertexShader);

    if (FAILED(hr))
    {
        SAFE_RELEASE(pVSBlob);
        return hr;
    }

    ID3DBlob* pPSBlob = CompileShader(g_PSShaderCode, "ps", "ps_5_0", "triangle.ps");
    if (!pPSBlob) return E_FAIL;
    hr = g_pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(),
        pPSBlob->GetBufferSize(),
        nullptr,
        &g_pPixelShader);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
          D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = g_pd3dDevice->CreateInputLayout(layoutDesc, 2,
        pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        &g_pInputLayout);
    SAFE_RELEASE(pVSBlob);
    SAFE_RELEASE(pPSBlob);

    if (FAILED(hr)) 
        return hr;

    return S_OK;
}

void CleanupD3D()
{
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);
    SAFE_RELEASE(g_pRenderTargetView);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pd3dDeviceContext);

    if (g_pd3dDevice)
    {
#ifdef _DEBUG
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
            SAFE_RELEASE(pDebug);
        }
#endif
        SAFE_RELEASE(g_pd3dDevice);
        g_pd3dDevice = nullptr;
    }

#ifdef _DEBUG
    ID3D11Debug* pDebug = nullptr;
    if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
    {
        pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        SAFE_RELEASE(pDebug);
    }
#endif
}

void Render()
{
    const FLOAT clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pd3dDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<FLOAT>(g_ClientWidth);
    viewport.Height = static_cast<FLOAT>(g_ClientHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &viewport);

    D3D11_RECT rect = { 0, 0, g_ClientWidth, g_ClientHeight };
    g_pd3dDeviceContext->RSSetScissorRects(1, &rect);

    ID3D11Buffer* vertexBuffers[] = { g_pVertexBuffer };
    UINT strides[] = { sizeof(Vertex) };     
    UINT offsets[] = { 0 };
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, vertexBuffers, strides, offsets);
    g_pd3dDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);

    g_pd3dDeviceContext->IASetInputLayout(g_pInputLayout);
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_pd3dDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);

    g_pd3dDeviceContext->DrawIndexed(3, 0, 0);

    g_pSwapChain->Present(0, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"TriangleWindowClass";

    if (!RegisterClassEx(&wc))
    {
        MessageBox(nullptr, L"Failed to register window class", L"Error", MB_OK);
        return -1;
    }

    HWND hWnd = CreateWindowEx(
        0,
        L"TriangleWindowClass",
        L"Direct3D 11 - Triangle",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        g_ClientWidth, g_ClientHeight,
        nullptr, nullptr,
        hInstance, nullptr
    );

    if (!hWnd)
    {
        MessageBox(nullptr, L"Couldn't create a window", L"Error", MB_OK);
        return -1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    if (FAILED(InitD3D(hWnd)))
    {
        MessageBox(nullptr, L"Failed to initialize Direct3D", L"Error", MB_OK);
        CleanupD3D();
        return -1;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Render();
        }
    }

    CleanupD3D();

    return static_cast<int>(msg.wParam);
}