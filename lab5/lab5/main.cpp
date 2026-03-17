#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <string>
#include <vector>
#include <cstdio> 
#include <algorithm> 

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

std::wstring GetExePath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

struct DDS_PIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};

struct DDS_HEADER
{
    DWORD dwSize;
    DWORD dwHeaderFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD dwSurfaceFlags;
    DWORD dwCubemapFlags;
    DWORD dwReserved2[3];
};

#define DDS_MAGIC 0x20534444  // "DDS "
#define DDS_HEADER_FLAGS_TEXTURE 0x00001007
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040

#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')

inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }

UINT GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
        return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
        return 16;
    default:
        return 0;
    }
}

struct TextureDesc
{
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

bool LoadDDS(const wchar_t* filename, TextureDesc& desc)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwMagic;
    DWORD dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC)
    {
        CloseHandle(hFile);
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    if (header.ddspf.dwFlags & DDS_FOURCC)
    {
        switch (header.ddspf.dwFourCC)
        {
        case FOURCC_DXT1:
            desc.fmt = DXGI_FORMAT_BC1_UNORM;
            break;
        case FOURCC_DXT3:
            desc.fmt = DXGI_FORMAT_BC2_UNORM;
            break;
        case FOURCC_DXT5:
            desc.fmt = DXGI_FORMAT_BC3_UNORM;
            break;
        default:
            desc.fmt = DXGI_FORMAT_UNKNOWN;
            break;
        }
    }
    else if (header.ddspf.dwFlags & DDS_RGB)
    {
        desc.fmt = DXGI_FORMAT_UNKNOWN;
    }

    if (desc.fmt == DXGI_FORMAT_UNKNOWN)
    {
        CloseHandle(hFile);
        return false;
    }

    UINT32 blockWidth = DivUp(desc.width, 4u);
    UINT32 blockHeight = DivUp(desc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(desc.fmt);
    UINT32 dataSize = pitch * blockHeight;

    desc.pData = malloc(dataSize);
    if (!desc.pData)
    {
        CloseHandle(hFile);
        return false;
    }
    ReadFile(hFile, desc.pData, dataSize, &dwBytesRead, NULL);

    CloseHandle(hFile);
    return true;
}

HWND g_hMainWindow = nullptr;

ID3D11Device* g_pD3DDevice = nullptr;
ID3D11DeviceContext* g_pD3DContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;
ID3D11RasterizerState* g_pRSCullNone = nullptr;

struct TexturedVertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};

ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;

ID3D11Buffer* g_pPlaneVertexBuffer = nullptr;
ID3D11Buffer* g_pPlaneIndexBuffer = nullptr;

ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;

ID3D11PixelShader* g_pPlanePS = nullptr;

ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxInputLayout = nullptr;
ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;

struct ModelConstantBuffer
{
    XMMATRIX model;
};
struct ViewProjConstantBuffer
{
    XMMATRIX vp;
};
ID3D11Buffer* g_pModelCB = nullptr;
ID3D11Buffer* g_pModelCB2 = nullptr;  
ID3D11Buffer* g_pModelCBPlane1 = nullptr;  
ID3D11Buffer* g_pModelCBPlane2 = nullptr;   
ID3D11Buffer* g_pViewProjCB = nullptr;

ID3D11ShaderResourceView* g_pTextureView = nullptr;
ID3D11ShaderResourceView* g_pCubemapView = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

ID3D11BlendState* g_pBlendState = nullptr;
ID3D11DepthStencilState* g_pDepthNoWrite = nullptr;

ID3D11RasterizerState* g_pRSCullBack = nullptr;

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
void CreateBuffers();
void CompileShaders();
void LoadTextures();
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

    g_hMainWindow = CreateWindowW(wc.lpszClassName, L"Лабораторная работа 8 - Глубина и прозрачность",
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

    CreateBuffers();
    CompileShaders();
    LoadTextures();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelConstantBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pModelCB);
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pModelCB2);
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pModelCBPlane1);
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pModelCBPlane2);

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
            Render();
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
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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
    if (FAILED(hr)) return false;

    hr = g_pD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_WindowWidth;
    depthDesc.Height = g_WindowHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pD3DDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return false;

    hr = g_pD3DDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
    pDepthStencil->Release();
    if (FAILED(hr)) return false;

    return true;
}

