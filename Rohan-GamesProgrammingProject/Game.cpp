//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include <iostream>

extern void ExitGame();

using namespace DirectX;
using Microsoft::WRL::ComPtr;


namespace
{
    const XMVECTORF32 START_POSITION = { 0.f, -1.5f, 0.f, 0.f };
    const XMVECTORF32 ROOM_BOUNDS = { 16.f, 12.f, 24.f, 0.f };
    const float ROTATION_GAIN = 0.01f;
    const float MOVEMENT_GAIN = 0.07f;

    struct VS_BLOOM_PARAMETERS
    {
        float bloomThreshold;
        float blurAmount;
        float bloomIntensity;
        float baseIntensity;
        float bloomSaturation;
        float baseSaturation;
        uint8_t na[8];
    };

    static_assert(!(sizeof(VS_BLOOM_PARAMETERS) % 16),
        "VS_BLOOM_PARAMETERS needs to be 16 bytes aligned");

    struct VS_BLUR_PARAMETERS
    {
        static const size_t SAMPLE_COUNT = 15;

        XMFLOAT4 sampleOffsets[SAMPLE_COUNT];
        XMFLOAT4 sampleWeights[SAMPLE_COUNT];

        void SetBlurEffectParameters(float dx, float dy,
            const VS_BLOOM_PARAMETERS& params)
        {
            sampleWeights[0].x = ComputeGaussian(0, params.blurAmount);
            sampleOffsets[0].x = sampleOffsets[0].y = 0.f;

            float totalWeights = sampleWeights[0].x;

            // Add pairs of additional sample taps, positioned
            // along a line in both directions from the center.
            for (size_t i = 0; i < SAMPLE_COUNT / 2; i++)
            {
                // Store weights for the positive and negative taps.
                float weight = ComputeGaussian(float(i + 1.f), params.blurAmount);

                sampleWeights[i * 2 + 1].x = weight;
                sampleWeights[i * 2 + 2].x = weight;

                totalWeights += weight * 2;

                // To get the maximum amount of blurring from a limited number of
                // pixel shader samples, we take advantage of the bilinear filtering
                // hardware inside the texture fetch unit. If we position our texture
                // coordinates exactly halfway between two texels, the filtering unit
                // will average them for us, giving two samples for the price of one.
                // This allows us to step in units of two texels per sample, rather
                // than just one at a time. The 1.5 offset kicks things off by
                // positioning us nicely in between two texels.
                float sampleOffset = float(i) * 2.f + 1.5f;

                Vector2 delta = Vector2(dx, dy) * sampleOffset;

                // Store texture coordinate offsets for the positive and negative taps.
                sampleOffsets[i * 2 + 1].x = delta.x;
                sampleOffsets[i * 2 + 1].y = delta.y;
                sampleOffsets[i * 2 + 2].x = -delta.x;
                sampleOffsets[i * 2 + 2].y = -delta.y;
            }

            for (size_t i = 0; i < SAMPLE_COUNT; i++)
            {
                sampleWeights[i].x /= totalWeights;
            }
        }

    private:
        float ComputeGaussian(float n, float theta)
        {
            return (float)((1.0 / sqrtf(2 * XM_PI * theta))
                * expf(-(n * n) / (2 * theta * theta)));
        }
    };

    static_assert(!(sizeof(VS_BLUR_PARAMETERS) % 16),
        "VS_BLUR_PARAMETERS needs to be 16 bytes aligned");

    enum BloomPresets
    {
        Default = 0,
        Soft,
        Desaturated,
        Saturated,
        Blurry,
        Subtle,
        None
    };

    BloomPresets g_Bloom = Default;

    static const VS_BLOOM_PARAMETERS g_BloomPresets[] =
    {
        //Thresh  Blur Bloom  Base  BloomSat BaseSat
        { 0.25f,  4,   1.25f, 1,    1,       1 }, // Default
        { 0,      3,   1,     1,    1,       1 }, // Soft
        { 0.5f,   8,   2,     1,    0,       1 }, // Desaturated
        { 0.25f,  4,   2,     1,    2,       0 }, // Saturated
        { 0,      2,   1,     0.1f, 1,       1 }, // Blurry
        { 0.5f,   2,   1,     1,    1,       1 }, // Subtle
        { 0.25f,  4,   1.25f, 1,    1,       1 }, // None
    };
}

