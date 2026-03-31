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

const UINT MAX_INSTANCES = 10;         
const UINT NUM_TEXTURES = 2;           
const std::wstring TEXTURE_NAMES[] = {
    L"Brick.dds",
    L"Kitty.dds"
};

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

#define DDS_MAGIC 0x20534444
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

struct TexturedVertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};

struct TexturedNormalTangentVertex
{
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 uv;
};

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
ID3D11ShaderResourceView* g_pNormalMapView = nullptr;

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

ID3D11VertexShader* g_pInstancedVS = nullptr;
ID3D11PixelShader* g_pInstancedPS = nullptr;
ID3D11InputLayout* g_pInstancedInputLayout = nullptr;

struct ModelConstantBuffer
{
    XMMATRIX model;
};
struct ViewProjConstantBuffer
{
    XMMATRIX vp;
};

struct SceneConstantBuffer
{
    XMMATRIX vp;
    XMFLOAT4 cameraPos;
    XMFLOAT4 lightCount;
    struct Light {
        XMFLOAT4 pos;
        XMFLOAT4 color;
    } lights[10];
    XMFLOAT4 ambientColor;
};


struct GeomBuffer
{
    XMMATRIX model;                
    XMMATRIX norm;                
    XMFLOAT4 shineSpeedTexIdNM;    
    XMFLOAT4 angle;                
};

ID3D11Buffer* g_pSceneCB = nullptr;
ID3D11Buffer* g_pModelCB = nullptr;
ID3D11Buffer* g_pModelCB2 = nullptr;
ID3D11Buffer* g_pViewProjCB = nullptr;

ID3D11Buffer* g_pModelCBPlane1 = nullptr;
ID3D11Buffer* g_pModelCBPlane2 = nullptr;

ID3D11ShaderResourceView* g_pTextureView = nullptr;
ID3D11ShaderResourceView* g_pCubemapView = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

ID3D11BlendState* g_pBlendState = nullptr;
ID3D11DepthStencilState* g_pDepthNoWrite = nullptr;
ID3D11RasterizerState* g_pRSCullBack = nullptr;

ID3D11Buffer* g_pGeomBufferInst = nullptr;
ID3D11Buffer* g_pVisibleIdsBuffer = nullptr;

GeomBuffer g_Instances[MAX_INSTANCES];
UINT g_InstanceCount = 0;
XMVECTOR g_LocalAABBMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 0.0f);
XMVECTOR g_LocalAABBMax = XMVectorSet(0.5f, 0.5f, 0.5f, 0.0f);


ID3D11ShaderResourceView* g_pTextureArrayView = nullptr;

UINT g_WindowWidth = 1280;
UINT g_WindowHeight = 720;
float g_CamYaw = 0.0f;
float g_CamPitch = 0.0f;
float g_CamDistance = 5.0f;
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


