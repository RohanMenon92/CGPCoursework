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
    float pi = 3.14159265359f;
    float radiansFactor = pi / 180.0f;

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

    BloomPresets g_Bloom = Blurry;

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

Game::~Game()
{
    if (m_audEngine)
    {
        m_audEngine->Suspend();
    }
}

void Game::InitializeSounds() {
    AUDIO_ENGINE_FLAGS eflags = AudioEngine_Default;
#ifdef _DEBUG
    eflags = eflags | AudioEngine_Debug;
#endif

    m_audEngine = std::make_unique<AudioEngine>(eflags);
    sound_spaceShip = std::make_unique<SoundEffect>(m_audEngine.get(), L"Sounds/positionalShip.wav");
    sound_ambient = std::make_unique<SoundEffect>(m_audEngine.get(), L"Sounds/Birds.wav");

    sound_spaceShip_instance = sound_spaceShip->CreateInstance(SoundEffectInstance_Use3D);
    sound_spaceShip_instance->SetVolume(1.f);
    sound_ambient_instance = sound_ambient->CreateInstance();
    sound_ambient_instance->SetVolume(0.3f);
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    m_keyboard = std::make_unique<Keyboard>();
    m_mouse = std::make_unique<Mouse>();
    m_mouse->SetWindow(window);
    InitializeSounds();

    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();

    CreateDeviceDependentResources();

    // Creating depth stencil target here
    m_deviceResources->CreateWindowSizeDependentResources();

    // Obtain the backbuffer for this window which will be the final 3D rendertarget.
    DX::ThrowIfFailed(m_deviceResources->GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D),
        &m_backBuffer));

    // Setting renderTargetView
    DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_backBuffer.Get(), nullptr,
        m_renderTargetView.ReleaseAndGetAddressOf()));

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

void Game::TakeInput()
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

void Game::CalculateAudioProperties() {
    if (m_retryAudio) {
        m_retryAudio = false;
        if (m_audEngine->Reset()) {

        }
    } else  if (!m_audEngine->Update())
    {
        if (m_audEngine->IsCriticalError())
        {
            // Make sure audio loops on losing device audio
            m_retryAudio = true;
        }
    }

    AudioListener listener;
    listener.SetPosition(m_cameraPos);

    AudioEmitter emitter;
    emitter.SetPosition(Vector3(0, 0, 0));
    // Loop Sounds
    sound_spaceShip_instance->Apply3D(listener, emitter, false);

    if (sound_spaceShip_instance->GetState() != SoundState::PLAYING) {
        sound_spaceShip_instance->Play();
    }

    if (sound_ambient_instance->GetState() != SoundState::PLAYING) {
        sound_ambient_instance->Play(true);
    }
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    CalculateAudioProperties();

    TakeInput();

    float elapsedTime = float(timer.GetElapsedSeconds());
    float totalTime = float(timer.GetTotalSeconds());

    // TODO: Add your game logic here.
    //RotateSphere(totalTime);

    // Rotate factor to define render skulls
    DoRotateAnimation();
    DoReticleAnimation();
    DoSoundAnimation(totalTime);

    elapsedTime;
}

void Game::DoSoundAnimation(float totalTime) {
    // Do pitch blending
    sound_ambient_instance->SetPitch(cos(totalTime/2));
}

void Game::DoRotateAnimation() {
    if (rotationFactor >= 360) {
        rotationFactor = 0;
    }
    else {
        rotationFactor += 0.2f;
    }


    if (lightRotationFactor >= 360) {
        lightRotationFactor = 0;
    }
    else {
        lightRotationFactor += 0.02f;
    }
}

void Game::DoReticleAnimation() {
    if (reticleDisplacement > 30.f || reticleDisplacement < -20.f) {
        reticleDisplacement = reticleOut ? 30 : -20;
        reticleOut = !reticleOut;
    }
    else {
        reticleDisplacement += reticleOut ? 0.5 : -0.5;
    }
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
    RenderSpriteBatch(); // Create BackGround
    RenderShape(); // Render ring structure
    RenderRoom(); // Render Room
    RenderBodys(); // Render body models
    RenderShip(); // Render ship model
    RenderSkulls(); // Render Skull models

    RenderAimReticle(); // Render Aiming Reticle

    context;

    m_deviceResources->PIXEndEvent();

    //Do PostProcessing and apply to RenderTarget
    PostProcess();

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
    m_world *= Matrix::CreateRotationY(rotationFactor * radiansFactor);
    primitiveShape->Draw(m_world, m_view, m_proj, Colors::White, ring_texture.Get());
    m_world = Matrix::Identity;
}