Game::Game() :
    m_pitch(0),
    m_yaw(0)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->RegisterDeviceNotify(this);
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    m_keyboard = std::make_unique<Keyboard>();
    m_mouse = std::make_unique<Mouse>();
    m_mouse->SetWindow(window);

    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();

    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();

    CreateWindowSizeDependentResources();
    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();
}

void Game::ReceiveInput()
{
    // Mouse Input
    auto mouse = m_mouse->GetState();

    if (mouse.positionMode == Mouse::MODE_RELATIVE)
    {
        Vector3 delta = Vector3(float(mouse.x), float(mouse.y), 0.f)
            * ROTATION_GAIN;

        m_pitch -= delta.y;
        m_yaw -= delta.x;

        // limit pitch to straight up or straight down
        // with a little fudge-factor to avoid gimbal lock
        float limit = XM_PI / 2.0f - 0.01f;
        m_pitch = std::max(-limit, m_pitch);
        m_pitch = std::min(+limit, m_pitch);

        // keep longitude in sane range by wrapping
        if (m_yaw > XM_PI)
        {
            m_yaw -= XM_PI * 2.0f;
        }
        else if (m_yaw < -XM_PI)
        {
            m_yaw += XM_PI * 2.0f;
        }
    }

    m_mouse->SetMode(mouse.leftButton ? Mouse::MODE_RELATIVE : Mouse::MODE_ABSOLUTE);

    // Keyboard Input
    auto kb = m_keyboard->GetState();
    if (kb.Escape)
    {
        ExitGame();
    }

    if (kb.Home)
    {
        m_cameraPos = START_POSITION.v;
        m_pitch = m_yaw = 0;
    }

    Vector3 move = Vector3::Zero;

    if (kb.Up || kb.Space)
        move.y += 1.f;

    if (kb.Down || kb.X)
        move.y -= 1.f;

    if (kb.Left || kb.A)
        move.x += 1.f;

    if (kb.Right || kb.D)
        move.x -= 1.f;

    if (kb.PageUp || kb.W)
        move.z += 1.f;

    if (kb.PageDown || kb.S)
        move.z -= 1.f;

    Quaternion q = Quaternion::CreateFromYawPitchRoll(m_yaw, -m_pitch, 0.f);

    move = Vector3::Transform(move, q);

    move *= MOVEMENT_GAIN;

    m_cameraPos += move;

    Vector3 halfBound = (Vector3(ROOM_BOUNDS.v) / Vector3(2.f))
        - Vector3(0.1f, 0.1f, 0.1f);

    m_cameraPos = Vector3::Min(m_cameraPos, halfBound);
    m_cameraPos = Vector3::Max(m_cameraPos, -halfBound);
}





// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    ReceiveInput();

    float elapsedTime = float(timer.GetElapsedSeconds());
    float totalTime = float(timer.GetTotalSeconds());

    // TODO: Add your game logic here.
    //RotateSphere(totalTime);

    elapsedTime;
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();

    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();

    float y = sinf(m_pitch);
    float r = cosf(m_pitch);
    float z = r * cosf(m_yaw);
    float x = r * sinf(m_yaw);

    XMVECTOR lookAt = m_cameraPos + Vector3(x, y, z);

    m_view = XMMatrixLookAtRH(m_cameraPos, lookAt, Vector3::Up);

    // TODO: Add your rendering code here.
    RenderSpriteBatch();
    RenderShape();
    RenderRoom();
    RenderTeapot();
    RenderAimReticle();

    context;

    m_deviceResources->PIXEndEvent();

    // Show the new frame.
    m_deviceResources->Present();
}

void Game::RenderSpriteBatch()
{
    m_spriteBatch->Begin();
    m_spriteBatch->Draw(m_background.Get(), m_fullscreenRect);
    m_spriteBatch->End();
}

void Game::RenderShape()
{
    modelShape->Draw(m_world, m_view, m_proj);
}



void Game::RenderTeapot()
{
    //modelTeapot->CreateInputLayout(m_env_effect.get(), m_inputLayout.ReleaseAndGetAddressOf());
}