void CreateBuffers()
{
    const TexturedVertex cubeVertices[] = {
        // Задняя грань (z = -0.5)
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },

        // Передняя грань (z = 0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },

        // Левая грань (x = -0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },

        // Правая грань (x = 0.5)
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },

        // Верхняя грань (y = 0.5)
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },

        // Нижняя грань (y = -0.5)
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }
    };

    const USHORT cubeIndices[] = {
         0,  2,  1,  0,  3,  2,  // задняя
         4,  5,  6,  4,  6,  7,  // передняя
         8, 10,  9,  8, 11, 10,  // левая
        12, 14, 13, 12, 15, 14,  // правая
        16, 18, 17, 16, 19, 18,  // верхняя
        20, 22, 21, 20, 23, 22   // нижняя
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(cubeVertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { cubeVertices };
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer);

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer);

    const TexturedVertex planeVertices[] = {
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) }
    };
    const USHORT planeIndices[] = { 0, 1, 2, 0, 2, 3 };

    desc.ByteWidth = sizeof(planeVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = planeVertices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pPlaneVertexBuffer);

    desc.ByteWidth = sizeof(planeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = planeIndices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pPlaneIndexBuffer);

    const TexturedVertex skyboxVertices[] = {
        { XMFLOAT3(-10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10,  10), XMFLOAT2(0,0) }
    };
    const USHORT skyboxIndices[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,7,3, 0,4,7,
        1,2,6, 1,6,5,  3,7,6, 3,6,2,  0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pSkyboxVertexBuffer);

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    g_pD3DDevice->CreateBuffer(&desc, &data, &g_pSkyboxIndexBuffer);
}


