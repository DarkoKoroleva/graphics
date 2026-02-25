#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

const char* vertexShaderCode = R"(
        cbuffer ModelCB : register(b0) { float4x4 model; }
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VSInput {
            float3 pos : POSITION;
            float4 color : COLOR;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.color = v.color;
            return o;
        }
    )";

const char* pixelShaderCode = R"(
        struct VSOutput {
            float4 pos : SV_Position;
            float4 color : COLOR;
        };
        float4 ps(VSOutput p) : SV_Target0 {
            return p.color;
        }
    )";

HWND g_hMainWindow = nullptr;

ID3D11Device* g_pD3DDevice = nullptr;
ID3D11DeviceContext* g_pD3DContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;

struct VertexData
{
    float x, y, z;
    UINT color;
};

ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

struct ModelConstantBuffer
{
    XMMATRIX model;
};
struct ViewProjConstantBuffer
{
    XMMATRIX vp;
};
ID3D11Buffer* g_pModelCB = nullptr;
ID3D11Buffer* g_pViewProjCB = nullptr;

UINT g_WindowWidth = 1280;
UINT g_WindowHeight = 720;

float g_CamYaw = 0.0f;       
float g_CamPitch = 0.3f;        
float g_CamDistance = 3.0f;    
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;

double g_LastFrameTime = 0.0;

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }


LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitializeDirect3D();
void CreateCubeBuffers();
void CompileShaders();
void CleanupDirect3D();
void Render();
void ResizeWindow(UINT newWidth, UINT newHeight);
void UpdateCamera(double deltaTime);

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
    _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11CubeClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Не удалось зарегистрировать класс окна", L"Ошибка", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_WindowWidth, (LONG)g_WindowHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hMainWindow = CreateWindowW(wc.lpszClassName, L"Лабораторная работа 4 - Куб",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWindow)
    {
        MessageBoxW(nullptr, L"Не удалось создать окно", L"Ошибка", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hMainWindow, nCmdShow);
    UpdateWindow(g_hMainWindow);

    if (!InitializeDirect3D())
    {
        CleanupDirect3D();
        DestroyWindow(g_hMainWindow);
        return -1;
    }

    CreateCubeBuffers();
    CompileShaders();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelConstantBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pModelCB);

    desc.ByteWidth = sizeof(ViewProjConstantBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pViewProjCB);

    g_LastFrameTime = (double)GetTickCount64() / 1000.0;

    MSG msg = {};
    bool done = false;
    while (!done)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!done)
        {
            Render();
        }
    }

    CleanupDirect3D();
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
                ResizeWindow(newW, newH);
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_LEFT)  g_KeyLeft = true;
        if (wParam == VK_RIGHT) g_KeyRight = true;
        if (wParam == VK_UP)    g_KeyUp = true;
        if (wParam == VK_DOWN)  g_KeyDown = true;
        return 0;

    case WM_KEYUP:
        if (wParam == VK_LEFT)  g_KeyLeft = false;
        if (wParam == VK_RIGHT) g_KeyRight = false;
        if (wParam == VK_UP)    g_KeyUp = false;
        if (wParam == VK_DOWN)  g_KeyDown = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

bool InitializeDirect3D()
{
    HRESULT hr;

    UINT flags = 0;
//#ifdef _DEBUG
//    flags |= D3D11_CREATE_DEVICE_DEBUG;
//#endif

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_WindowWidth;
    scd.BufferDesc.Height = g_WindowHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hMainWindow;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &scd, &g_pSwapChain, &g_pD3DDevice, &obtainedLevel, &g_pD3DContext
    );

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return false;

    hr = g_pD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) 
        return false;

    return true;
}