void Game::RenderRoom()
{
    modelCube->Draw(Matrix::Identity, m_view, m_proj, Colors::White, room_texture.Get());
}

void Game::RenderAimReticle() {
    auto context = m_deviceResources->GetD3DDeviceContext();

    // Render polygon for aim reticle
    context->OMSetBlendState(m_states->AlphaBlend(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_states->DepthNone(), 0);
    context->RSSetState(m_states->CullNone());

    m_effect->Apply(context);

    context->IASetInputLayout(m_inputLayout.Get());

    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right / 2;
    float height = screenSize.bottom / 2;

    std::vector<VertexPositionColor> aimReticlePoints = {
        //Triangle1
        VertexPositionColor(Vector3(width / 2, height/2 - 20.f, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 - 30.f, height / 2 - 80.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 + 30.f, height / 2 - 80.f, 0.5f), Colors::Transparent),

        //Triangle2
        VertexPositionColor(Vector3(width / 2 + 20.f, height / 2, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 + 80.f, height / 2 - 30.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 + 80.f, height / 2 + 30.f, 0.5f), Colors::Transparent),

        //Triangle3
        VertexPositionColor(Vector3(width / 2, height / 2 + 20.f, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 + 30.f, height / 2 + 80.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 - 30.f, height / 2 + 80.f, 0.5f), Colors::Transparent),

        //Triangle4
        VertexPositionColor(Vector3(width / 2 - 20.f, height / 2, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 - 80.f, height / 2 + 30.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 - 80.f, height / 2 - 30.f, 0.5f), Colors::Transparent)
    };

    // Loop through 3 at a time to create reticle
    m_batch->Begin();
    for (std::vector<VertexPositionColor>::size_type i = 0; i != aimReticlePoints.size(); i += 3)
    {
        m_batch->DrawTriangle(aimReticlePoints[i], aimReticlePoints[i + 1], aimReticlePoints[i + 2]);
    }
    m_batch->End();

    // End render polygon
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    auto viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
void Game::LoadTextures()
{
    auto device = m_deviceResources->GetD3DDevice();

    DX::ThrowIfFailed(CreateWICTextureFromFile(device,
        L"sunset.jpg", nullptr,
        m_background.ReleaseAndGetAddressOf()));

    //DX::ThrowIfFailed(
    //    CreateDDSTextureFromFile(device, L"cubemap.dds", nullptr,
    //        cube_map_texture.ReleaseAndGetAddressOf()));

    DX::ThrowIfFailed(
        CreateDDSTextureFromFile(device, L"Textures/roomtexture.dds",
            nullptr, room_texture.ReleaseAndGetAddressOf()));

    //DX::ThrowIfFailed(
    //    CreateDDSTextureFromFile(device, L"porcelain.dds", nullptr,
    //        teapot_texture.ReleaseAndGetAddressOf()));

}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
    auto r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();
    // TODO: Game window is being resized.
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 1600;
    height = 900;
}
#pragma endregion

void Game::AimReticleCreateBatch() {
    auto device = m_deviceResources->GetD3DDevice();

    // For initializing state and effects for Triangle Render

    void const* shaderByteCode;
    size_t byteCodeLength;

    m_effect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateInputLayout(VertexPositionColor::InputElements,
        VertexPositionColor::InputElementCount,
        shaderByteCode, byteCodeLength,
        m_inputLayout.ReleaseAndGetAddressOf()));

    m_batch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(m_deviceResources->GetD3DDeviceContext());

    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right/2;
    float height = screenSize.bottom/2;

    Matrix proj = Matrix::CreateScale(2.f / width,
        -2.f / height, 1.f)
        * Matrix::CreateTranslation(-1.f, 1.f, 0.f);
    m_effect->SetProjection(proj);
    // End initializing state and effects for Triangle Render
}

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto context = m_deviceResources->GetD3DDeviceContext();
    
    // TODO:(CreateDevice)
    // Initialize device dependent objects here (independent of window size).x 
    m_states = std::make_unique<CommonStates>(device);
    
    LoadTextures();
    ReadShaders();
    CreateEffects();

    Create3DModels();
    AimReticleCreateBatch();
    
    device;
}