void CompileShaders()
{
    const char* vertexShaderCode = R"(
        cbuffer ModelCB : register(b0) { float4x4 model; }
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VSInput {
            float3 pos : POSITION;
            float2 uv : TEXCOORD;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.uv = v.uv;
            return o;
        }
    )";

    const char* texturedPS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState colorSampler : register(s0);
        struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 {
            return colorTexture.Sample(colorSampler, p.uv);
        }
    )";

    const char* planePS = R"(
        struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 {
            return float4(1.0f, 0.0f, 0.0f, 0.5f); // красный с альфа 0.5
        }
    )";

    const char* skyboxVS = R"(
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VSInput { float3 pos : POSITION; float2 uv : TEXCOORD; };
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        VSOutput vs(VSInput v) {
            VSOutput o;
            o.pos = mul(float4(v.pos, 1.0), vp);
            o.localPos = v.pos;
            return o;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1);
        SamplerState skyboxSampler : register(s1);
        struct VSOutput { float4 pos : SV_Position; float3 localPos : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 {
            return skyboxTexture.Sample(skyboxSampler, p.localPos);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;


    D3DCompile(vertexShaderCode, strlen(vertexShaderCode), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pVertexShader);


    D3DCompile(texturedPS, strlen(texturedPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    SAFE_RELEASE(pPsBlob);


    D3DCompile(planePS, strlen(planePS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPlanePS);
    SAFE_RELEASE(pPsBlob);


    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pD3DDevice->CreateInputLayout(layout, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);

    // Skybox шейдеры
    SAFE_RELEASE(pVsBlob);
    D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);

    D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);

    g_pD3DDevice->CreateInputLayout(layout, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
}

void LoadTextures()
{
    HRESULT hr;

    TextureDesc texDesc;
    std::wstring fullPath = GetExePath() + L"..\\..\\textures\\like.dds";
    if (!LoadDDS(fullPath.c_str(), texDesc))
    {
        MessageBoxA(NULL, "Failed to load brick.dds", "Error", MB_OK);
        return;
    }

    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = texDesc.width;
    tex2DDesc.Height = texDesc.height;
    tex2DDesc.MipLevels = 1;
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = texDesc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.SampleDesc.Quality = 0;
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    UINT blockWidth = DivUp(texDesc.width, 4u);
    UINT blockHeight = DivUp(texDesc.height, 4u);
    UINT pitch = blockWidth * GetBytesPerBlock(texDesc.fmt);

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = texDesc.pData;
    texData.SysMemPitch = pitch;

    ID3D11Texture2D* pTexture = nullptr;
    hr = g_pD3DDevice->CreateTexture2D(&tex2DDesc, &texData, &pTexture);
    free(texDesc.pData);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = g_pD3DDevice->CreateShaderResourceView(pTexture, &srvDesc, &g_pTextureView);
    pTexture->Release();
    if (FAILED(hr)) return;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] = sampDesc.BorderColor[3] = 1.0f;
    g_pD3DDevice->CreateSamplerState(&sampDesc, &g_pSampler);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_pD3DDevice->CreateBlendState(&blendDesc, &g_pBlendState);

    D3D11_DEPTH_STENCIL_DESC dsNoWriteDesc = {};
    dsNoWriteDesc.DepthEnable = TRUE;
    dsNoWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsNoWriteDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsNoWriteDesc.StencilEnable = FALSE;
    g_pD3DDevice->CreateDepthStencilState(&dsNoWriteDesc, &g_pDepthNoWrite);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    g_pD3DDevice->CreateRasterizerState(&rsDesc, &g_pRSCullBack);

    std::wstring path = GetExePath() + L"..\\..\\textures\\skybox\\";
    std::wstring faceNames[6] = {
        path + L"posx.dds", path + L"negx.dds",
        path + L"posy.dds", path + L"negy.dds",
        path + L"posz.dds", path + L"negz.dds"
    };

    TextureDesc faceDescs[6];
    bool allOk = true;
    for (int i = 0; i < 6; ++i)
    {
        if (!LoadDDS(faceNames[i].c_str(), faceDescs[i]))
        {
            allOk = false;
            break;
        }
    }
    if (!allOk)
    {
        MessageBoxA(NULL, "Failed to load cubemap faces", "Error", MB_OK);
        return;
    }

    for (int i = 1; i < 6; ++i)
    {
        if (faceDescs[i].fmt != faceDescs[0].fmt ||
            faceDescs[i].width != faceDescs[0].width ||
            faceDescs[i].height != faceDescs[0].height)
        {
            MessageBoxA(NULL, "Cubemap faces must be identical", "Error", MB_OK);
            return;
        }
    }

    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceDescs[0].width;
    cubeDesc.Height = faceDescs[0].height;
    cubeDesc.MipLevels = 1;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = faceDescs[0].fmt;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.SampleDesc.Quality = 0;
    cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    blockWidth = DivUp(cubeDesc.Width, 4u);
    blockHeight = DivUp(cubeDesc.Height, 4u);
    pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

    D3D11_SUBRESOURCE_DATA initData[6];
    for (int i = 0; i < 6; ++i)
    {
        initData[i].pSysMem = faceDescs[i].pData;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pCubemapTex = nullptr;
    hr = g_pD3DDevice->CreateTexture2D(&cubeDesc, initData, &pCubemapTex);
    for (int i = 0; i < 6; ++i) free(faceDescs[i].pData);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
    cubeSRVDesc.Format = cubeDesc.Format;
    cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSRVDesc.TextureCube.MipLevels = 1;
    cubeSRVDesc.TextureCube.MostDetailedMip = 0;
    hr = g_pD3DDevice->CreateShaderResourceView(pCubemapTex, &cubeSRVDesc, &g_pCubemapView);
    pCubemapTex->Release();

    D3D11_RASTERIZER_DESC rsCullNoneDesc = {};
    rsCullNoneDesc.FillMode = D3D11_FILL_SOLID;
    rsCullNoneDesc.CullMode = D3D11_CULL_NONE;
    rsCullNoneDesc.FrontCounterClockwise = FALSE;
    g_pD3DDevice->CreateRasterizerState(&rsCullNoneDesc, &g_pRSCullNone);
}

void UpdateCamera(double deltaTime)
{
    float speed = 1.0f;
    if (g_KeyLeft)  g_CamYaw -= speed * (float)deltaTime;
    if (g_KeyRight) g_CamYaw += speed * (float)deltaTime;
    if (g_KeyUp)    g_CamPitch += speed * (float)deltaTime;
    if (g_KeyDown)  g_CamPitch -= speed * (float)deltaTime;

    const float maxPitch = 1.5f;
    if (g_CamPitch > maxPitch) g_CamPitch = maxPitch;
    if (g_CamPitch < -maxPitch) g_CamPitch = -maxPitch;
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
    g_pD3DContext->OMSetRenderTargets(1, &g_pBackBufferRTV, g_pDepthStencilView);
    const float clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_pD3DContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);
    g_pD3DContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)g_WindowWidth, (float)g_WindowHeight, 0.0f, 1.0f };
    g_pD3DContext->RSSetViewports(1, &vp);
    g_pD3DContext->RSSetState(g_pRSCullBack);

    float camX = g_CamDistance * sin(g_CamYaw) * cos(g_CamPitch);
    float camY = g_CamDistance * sin(g_CamPitch);
    float camZ = g_CamDistance * cos(g_CamYaw) * cos(g_CamPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = (float)g_WindowWidth / (float)g_WindowHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);


    {
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
        XMMATRIX vpSky = viewNoTrans * proj;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_pD3DContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
            XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpSky));
            g_pD3DContext->Unmap(g_pViewProjCB, 0);
        }

        D3D11_DEPTH_STENCIL_DESC dsSky = {};
        dsSky.DepthEnable = TRUE;
        dsSky.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsSky.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dsSky.StencilEnable = FALSE;
        ID3D11DepthStencilState* pDSSky = nullptr;
        g_pD3DDevice->CreateDepthStencilState(&dsSky, &pDSSky);
        g_pD3DContext->OMSetDepthStencilState(pDSSky, 0);
        SAFE_RELEASE(pDSSky);

        D3D11_RASTERIZER_DESC rsSky = {};
        rsSky.FillMode = D3D11_FILL_SOLID;
        rsSky.CullMode = D3D11_CULL_NONE;
        ID3D11RasterizerState* pRSSky = nullptr;
        g_pD3DDevice->CreateRasterizerState(&rsSky, &pRSSky);
        g_pD3DContext->RSSetState(pRSSky);
        SAFE_RELEASE(pRSSky);

        g_pD3DContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        g_pD3DContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
        g_pD3DContext->IASetInputLayout(g_pSkyboxInputLayout);
        UINT stride = sizeof(TexturedVertex);
        UINT offset = 0;
        ID3D11Buffer* vbSky[] = { g_pSkyboxVertexBuffer };
        g_pD3DContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
        g_pD3DContext->IASetIndexBuffer(g_pSkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
        g_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbsSky[] = { nullptr, g_pViewProjCB };
        g_pD3DContext->VSSetConstantBuffers(0, 2, cbsSky);
        ID3D11ShaderResourceView* skySRV[] = { g_pCubemapView };
        g_pD3DContext->PSSetShaderResources(1, 1, skySRV);
        ID3D11SamplerState* samplers[] = { g_pSampler };
        g_pD3DContext->PSSetSamplers(1, 1, samplers);

        g_pD3DContext->DrawIndexed(36, 0, 0);
        g_pD3DContext->RSSetState(g_pRSCullBack);
        g_pD3DContext->OMSetDepthStencilState(nullptr, 0);
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pD3DContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(view * proj));
        g_pD3DContext->Unmap(g_pViewProjCB, 0);
    }

    float angle = (float)currentTime * 0.5f;
    XMMATRIX model1 = XMMatrixRotationY(angle);
    XMMATRIX model2 = XMMatrixTranslation(0.0f, 0.0f, 2.0f) * XMMatrixRotationY(angle);

    ModelConstantBuffer modelData;
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model1));
    g_pD3DContext->UpdateSubresource(g_pModelCB, 0, nullptr, &modelData, 0, 0);

    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model2));
    g_pD3DContext->UpdateSubresource(g_pModelCB2, 0, nullptr, &modelData, 0, 0);

    ID3D11Buffer* cbsCube[] = { g_pModelCB, g_pViewProjCB };
    g_pD3DContext->VSSetConstantBuffers(0, 2, cbsCube);

    UINT stride = sizeof(TexturedVertex);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pD3DContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pD3DContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pD3DContext->IASetInputLayout(g_pInputLayout);
    g_pD3DContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pD3DContext->PSSetShader(g_pPixelShader, nullptr, 0);
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pD3DContext->PSSetSamplers(0, 1, samplers);
    ID3D11ShaderResourceView* cubeSRV[] = { g_pTextureView };
    g_pD3DContext->PSSetShaderResources(0, 1, cubeSRV);

    g_pD3DContext->DrawIndexed(36, 0, 0);
    cbsCube[0] = g_pModelCB2;
    g_pD3DContext->VSSetConstantBuffers(0, 2, cbsCube);
    g_pD3DContext->DrawIndexed(36, 0, 0);

    // Прозрачные плоскости
    XMVECTOR eyePos = eye;
    struct PlaneInfo {
        XMMATRIX world;
        float distance; 
        ID3D11Buffer* modelBuffer;
    } planes[2];

    // Плоскость 1
    XMMATRIX plane1World = XMMatrixTranslation(0.0f, 0.0f, 0.6f) * XMMatrixRotationY(angle);
    planes[0].world = plane1World;
    XMVECTOR center1 = XMVectorSet(0.0f, 0.0f, 0.5f, 1.0f);
    XMVECTOR toEye1 = XMVectorSubtract(eyePos, center1);
    planes[0].distance = XMVectorGetX(XMVector3Length(toEye1));
    planes[0].modelBuffer = g_pModelCBPlane1;

    // Плоскость 2 
    XMMATRIX plane2World = XMMatrixTranslation(0.0f, 0.0f, 1.2f) * XMMatrixRotationY(angle);
    planes[1].world = plane2World;
    XMVECTOR center2 = XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
    XMVECTOR toEye2 = XMVectorSubtract(eyePos, center2);
    planes[1].distance = XMVectorGetX(XMVector3Length(toEye2));
    planes[1].modelBuffer = g_pModelCBPlane2;

    if (planes[0].distance < planes[1].distance)
    {
        std::swap(planes[0], planes[1]);
    }

    g_pD3DContext->OMSetBlendState(g_pBlendState, nullptr, 0xffffffff);
    g_pD3DContext->OMSetDepthStencilState(g_pDepthNoWrite, 0);
    g_pD3DContext->RSSetState(g_pRSCullNone);

    g_pD3DContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pD3DContext->PSSetShader(g_pPlanePS, nullptr, 0);
    g_pD3DContext->IASetInputLayout(g_pInputLayout);
    g_pD3DContext->IASetVertexBuffers(0, 1, &g_pPlaneVertexBuffer, &stride, &offset);
    g_pD3DContext->IASetIndexBuffer(g_pPlaneIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    g_pD3DContext->PSSetShaderResources(0, 1, nullSRV);

    for (int i = 0; i < 2; ++i)
    {
        XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(planes[i].world));
        g_pD3DContext->UpdateSubresource(planes[i].modelBuffer, 0, nullptr, &modelData, 0, 0);

        ID3D11Buffer* cbsPlane[] = { planes[i].modelBuffer, g_pViewProjCB };
        g_pD3DContext->VSSetConstantBuffers(0, 2, cbsPlane);

        g_pD3DContext->DrawIndexed(6, 0, 0);
    }

    g_pD3DContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    g_pD3DContext->OMSetDepthStencilState(nullptr, 0);

    g_pSwapChain->Present(1, 0);
}