void CreateInstances();
void LoadTextureArray();
void BuildFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6]);
void TransformAABB(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax);
bool IsAABBInsideFrustum(const XMVECTOR planes[6], const XMVECTOR& aabbMin, const XMVECTOR& aabbMax);
void UpdateInstanceTransforms(double time);

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
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_WindowWidth, (LONG)g_WindowHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hMainWindow = CreateWindowW(wc.lpszClassName, L"Lab7: Instancing & Frustum Culling",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWindow)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
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
    LoadTextureArray();      
    CreateInstances();       

    // Create constant buffers
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

    desc.ByteWidth = sizeof(SceneConstantBuffer);
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pSceneCB);

    desc.ByteWidth = sizeof(GeomBuffer) * MAX_INSTANCES;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pGeomBufferInst);

    desc.ByteWidth = sizeof(XMUINT4) * MAX_INSTANCES; 
    g_pD3DDevice->CreateBuffer(&desc, nullptr, &g_pVisibleIdsBuffer);

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
    // flags |= D3D11_CREATE_DEVICE_DEBUG;
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
    const TexturedNormalTangentVertex cubeVertices[] = {
        // Back face
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Front face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Left face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Right face
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Top face
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Bottom face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) }
    };

    const USHORT cubeIndices[] = {
         0,  2,  1,  0,  3,  2,
         4,  5,  6,  4,  6,  7,
         8, 10,  9,  8, 11, 10,
        12, 14, 13, 12, 15, 14,
        16, 18, 17, 16, 19, 18,
        20, 22, 21, 20, 23, 22
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
            float3 normal : NORMAL;
            float3 tangent : TANGENT;
            float2 uv : TEXCOORD;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 uv : TEXCOORD1;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.worldPos = worldPos.xyz;
            float3x3 normalMatrix = (float3x3)model;
            o.worldNormal = normalize(mul(v.normal, normalMatrix));
            o.worldTangent = normalize(mul(v.tangent, normalMatrix));
            o.uv = v.uv;
            return o;
        }
    )";

    const char* pixelShaderCode = R"(
        Texture2D colorTexture : register(t0);
        Texture2D normalMap : register(t1);
        SamplerState colorSampler : register(s0);
        cbuffer SceneCB : register(b2) {
            float4x4 vp;
            float4 cameraPos;
            float4 lightCount;
            struct Light {
                float4 pos;
                float4 color;
            } lights[10];
            float4 ambientColor;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 uv : TEXCOORD1;
        };
        float4 ps(VSOutput p) : SV_Target0 {
            float4 texColor = colorTexture.Sample(colorSampler, p.uv);
            float3 tangentNormal = normalMap.Sample(colorSampler, p.uv).xyz * 2.0 - 1.0;
            float3 N = normalize(p.worldNormal);
            float3 T = normalize(p.worldTangent);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            float3 worldNormal = normalize(mul(tangentNormal, TBN));
            float3 finalColor = ambientColor.xyz * texColor.xyz;
            for (int i = 0; i < lightCount.x; ++i) {
                float3 L = lights[i].pos.xyz - p.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(worldNormal, L), 0.0);
                finalColor += texColor.xyz * diff * atten * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - p.worldPos.xyz);
                float3 R = reflect(-L, worldNormal);
                float spec = pow(max(dot(V, R), 0.0), 32.0);
                finalColor += spec * atten * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
        }
    )";

    const char* planePS = R"(
        struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        float4 ps(VSOutput p) : SV_Target0 {
            return float4(1.0f, 0.0f, 0.0f, 0.5f);
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

    const char* instancedVS = R"(
        cbuffer GeomBufferInst : register(b1)
        {
            struct GeomBuffer
            {
                float4x4 model;
                float4x4 norm;
                float4 shineSpeedTexIdNM; // x=shininess, y=rot speed, z=tex id, w=normal map presence
                float4 angle;
            } geomBuffer[100];
        };
        cbuffer ViewProjCB : register(b2)
        {
            float4x4 vp;
        };
        struct VSInput
        {
            float3 pos    : POSITION;
            float3 tang   : TANGENT;
            float3 norm   : NORMAL;
            float2 uv     : TEXCOORD;
            uint instanceId : SV_InstanceID;
        };
        struct VSOutput
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        VSOutput vs(VSInput v)
        {
            VSOutput o;
            uint idx = v.instanceId;
            float4 worldPos = mul(geomBuffer[idx].model, float4(v.pos, 1.0));
            o.pos = mul(worldPos, vp);
            o.worldPos = worldPos;
            o.uv = v.uv;
            o.tang = mul(geomBuffer[idx].norm, float4(v.tang, 0)).xyz;
            o.norm = mul(geomBuffer[idx].norm, float4(v.norm, 0)).xyz;
            o.instanceId = idx;
            return o;
        }
    )";


    const char* instancedPS = R"(
        Texture2DArray colorTexture : register(t0);
        Texture2D normalMapTexture : register(t1);
        SamplerState colorSampler : register(s0);
        cbuffer GeomBufferInst : register(b1)
        {
            struct GeomBuffer
            {
                float4x4 model;
                float4x4 norm;
                float4 shineSpeedTexIdNM;
                float4 angle;
            } geomBuffer[100];
        };
        cbuffer SceneCB : register(b3)
        {
            float4x4 vp;
            float4 cameraPos;
            float4 lightCount;
            struct Light
            {
                float4 pos;
                float4 color;
            } lights[10];
            float4 ambientColor;
        };
        cbuffer VisibleIds : register(b4)
        {
            uint4 ids[100];
        };
        struct VSOutput
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        float4 ps(VSOutput pixel) : SV_Target0
        {
            uint idx = ids[pixel.instanceId].x;
            uint texId = (uint)geomBuffer[idx].shineSpeedTexIdNM.z;
            float3 color = colorTexture.Sample(colorSampler, float3(pixel.uv, texId)).xyz;
            uint flags = asuint(geomBuffer[idx].shineSpeedTexIdNM.w);
            float3 normal;
            if (flags == 1 && lightCount.y > 0)
            {
                float3 tangentNormal = normalMapTexture.Sample(colorSampler, pixel.uv).xyz * 2.0 - 1.0;
                float3 N = normalize(pixel.norm);
                float3 T = normalize(pixel.tang);
                float3 B = cross(N, T);
                normal = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
            }
            else
            {
                normal = normalize(pixel.norm);
            }
            float shininess = geomBuffer[idx].shineSpeedTexIdNM.x;
            float3 finalColor = ambientColor.xyz * color;
            for (int i = 0; i < lightCount.x; ++i)
            {
                float3 L = lights[i].pos.xyz - pixel.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(normal, L), 0.0);
                finalColor += color * diff * atten * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - pixel.worldPos.xyz);
                float3 R = reflect(-L, normal);
                float spec = pow(max(dot(V, R), 0.0), shininess);
                finalColor += spec * atten * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
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

    D3DCompile(pixelShaderCode, strlen(pixelShaderCode), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    SAFE_RELEASE(pPsBlob);

    D3DCompile(planePS, strlen(planePS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pPlanePS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pD3DDevice->CreateInputLayout(layout, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInputLayout);
    SAFE_RELEASE(pVsBlob);

    D3DCompile(instancedVS, strlen(instancedVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pInstancedVS);

    D3DCompile(instancedPS, strlen(instancedPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pInstancedPS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layoutInst[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pD3DDevice->CreateInputLayout(layoutInst, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInstancedInputLayout);
    SAFE_RELEASE(pVsBlob);

    D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);

    D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pD3DDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);

    D3D11_INPUT_ELEMENT_DESC layoutSky[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pD3DDevice->CreateInputLayout(layoutSky, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);
    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);

    assert(g_pPlanePS != nullptr);
    OutputDebugStringA(g_pPlanePS ? "Plane PS created\n" : "Plane PS not created!\n");
}

void LoadTextures()
{
    HRESULT hr;

    TextureDesc texDesc;
    std::wstring fullPath = GetExePath() + L"..\\..\\textures\\like.dds";
    if (!LoadDDS(fullPath.c_str(), texDesc))
    {
        MessageBoxA(NULL, "Failed to load like.dds", "Error", MB_OK);
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

    std::wstring normalPath = GetExePath() + L"..\\..\\textures\\like_normal.dds";
    TextureDesc normalDesc;
    if (LoadDDS(normalPath.c_str(), normalDesc))
    {
        D3D11_TEXTURE2D_DESC normTexDesc = {};
        normTexDesc.Width = normalDesc.width;
        normTexDesc.Height = normalDesc.height;
        normTexDesc.MipLevels = 1;
        normTexDesc.ArraySize = 1;
        normTexDesc.Format = normalDesc.fmt;
        normTexDesc.SampleDesc.Count = 1;
        normTexDesc.SampleDesc.Quality = 0;
        normTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        normTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        UINT normBlockWidth = DivUp(normalDesc.width, 4u);
        UINT normBlockHeight = DivUp(normalDesc.height, 4u);
        UINT normPitch = normBlockWidth * GetBytesPerBlock(normalDesc.fmt);

        D3D11_SUBRESOURCE_DATA normData = {};
        normData.pSysMem = normalDesc.pData;
        normData.SysMemPitch = normPitch;

        ID3D11Texture2D* pNormalTex = nullptr;
        HRESULT hr = g_pD3DDevice->CreateTexture2D(&normTexDesc, &normData, &pNormalTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = normalDesc.fmt;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            hr = g_pD3DDevice->CreateShaderResourceView(pNormalTex, &srvDesc, &g_pNormalMapView);
            pNormalTex->Release();
        }
        free(normalDesc.pData);
    }
    else {
        OutputDebugStringA("No normal map loaded, using flat normals\n");
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
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

void LoadTextureArray()
{
    std::vector<TextureDesc> texDescs(NUM_TEXTURES);
    bool allOk = true;
    for (UINT i = 0; i < NUM_TEXTURES; ++i)
    {
        std::wstring fullPath = GetExePath() + L"..\\..\\textures\\" + TEXTURE_NAMES[i];
        if (!LoadDDS(fullPath.c_str(), texDescs[i]))
        {
            allOk = false;
            break;
        }
    }
    if (!allOk)
    {
        MessageBoxA(NULL, "Failed to load one of the textures for array", "Error", MB_OK);
        return;
    }

    DXGI_FORMAT fmt = texDescs[0].fmt;
    UINT width = texDescs[0].width;
    UINT height = texDescs[0].height;
    for (UINT i = 1; i < NUM_TEXTURES; ++i)
    {
        if (texDescs[i].fmt != fmt || texDescs[i].width != width || texDescs[i].height != height)
        {
            MessageBoxA(NULL, "Textures must have same format and size", "Error", MB_OK);
            return;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = NUM_TEXTURES;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    UINT blockWidth = DivUp(width, 4u);
    UINT blockHeight = DivUp(height, 4u);
    UINT pitch = blockWidth * GetBytesPerBlock(fmt);

    std::vector<D3D11_SUBRESOURCE_DATA> initData(NUM_TEXTURES);
    for (UINT i = 0; i < NUM_TEXTURES; ++i)
    {
        initData[i].pSysMem = texDescs[i].pData;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pTexArray = nullptr;
    HRESULT hr = g_pD3DDevice->CreateTexture2D(&texDesc, initData.data(), &pTexArray);
    for (auto& td : texDescs) free(td.pData);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = NUM_TEXTURES;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    hr = g_pD3DDevice->CreateShaderResourceView(pTexArray, &srvDesc, &g_pTextureArrayView);
    pTexArray->Release();
}

void CreateInstances()
{
    g_InstanceCount = MAX_INSTANCES;
    float radius = 3.0f;

    for (UINT i = 0; i < MAX_INSTANCES; ++i)
    {
        float phi = XM_PI * (3.0f - sqrtf(5.0f));
        float y = 1.0f - (i / (float)(MAX_INSTANCES - 1)) * 2.0f; 
        float radiusAtY = sqrtf(1.0f - y * y);
        float theta = i * phi * 2.0f * XM_PI;

        float x = cosf(theta) * radiusAtY;
        float z = sinf(theta) * radiusAtY;

        XMFLOAT3 pos(x * radius, y * radius, z * radius);

        XMMATRIX model = XMMatrixTranslation(pos.x, pos.y, pos.z);
        XMMATRIX norm = XMMatrixTranspose(XMMatrixInverse(nullptr, model));
        g_Instances[i].model = model;
        g_Instances[i].norm = norm;

        int texId = rand() % NUM_TEXTURES;
        float shininess = 32.0f;
        float rotSpeed = 0.5f + (rand() % 100) / 100.0f;
        float normalMapPresence = (texId == 2) ? 1.0f : 0.0f; // только для like.dds
        g_Instances[i].shineSpeedTexIdNM = XMFLOAT4(shininess, rotSpeed, (float)texId, normalMapPresence);
        g_Instances[i].angle = XMFLOAT4(pos.x, pos.y, pos.z, 0.0f);
    }
}


void UpdateInstanceTransforms(double time)
{
    for (UINT i = 0; i < g_InstanceCount; ++i)
    {
        float angle = (float)time * g_Instances[i].shineSpeedTexIdNM.y;
        XMMATRIX rot = XMMatrixRotationY(angle);
        XMMATRIX trans = XMMatrixTranslation(g_Instances[i].angle.x, g_Instances[i].angle.y, g_Instances[i].angle.z);
        g_Instances[i].model = rot * trans;
        g_Instances[i].norm = XMMatrixTranspose(XMMatrixInverse(nullptr, g_Instances[i].model));
    }
}

void BuildFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6])
{
    XMVECTOR row1 = XMVectorSet(vp.r[0].m128_f32[0], vp.r[1].m128_f32[0], vp.r[2].m128_f32[0], vp.r[3].m128_f32[0]);
    XMVECTOR row2 = XMVectorSet(vp.r[0].m128_f32[1], vp.r[1].m128_f32[1], vp.r[2].m128_f32[1], vp.r[3].m128_f32[1]);
    XMVECTOR row3 = XMVectorSet(vp.r[0].m128_f32[2], vp.r[1].m128_f32[2], vp.r[2].m128_f32[2], vp.r[3].m128_f32[2]);
    XMVECTOR row4 = XMVectorSet(vp.r[0].m128_f32[3], vp.r[1].m128_f32[3], vp.r[2].m128_f32[3], vp.r[3].m128_f32[3]);

    planes[0] = row4 + row1;  // left
    planes[1] = row4 - row1;  // right
    planes[2] = row4 + row2;  // bottom
    planes[3] = row4 - row2;  // top
    planes[4] = row4 + row3;  // near
    planes[5] = row4 - row3;  // far

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR norm = XMVector3Length(planes[i]);
        planes[i] = planes[i] / norm;
    }
}

void TransformAABB(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax)
{
    XMVECTOR corners[8];
    corners[0] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[1] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[2] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[3] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[4] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[5] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[6] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);
    corners[7] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);

    worldMin = XMVectorReplicate(FLT_MAX);
    worldMax = XMVectorReplicate(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR worldCorner = XMVector4Transform(corners[i], transform);
        worldMin = XMVectorMin(worldMin, worldCorner);
        worldMax = XMVectorMax(worldMax, worldCorner);
    }
}

bool IsAABBInsideFrustum(const XMVECTOR planes[6], const XMVECTOR& aabbMin, const XMVECTOR& aabbMax)
{
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = aabbMin;
        if (XMVectorGetX(planes[i]) >= 0) p = XMVectorSetX(p, XMVectorGetX(aabbMax));
        if (XMVectorGetY(planes[i]) >= 0) p = XMVectorSetY(p, XMVectorGetY(aabbMax));
        if (XMVectorGetZ(planes[i]) >= 0) p = XMVectorSetZ(p, XMVectorGetZ(aabbMax));
        if (XMVector4Dot(p, planes[i]).m128_f32[0] < 0) return false;
    }
    return true;
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
    XMMATRIX viewProj = view * proj;

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
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(viewProj));
        g_pD3DContext->Unmap(g_pViewProjCB, 0);
    }

    if (SUCCEEDED(g_pD3DContext->Map(g_pSceneCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        SceneConstantBuffer* pScene = (SceneConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pScene->vp, XMMatrixTranspose(viewProj));
        pScene->cameraPos = XMFLOAT4(camX, camY, camZ, 1.0f);
        pScene->lightCount.x = 2;
        pScene->lights[0].pos = XMFLOAT4(0.0f, 2.0f, 10.0f, 1.0f);
        pScene->lights[0].color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        pScene->lights[1].pos = XMFLOAT4(0.0f, 2.0f, 2.5f, 1.0f);
        pScene->lights[1].color = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
        pScene->ambientColor = XMFLOAT4(0.8f, 0.8f, 0.f, 1.0f);
        g_pD3DContext->Unmap(g_pSceneCB, 0);
    }


    UpdateInstanceTransforms(currentTime);
    g_pD3DContext->UpdateSubresource(g_pGeomBufferInst, 0, nullptr, g_Instances, sizeof(GeomBuffer) * MAX_INSTANCES, 0);


    XMVECTOR frustumPlanes[6];
    BuildFrustumPlanes(viewProj, frustumPlanes);
    std::vector<UINT> visibleIndices;
    XMVECTOR localMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);
    XMVECTOR localMax = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);
    for (UINT i = 0; i < g_InstanceCount; ++i)
    {
        XMVECTOR worldMin, worldMax;
        TransformAABB(g_Instances[i].model, localMin, localMax, worldMin, worldMax);
        if (IsAABBInsideFrustum(frustumPlanes, worldMin, worldMax))
            visibleIndices.push_back(i);

    }

    std::vector<XMUINT4> packedIds(MAX_INSTANCES);
    for (size_t j = 0; j < visibleIndices.size(); ++j)
        packedIds[j].x = visibleIndices[j];
    g_pD3DContext->UpdateSubresource(g_pVisibleIdsBuffer, 0, nullptr, packedIds.data(), sizeof(XMUINT4) * MAX_INSTANCES, 0);


    UINT stride = sizeof(TexturedNormalTangentVertex);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pD3DContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pD3DContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pD3DContext->IASetInputLayout(g_pInstancedInputLayout);
    g_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_pD3DContext->VSSetShader(g_pInstancedVS, nullptr, 0);
    g_pD3DContext->PSSetShader(g_pInstancedPS, nullptr, 0);

    ID3D11Buffer* cbInstVS[] = { nullptr, g_pGeomBufferInst, g_pViewProjCB };
    g_pD3DContext->VSSetConstantBuffers(0, 3, cbInstVS);

    ID3D11Buffer* cbInstPS[] = { nullptr, g_pGeomBufferInst, g_pSceneCB, g_pVisibleIdsBuffer };
    g_pD3DContext->PSSetConstantBuffers(1, 1, &g_pGeomBufferInst);  
    g_pD3DContext->PSSetConstantBuffers(3, 1, &g_pSceneCB);         
    g_pD3DContext->PSSetConstantBuffers(4, 1, &g_pVisibleIdsBuffer);

    ID3D11ShaderResourceView* texArraySRV[] = { g_pTextureArrayView, g_pNormalMapView };
    g_pD3DContext->PSSetShaderResources(0, 2, texArraySRV);
    ID3D11SamplerState* samp = g_pSampler;
    g_pD3DContext->PSSetSamplers(0, 1, &samp);

    if (visibleIndices.size() > 0)
        g_pD3DContext->DrawIndexedInstanced(36, (UINT)visibleIndices.size(), 0, 0, 0);

 
    XMVECTOR eyePos = eye;
    struct PlaneInfo {
        XMMATRIX world;
        float distance;
        ID3D11Buffer* modelBuffer;
    } planes[2];

    XMMATRIX plane1World = XMMatrixTranslation(0.0f, 0.0f, 0.6f);
    planes[0].world = plane1World;
    XMVECTOR center1 = plane1World.r[3];
    planes[0].distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(eyePos, center1)));
    planes[0].modelBuffer = g_pModelCBPlane1;

    XMMATRIX plane2World = XMMatrixTranslation(0.0f, 0.0f, 1.2f);
    planes[1].world = plane2World;
    XMVECTOR center2 = plane2World.r[3];
    planes[1].distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(eyePos, center2)));
    planes[1].modelBuffer = g_pModelCBPlane2;

    if (planes[0].distance < planes[1].distance)
        std::swap(planes[0], planes[1]);

    g_pD3DContext->OMSetBlendState(g_pBlendState, nullptr, 0xffffffff);
    g_pD3DContext->OMSetDepthStencilState(g_pDepthNoWrite, 0);
    g_pD3DContext->RSSetState(g_pRSCullNone);

    g_pD3DContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pD3DContext->PSSetShader(g_pPlanePS, nullptr, 0);
    g_pD3DContext->IASetInputLayout(g_pInputLayout);
    stride = sizeof(TexturedVertex);
    offset = 0;
    g_pD3DContext->IASetVertexBuffers(0, 1, &g_pPlaneVertexBuffer, &stride, &offset);
    g_pD3DContext->IASetIndexBuffer(g_pPlaneIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pD3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    g_pD3DContext->PSSetShaderResources(0, 1, nullSRV);

    for (int i = 0; i < 2; ++i)
    {
        ModelConstantBuffer modelDataPlane;
        XMStoreFloat4x4((XMFLOAT4X4*)&modelDataPlane.model, XMMatrixTranspose(planes[i].world));
        g_pD3DContext->UpdateSubresource(planes[i].modelBuffer, 0, nullptr, &modelDataPlane, 0, 0);

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
    SAFE_RELEASE(g_pSceneCB);
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
    SAFE_RELEASE(g_pNormalMapView);


    SAFE_RELEASE(g_pInstancedVS);
    SAFE_RELEASE(g_pInstancedPS);
    SAFE_RELEASE(g_pInstancedInputLayout);
    SAFE_RELEASE(g_pGeomBufferInst);
    SAFE_RELEASE(g_pVisibleIdsBuffer);
    SAFE_RELEASE(g_pTextureArrayView);

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