void Game::RenderShip() {
    Quaternion q = Quaternion::CreateFromYawPitchRoll(lightRotationFactor, 3.f, 0.f);
    modelShip->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            lights->SetLightEnabled(0, true);
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetLightDirection(0, dir);
            lights->SetAmbientLightColor(Colors::Blue);
            lights->SetLightDiffuseColor(0, Colors::LightBlue);
        }
    });

    m_world *= Matrix::CreateScale(0.005f);
    m_world *= Matrix::CreateTranslation(0.0f, -5.0f, 1.0f);
    m_world *= Matrix::CreateRotationY(45.f * radiansFactor);
    modelShip->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;
}

void Game::RenderSkulls()
{
    Quaternion q = Quaternion::CreateFromYawPitchRoll(m_yaw, m_pitch, 0.f);
    modelSkull1->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetLightDirection(0, dir / 2.f);
            lights->SetAmbientLightColor(Colors::DarkGoldenrod);
        }
    });

    m_world *= Matrix::CreateTranslation(-5.0f, 2.0f, -5.0f);
    m_world *= Matrix::CreateRotationY(rotationFactor * radiansFactor);
    modelSkull1->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;

    modelSkull1->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetLightDirection(0, dir / 2.f);
            lights->SetAmbientLightColor(Colors::DarkGreen);
        }
    });

    m_world *= Matrix::CreateTranslation(5.0f, 2.0f, -5.0f);
    m_world *= Matrix::CreateRotationY(rotationFactor * radiansFactor);
    modelSkull2->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;
}

void Game::RenderBodys()
{
    Quaternion q = Quaternion::CreateFromYawPitchRoll(lightRotationFactor, 0, 0.f);

    modelBody1->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetLightEnabled(0, true);
            lights->SetLightEnabled(1, false);
            lights->SetLightDirection(0, dir / -2.f);
            lights->SetAmbientLightColor(Colors::Gray);
            lights->SetLightDiffuseColor(0, Colors::Green);
        }
        auto fog = dynamic_cast<IEffectFog*>(effect);
        if (fog)
        {
            fog->SetFogEnabled(true);
            fog->SetFogStart(5); // assuming RH coordiantes
            fog->SetFogEnd(12);
            fog->SetFogColor(Colors::Blue);
        }
    });

    m_world *= Matrix::CreateScale(0.01f);
    m_world *= Matrix::CreateTranslation(-5.0f, -5.5f, 1.0f);
    m_world *= Matrix::CreateRotationY(45.f * radiansFactor);
    modelBody1->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;

    modelBody2->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetAmbientLightColor(Colors::Gray);
            lights->SetLightEnabled(0, true);
            lights->SetLightEnabled(1, true);
            lights->SetLightDirection(1, dir / 2.f);
            lights->SetLightDirection(0, dir / -2.f);
            lights->SetLightDiffuseColor(0, Colors::Red);
            lights->SetLightDiffuseColor(1, Colors::Yellow);
        }
        auto fog = dynamic_cast<IEffectFog*>(effect);
        if (fog)
        {
            fog->SetFogEnabled(true);
            fog->SetFogStart(5); // assuming RH coordiantes
            fog->SetFogEnd(12);
            fog->SetFogColor(Colors::Yellow);
        }
    });

    m_world *= Matrix::CreateScale(0.01f);
    m_world *= Matrix::CreateRotationY(45.f * radiansFactor);
    m_world *= Matrix::CreateTranslation(-2.0f, -5.5f, 1.0f);
    m_world *= Matrix::CreateRotationY(135.f * radiansFactor);
    modelBody2->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;

    modelBody3->UpdateEffects([&](IEffect* effect)
    {
        auto lights = dynamic_cast<IEffectLights*>(effect);
        if (lights)
        {
            XMVECTOR dir = XMVector3Rotate(g_XMOne, q);
            lights->SetAmbientLightColor(Colors::Gray);
            lights->SetLightEnabled(0, false);
            lights->SetLightEnabled(1, false);
        }
        auto fog = dynamic_cast<IEffectFog*>(effect);
        if (fog)
        {
            fog->SetFogEnabled(true);
            fog->SetFogStart(5); // assuming RH coordiantes
            fog->SetFogEnd(12);
            fog->SetFogColor(Colors::Yellow);
        }
    });

    m_world *= Matrix::CreateScale(0.01f);
    m_world *= Matrix::CreateRotationY(45.f * radiansFactor);
    m_world *= Matrix::CreateTranslation(-6.5f, -5.5f, 1.0f);
    m_world *= Matrix::CreateRotationY(90.f * radiansFactor);
    modelBody3->Draw(m_deviceResources->GetD3DDeviceContext(), *m_States, m_world, m_view, m_proj);
    m_world = Matrix::Identity;
}