// Read Shader data
void Game::ReadShaders() {
    auto device = m_deviceResources->GetD3DDevice();

    // Extract the shader files
    auto blob = DX::ReadData(L"BloomExtract.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_bloomExtractPS.ReleaseAndGetAddressOf()));

    blob = DX::ReadData(L"BloomCombine.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_bloomCombinePS.ReleaseAndGetAddressOf()));

    blob = DX::ReadData(L"GaussianBlur.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_gaussianBlurPS.ReleaseAndGetAddressOf()));

    {
        CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLOOM_PARAMETERS),
            D3D11_BIND_CONSTANT_BUFFER);
        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = &g_BloomPresets[g_Bloom];
        initData.SysMemPitch = sizeof(VS_BLOOM_PARAMETERS);
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, &initData,
            m_bloomParams.ReleaseAndGetAddressOf()));
    }

    {
        CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLUR_PARAMETERS),
            D3D11_BIND_CONSTANT_BUFFER);
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, nullptr,
            m_blurParamsWidth.ReleaseAndGetAddressOf()));
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, nullptr,
            m_blurParamsHeight.ReleaseAndGetAddressOf()));
    }
}

void Game::CreateEffects()
{
    auto device = m_deviceResources->GetD3DDevice();

    m_effect = std::make_unique<BasicEffect>(device);
    m_effect->SetVertexColorEnabled(true);



    //m_env_effect = std::make_unique<EnvironmentMapEffect>(device);
    //m_env_effect->EnableDefaultLighting();

    //modelTeapot->CreateInputLayout(m_env_effect.get(),
    //    m_inputLayout.ReleaseAndGetAddressOf());

    //m_env_effect->SetTexture(teapot_texture.Get());
    //m_env_effect->SetEnvironmentMap(cube_map_texture.Get());

}



#pragma region Direct3D Resources
// These are the resources that depend on the size.
void Game::CreateWindowSizeDependentResources()
{
    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right / 2;
    float height = screenSize.bottom / 2;

    // TO DO (CreateResources)
    m_fullscreenRect.left = 0;
    m_fullscreenRect.top = 0;
    m_fullscreenRect.right = width;
    m_fullscreenRect.bottom = height;
    m_view = Matrix::CreateLookAt(Vector3(2.f, 2.f, 2.f), Vector3::Zero, Vector3::UnitY);
    m_proj = Matrix::CreatePerspectiveFieldOfView(XMConvertToRadians(70.f), width / height, 0.1f, 100.f);

    CreateBlurParameters(width, height);
}

void Game::CreateBlurParameters(float width, float height) {
    auto deviceContext = m_deviceResources->GetD3DDeviceContext();

    VS_BLUR_PARAMETERS blurData;
    blurData.SetBlurEffectParameters(1.f / (width / 2), 0,
        g_BloomPresets[g_Bloom]);
    deviceContext->UpdateSubresource(m_blurParamsWidth.Get(), 0, nullptr,
        &blurData, sizeof(VS_BLUR_PARAMETERS), 0);

    blurData.SetBlurEffectParameters(0, 1.f / (width / 2),
        g_BloomPresets[g_Bloom]);
    deviceContext->UpdateSubresource(m_blurParamsHeight.Get(), 0, nullptr,
        &blurData, sizeof(VS_BLUR_PARAMETERS), 0);
}

void Game::Create3DModels() {
    auto context = m_deviceResources->GetD3DDeviceContext();

    m_spriteBatch = std::make_unique<SpriteBatch>(context);
    modelShape = GeometricPrimitive::CreateTorus(context);

    modelCube = GeometricPrimitive::CreateBox(context,
        XMFLOAT3(ROOM_BOUNDS[0], ROOM_BOUNDS[1], ROOM_BOUNDS[2]),
        false, true);
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
    m_bloomExtractPS.Reset();
    m_bloomCombinePS.Reset();
    m_gaussianBlurPS.Reset();

    m_bloomParams.Reset();
    m_blurParamsWidth.Reset();
    m_blurParamsHeight.Reset();

    m_states.reset();
    m_spriteBatch.reset();
    m_background.Reset();

    modelShape.reset();
    modelCube.reset();
    room_texture.Reset();
    body_colour_texture.Reset();
    body_normal_texture.Reset();
    body_emissive_texture.Reset();
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}
#pragma endregion
