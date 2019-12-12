//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include <iostream>

extern void ExitGame();

using namespace DirectX;

namespace
{
    const XMVECTORF32 START_POSITION = { 0.f, -1.5f, 0.f, 0.f };
    const XMVECTORF32 ROOM_BOUNDS = { 16.f, 12.f, 24.f, 0.f };
    const float ROTATION_GAIN = 0.01f;
    const float MOVEMENT_GAIN = 0.07f;
}

using Microsoft::WRL::ComPtr;

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
    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();

    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();

    CreateWindowSizeDependentResources();

    m_keyboard = std::make_unique<Keyboard>();
    m_mouse = std::make_unique<Mouse>();
    m_mouse->SetWindow(window);
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
    RotateSphere(totalTime);

    elapsedTime;
}

void Game::RotateSphere(float time)
{
    
    sphere_world = Matrix::CreateRotationZ(cosf(time) * 1.f);
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
    RenderRoom();
    RenderSphere();
    RenderAimReticle();

    context;

    m_deviceResources->PIXEndEvent();

    // Show the new frame.
    m_deviceResources->Present();
}

void Game::RenderSphere() {
    m_sphere->Draw(sphere_world, m_view, m_proj, Colors::White, earth_texture.Get());
}

void Game::RenderRoom()
{
    m_room->Draw(Matrix::Identity, m_view, m_proj, Colors::White, room_texture.Get());
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
    DX::ThrowIfFailed(
        CreateDDSTextureFromFile(m_deviceResources->GetD3DDevice(), L"Textures/roomtexture.dds",
            nullptr, room_texture.ReleaseAndGetAddressOf()));

    DX::ThrowIfFailed(
        CreateWICTextureFromFile(m_deviceResources->GetD3DDevice(), L"Textures/earth.bmp", nullptr,
            earth_texture.ReleaseAndGetAddressOf()));

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
    m_states = std::make_unique<CommonStates>(device);

    m_effect = std::make_unique<BasicEffect>(device);
    m_effect->SetVertexColorEnabled(true);

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
    
    // TODO: Initialize device dependent objects here (independent of window size).x (CreateDevice)
    AimReticleCreateBatch();
    Create3DModels();
    LoadTextures();

    device;
}



#pragma region Direct3D Resources
// These are the resources that depend on the size.
void Game::CreateWindowSizeDependentResources()
{
    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right / 2;
    float height = screenSize.bottom / 2;

    // TO DO (CreateResources)
    m_view = Matrix::CreateLookAt(Vector3(2.f, 2.f, 2.f), Vector3::Zero, Vector3::UnitY);
    m_proj = Matrix::CreatePerspectiveFieldOfView(XMConvertToRadians(70.f), width / height, 0.1f, 100.f);
}

void Game::Create3DModels() {
    m_room = GeometricPrimitive::CreateBox(m_deviceResources->GetD3DDeviceContext(),
        XMFLOAT3(ROOM_BOUNDS[0], ROOM_BOUNDS[1], ROOM_BOUNDS[2]),
        false, true);
    m_sphere = GeometricPrimitive::CreateSphere(m_deviceResources->GetD3DDeviceContext());
    sphere_world = Matrix::Identity;
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
    m_sphere.reset();
    m_room.reset();
    earth_texture.Reset();
    room_texture.Reset();
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}
#pragma endregion