void Game::RenderRoom()
{
    primitiveCube->Draw(Matrix::Identity, m_view, m_proj, Colors::White, room_texture.Get());
}

void Game::RenderAimReticle() {
    auto context = m_deviceResources->GetD3DDeviceContext();

    // Render polygon for aim reticle
    context->OMSetBlendState(m_States->AlphaBlend(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_States->DepthNone(), 0);
    context->RSSetState(m_States->CullNone());

    m_ReticleEffect->Apply(context);

    context->IASetInputLayout(m_inputLayout.Get());

    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right / 2;
    float height = screenSize.bottom / 2;

    std::vector<VertexPositionColor> aimReticlePoints = {
        //Triangle1
        VertexPositionColor(Vector3(width / 2, - reticleDisplacement + height/2 - 20.f, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 - 30.f, height / 2 - 80.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 + 30.f, height / 2 - 80.f, 0.5f), Colors::Transparent),

        //Triangle2
        VertexPositionColor(Vector3(reticleDisplacement + width / 2 + 20.f, height / 2, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 + 80.f, height / 2 - 30.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 + 80.f, height / 2 + 30.f, 0.5f), Colors::Transparent),

        //Triangle3
        VertexPositionColor(Vector3(width / 2, reticleDisplacement + height / 2 + 20.f, 0.5f), Colors::Green),
        VertexPositionColor(Vector3(width / 2 + 30.f, height / 2 + 80.f, 0.5f), Colors::Transparent),
        VertexPositionColor(Vector3(width / 2 - 30.f, height / 2 + 80.f, 0.5f), Colors::Transparent),

        //Triangle4
        VertexPositionColor(Vector3(-reticleDisplacement + width / 2 - 20.f, height / 2, 0.5f), Colors::Green),
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

void Game::PostProcess()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto deviceContext = m_deviceResources->GetD3DDeviceContext();

    ID3D11ShaderResourceView* null[] = { nullptr, nullptr };

    if (g_Bloom == None)
    {
        // Pass-through test
        deviceContext->CopyResource(m_backBuffer.Get(), m_sceneTex.Get());
    }
    else
    {
        // scene -> RT1 (downsample)
        deviceContext->OMSetRenderTargets(1, m_rt1RT.GetAddressOf(), nullptr);
        m_spriteBatch->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
            deviceContext->PSSetConstantBuffers(0, 1, m_bloomParams.GetAddressOf());
            deviceContext->PSSetShader(m_bloomExtractPS.Get(), nullptr, 0);
        });
        m_spriteBatch->Draw(m_sceneSRV.Get(), m_bloomRect);
        m_spriteBatch->End();

        // RT1 -> RT2 (blur horizontal)
        deviceContext->OMSetRenderTargets(1, m_rt2RT.GetAddressOf(), nullptr);
        m_spriteBatch->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
            deviceContext->PSSetShader(m_gaussianBlurPS.Get(), nullptr, 0);
            deviceContext->PSSetConstantBuffers(0, 1,
                m_blurParamsWidth.GetAddressOf());
        });
        m_spriteBatch->Draw(m_rt1SRV.Get(), m_bloomRect);
        m_spriteBatch->End();

        deviceContext->PSSetShaderResources(0, 2, null);

        // RT2 -> RT1 (blur vertical)
        deviceContext->OMSetRenderTargets(1, m_rt1RT.GetAddressOf(), nullptr);
        m_spriteBatch->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
            deviceContext->PSSetShader(m_gaussianBlurPS.Get(), nullptr, 0);
            deviceContext->PSSetConstantBuffers(0, 1,
                m_blurParamsHeight.GetAddressOf());
        });
        m_spriteBatch->Draw(m_rt2SRV.Get(), m_bloomRect);
        m_spriteBatch->End();

        // RT1 + scene
        deviceContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
        m_spriteBatch->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
            deviceContext->PSSetShader(m_bloomCombinePS.Get(), nullptr, 0);
            deviceContext->PSSetShaderResources(1, 1, m_sceneSRV.GetAddressOf());
            deviceContext->PSSetConstantBuffers(0, 1, m_bloomParams.GetAddressOf());
        });
        m_spriteBatch->Draw(m_rt1SRV.Get(), m_fullscreenRect);
        m_spriteBatch->End();
    }

    deviceContext->PSSetShaderResources(0, 2, null);
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();

    context->ClearRenderTargetView(m_renderTargetView.Get(), Colors::Black);
    context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    // Using new RenderTarget here
    context->OMSetRenderTargets(1, m_sceneRT.GetAddressOf(), m_deviceResources->GetDepthStencilView());

    // Set the viewport.
    auto viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