void CreateCubeBuffers()
{
    const VertexData vertices[] = {
        {-0.5f, -0.5f, -0.5f, RGB(255,0,0)},
        { 0.5f, -0.5f, -0.5f, RGB(0,255,0)}, 
        { 0.5f,  0.5f, -0.5f, RGB(0,0,255)},  
        {-0.5f,  0.5f, -0.5f, RGB(255,255,0)},

        {-0.5f, -0.5f,  0.5f, RGB(255,0,255)},
        { 0.5f, -0.5f,  0.5f, RGB(0,255,255)},
        { 0.5f,  0.5f,  0.5f, RGB(128,128,128)}, 
        {-0.5f,  0.5f,  0.5f, RGB(255,128,0)} 
    };

    const USHORT indices[] = {
        0,2,1, 0,3,2,
        4,5,6, 4,6,7,
        0,7,3, 0,4,7,
        1,6,5, 1,2,6,
        0,5,4, 0,1,5,
        3,6,2, 3,7,6
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(vertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { vertices };
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);

    desc.ByteWidth = sizeof(indices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);
}

void CompileShaders()
{
       UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    D3DCompile(
        vertexShaderCode, strlen(vertexShaderCode),
        nullptr, nullptr, nullptr,
        "vs", "vs_5_0", compileFlags, 0, 
        &pVsBlob, &pErrorBlob
    );

    if (pErrorBlob)
    {
        OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        pErrorBlob->Release();
    }

    g_pD3DDevice->CreateVertexShader(
        pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(),
        nullptr, &g_pVertexShader
    );

    D3DCompile(
        pixelShaderCode, strlen(pixelShaderCode), 
        nullptr, nullptr, nullptr,
        "ps", "ps_5_0", compileFlags, 
        0, &pPsBlob, &pErrorBlob
    );

    if (pErrorBlob)
    {
        OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        pErrorBlob->Release();
    }

    g_pD3DDevice->CreatePixelShader(
        pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(),
        nullptr, &g_pPixelShader
    );

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pD3DDevice->CreateInputLayout(layout, 2,
        pVsBlob->GetBufferPointer(),
        pVsBlob->GetBufferSize(),
        &g_pInputLayout
    );

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
}


void UpdateCamera(double deltaTime)
{
    float speed = 1.0f; 
    if (g_KeyLeft)  
        g_CamYaw -= speed * (float)deltaTime;
    if (g_KeyRight) 
        g_CamYaw += speed * (float)deltaTime;
    if (g_KeyUp)    
        g_CamPitch += speed * (float)deltaTime;
    if (g_KeyDown)  
        g_CamPitch -= speed * (float)deltaTime;

    const float maxPitch = 1.5f;
    if (g_CamPitch > maxPitch) 
        g_CamPitch = maxPitch;
    if (g_CamPitch < -maxPitch) 
        g_CamPitch = -maxPitch;
}

void Render()
{
    if (!g_pD3DContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    double deltaTime = currentTime - g_LastFrameTime;
    g_LastFrameTime = currentTime;


    UpdateCamera(deltaTime);
    g_pD3DContext->ClearState();
    g_pD3DContext->OMSetRenderTargets(1, &g_pBackBufferRTV, nullptr);

    const float clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pD3DContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_WindowWidth;
    vp.Height = (float)g_WindowHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pD3DContext->RSSetViewports(1, &vp);

    float angle = (float)currentTime * 0.5f;
    XMMATRIX model = XMMatrixRotationY(angle);

    float camX = g_CamDistance * sin(g_CamYaw) * cos(g_CamPitch);
    float camY = g_CamDistance * sin(g_CamPitch);
    float camZ = g_CamDistance * cos(g_CamYaw) * cos(g_CamPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);

    float aspect = (float)g_WindowWidth / (float)g_WindowHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);

    XMMATRIX vpMatrix = view * proj;

    ModelConstantBuffer modelData;
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model));
    g_pD3DContext->UpdateSubresource(g_pModelCB, 0, nullptr, &modelData, 0, 0);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pD3DContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpMatrix));
        g_pD3DContext->Unmap(g_pViewProjCB, 0);
    }

    UINT stride = sizeof(VertexData);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = { g_pVertexBuffer };
    g_pD3DContext->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    g_pD3DContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pD3DContext->IASetInputLayout(g_pInputLayout);
    g_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* cbs[] = { g_pModelCB, g_pViewProjCB };
    g_pD3DContext->VSSetConstantBuffers(0, 2, cbs);

    g_pD3DContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pD3DContext->PSSetShader(g_pPixelShader, nullptr, 0);


    g_pD3DContext->DrawIndexed(36, 0, 0);

    g_pSwapChain->Present(1, 0);
}

void ResizeWindow(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pD3DDevice || !g_pD3DContext)
        return;

    g_pD3DContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight,
        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) 
        return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer)
    {
        g_pD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
        pBackBuffer->Release();
    }

    g_WindowWidth = newWidth;
    g_WindowHeight = newHeight;
}

void CleanupDirect3D()
{
    if (g_pD3DContext)
        g_pD3DContext->ClearState();

    SAFE_RELEASE(g_pModelCB);
    SAFE_RELEASE(g_pViewProjCB);
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pSwapChain);

#ifdef _DEBUG
    if (g_pD3DDevice)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pD3DDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
    }
#endif

    SAFE_RELEASE(g_pD3DContext);
    SAFE_RELEASE(g_pD3DDevice);
}