void ResizeWindow(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pD3DDevice || !g_pD3DContext)
        return;

    g_pD3DContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer)
    {
        g_pD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
        pBackBuffer->Release();
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth;
    depthDesc.Height = newHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    g_pD3DDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (pDepthStencil)
    {
        g_pD3DDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
        pDepthStencil->Release();
    }

    g_WindowWidth = newWidth;
    g_WindowHeight = newHeight;
}

void CleanupDirect3D()
{
    if (g_pD3DContext)
        g_pD3DContext->ClearState();

    SAFE_RELEASE(g_pModelCB);
    SAFE_RELEASE(g_pModelCB2);
    SAFE_RELEASE(g_pModelCBPlane1);
    SAFE_RELEASE(g_pModelCBPlane2);
    SAFE_RELEASE(g_pViewProjCB);
    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);
    SAFE_RELEASE(g_pPlanePS);
    SAFE_RELEASE(g_pSkyboxInputLayout);
    SAFE_RELEASE(g_pSkyboxVS);
    SAFE_RELEASE(g_pSkyboxPS);
    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);
    SAFE_RELEASE(g_pPlaneIndexBuffer);
    SAFE_RELEASE(g_pPlaneVertexBuffer);
    SAFE_RELEASE(g_pSkyboxIndexBuffer);
    SAFE_RELEASE(g_pSkyboxVertexBuffer);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pTextureView);
    SAFE_RELEASE(g_pCubemapView);
    SAFE_RELEASE(g_pSampler);
    SAFE_RELEASE(g_pBlendState);
    SAFE_RELEASE(g_pDepthNoWrite);
    SAFE_RELEASE(g_pRSCullBack);
    SAFE_RELEASE(g_pRSCullNone);

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