void Game::LoadTextures()
{
    auto device = m_deviceResources->GetD3DDevice();

    DX::ThrowIfFailed(
        CreateWICTextureFromFile(device,
        L"Textures/sunset.jpg", nullptr,
        m_background.ReleaseAndGetAddressOf()));

    DX::ThrowIfFailed(
        CreateDDSTextureFromFile(device, L"Textures/roomtexture.dds", nullptr,
            room_texture.ReleaseAndGetAddressOf()));

    DX::ThrowIfFailed(
        CreateWICTextureFromFile(device, L"Textures/earth.bmp", nullptr,
            ring_texture.ReleaseAndGetAddressOf()));

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
    m_audEngine->Suspend();
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_audEngine->Resume();
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

    m_ReticleEffect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

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
    m_ReticleEffect->SetProjection(proj);
    // End initializing state and effects for Triangle Render
}

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto swapchain = m_deviceResources->GetSwapChain();

    // TODO:(CreateDevice)
    // Initialize device dependent objects here (independent of window size).x     
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

    m_States = std::make_unique<CommonStates>(device);
    m_ReticleEffect = std::make_unique<BasicEffect>(device);
    m_ReticleEffect->SetVertexColorEnabled(true);


    m_fxFactory1 = std::make_unique<EffectFactory>(device);
    m_fxFactory2 = std::make_unique<EffectFactory>(device);

    m_world = Matrix::Identity;
}



#pragma region Direct3D Resources
// These are the resources that depend on the size.
void Game::CreateWindowSizeDependentResources()
{
    auto screenSize = m_deviceResources->GetOutputSize();
    float width = screenSize.right;
    float height = screenSize.bottom;

    // TO DO (CreateResources)
    m_fullscreenRect.left = 0;
    m_fullscreenRect.top = 0;
    m_fullscreenRect.right = width;
    m_fullscreenRect.bottom = height;

    m_view = Matrix::CreateLookAt(Vector3(2.f, 2.f, 2.f), Vector3::Zero, Vector3::UnitY);
    m_proj = Matrix::CreatePerspectiveFieldOfView(XMConvertToRadians(70.f), width / height, 0.1f, 100.f);

    CreateBlurParameters(width, height);
    CreateRenderParameters(width, height);
}

void Game::CreateRenderParameters(float width, float height) {
    auto device = m_deviceResources->GetD3DDevice();
    auto backBufferFormat = m_deviceResources->GetBackBufferFormat();

    // Full-size render target for scene
    CD3D11_TEXTURE2D_DESC sceneDesc(backBufferFormat, width, height,
        1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    DX::ThrowIfFailed(device->CreateTexture2D(&sceneDesc, nullptr,
        m_sceneTex.GetAddressOf()));
    DX::ThrowIfFailed(device->CreateRenderTargetView(m_sceneTex.Get(), nullptr,
        m_sceneRT.ReleaseAndGetAddressOf()));
    DX::ThrowIfFailed(device->CreateShaderResourceView(m_sceneTex.Get(), nullptr,
        m_sceneSRV.ReleaseAndGetAddressOf()));

    // Half-size blurring render targets
    CD3D11_TEXTURE2D_DESC rtDesc(backBufferFormat, width, height,
        1, 1, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtTexture1;
    DX::ThrowIfFailed(device->CreateTexture2D(&rtDesc, nullptr,
        rtTexture1.GetAddressOf()));
    DX::ThrowIfFailed(device->CreateRenderTargetView(rtTexture1.Get(), nullptr,
        m_rt1RT.ReleaseAndGetAddressOf()));
    DX::ThrowIfFailed(device->CreateShaderResourceView(rtTexture1.Get(), nullptr,
        m_rt1SRV.ReleaseAndGetAddressOf()));

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtTexture2;
    DX::ThrowIfFailed(device->CreateTexture2D(&rtDesc, nullptr,
        rtTexture2.GetAddressOf()));
    DX::ThrowIfFailed(device->CreateRenderTargetView(rtTexture2.Get(), nullptr,
        m_rt2RT.ReleaseAndGetAddressOf()));
    DX::ThrowIfFailed(device->CreateShaderResourceView(rtTexture2.Get(), nullptr,
        m_rt2SRV.ReleaseAndGetAddressOf()));

    // Define size of texture
    m_bloomRect.left = 0;
    m_bloomRect.top = 0;
    m_bloomRect.right = width;
    m_bloomRect.bottom = height;
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
    auto device = m_deviceResources->GetD3DDevice();

    m_spriteBatch = std::make_unique<SpriteBatch>(context);
    primitiveShape = GeometricPrimitive::CreateTorus(context);

    primitiveCube = GeometricPrimitive::CreateBox(context,
        XMFLOAT3(ROOM_BOUNDS[0], ROOM_BOUNDS[1], ROOM_BOUNDS[2]),
        false, true);

    modelBody1 = Model::CreateFromSDKMESH(device, L"Mesh/body.sdkmesh", *m_fxFactory1);
    modelBody2 = Model::CreateFromSDKMESH(device, L"Mesh/body.sdkmesh", *m_fxFactory1);
    modelBody3 = Model::CreateFromSDKMESH(device, L"Mesh/body.sdkmesh", *m_fxFactory1);
    modelSkull1 = Model::CreateFromSDKMESH(device, L"Mesh/skull.sdkmesh", *m_fxFactory2);
    modelSkull2 = Model::CreateFromSDKMESH(device, L"Mesh/skull.sdkmesh", *m_fxFactory2);
    modelShip = Model::CreateFromSDKMESH(device, L"Mesh/spaceship.sdkmesh", *m_fxFactory2);
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
    modelBody1.reset();
    modelBody2.reset();
    modelBody3.reset();
    modelSkull1.reset();
    modelSkull2.reset();
    modelShip.reset();

    m_inputLayout.Reset();
    
    m_sceneTex.Reset();
    m_sceneSRV.Reset();
    m_sceneRT.Reset();
    m_rt1SRV.Reset();
    m_rt1RT.Reset();
    m_rt2SRV.Reset();
    m_rt2RT.Reset();
    m_backBuffer.Reset();

    m_bloomExtractPS.Reset();
    m_bloomCombinePS.Reset();
    m_gaussianBlurPS.Reset();

    m_bloomParams.Reset();
    m_blurParamsWidth.Reset();
    m_blurParamsHeight.Reset();

    m_States.reset();
    m_spriteBatch.reset();
    m_background.Reset();

    primitiveShape.reset();
    primitiveCube.reset();
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
