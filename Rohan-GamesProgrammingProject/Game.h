//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"

#include <CommonStates.h>
#include <PrimitiveBatch.h>
#include <Effects.h>
#include <SimpleMath.h>

#include <vector>
#include <Effects.h>
#include <VertexTypes.h>
#include <GeometricPrimitive.h>
#include <WICTextureLoader.h>
#include <DDSTextureLoader.h>
#include <Mouse.h>
#include <Keyboard.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>
#include <RenderTexture.h>
#include <Model.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;


// A basic game implementation that creates a D3D11 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:
    std::unique_ptr<DirectX::Keyboard> m_keyboard;
    std::unique_ptr<DirectX::Mouse> m_mouse;

    Game() noexcept(false);
    ~Game();

    void InitializeSounds();

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnWindowSizeChanged(int width, int height);

    // Properties
    void GetDefaultSize(int& width, int& height) const;

    void AimReticleCreateBatch();

private:
    void TakeInput();
    void CalculateAudioProperties();
    void Update(DX::StepTimer const& timer);

    void DoSoundAnimation(float totalTime);

    void DoRotateAnimation();

    void DoReticleAnimation();


    void Render();
    void PostProcess();
    void RenderSpriteBatch();
    void RenderShape();
    void RenderShip();
    void RenderSkulls();
    void RenderRoom();
    void RenderBodys();
    void RenderAimReticle();

    void Clear();

    void LoadTextures();

    void CreateDeviceDependentResources();
    void CreateEffects();
    void ReadShaders();
    void CreateWindowSizeDependentResources();
    void Create3DModels();
    void CreateBlurParameters(float width, float height);
    void CreateRenderParameters(float width, float height);
    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer m_timer;

    // For rendering aim reticle
    std::unique_ptr<DirectX::CommonStates> m_States;
    
    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont> m_font;
    

    std::unique_ptr<DirectX::IEffectFactory> m_fxFactory1;
    std::unique_ptr<DirectX::IEffectFactory> m_fxFactory2;
    std::unique_ptr<DirectX::BasicEffect> m_ReticleEffect;

    std::unique_ptr<PrimitiveBatch<VertexPositionColor>> m_batch;
    // End Aim Reticle

    // For 3D Shapes

    DirectX::SimpleMath::Matrix m_teapot_world;

    DirectX::SimpleMath::Matrix m_world; // World Matrix
    DirectX::SimpleMath::Matrix m_view; // View Matrix
    DirectX::SimpleMath::Matrix m_proj; // Projection mAtrix

    RECT m_fullscreenRect;
    RECT spriteDrawingRect;

    // Camera
    DirectX::SimpleMath::Vector3 m_cameraPos;
    float m_pitch;
    float m_yaw;


    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;

    // Shaders

     
    // Define Pixel Shaders
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_bloomExtractPS; // Extracts Pixel Data
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_bloomCombinePS; // Bloom Combine Pixel Data
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_gaussianBlurPS; // Guassian Blur on Pixel Data

    // Define Buffers
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_bloomParams;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_blurParamsWidth;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_blurParamsHeight;

    // Models
    std::unique_ptr<DirectX::Model> modelBody1;
    std::unique_ptr<DirectX::Model> modelBody2;
    std::unique_ptr<DirectX::Model> modelBody3;
    std::unique_ptr<DirectX::Model> modelSkull1;
    std::unique_ptr<DirectX::Model> modelSkull2;
    std::unique_ptr<DirectX::Model> modelShip;

    //std::unique_ptr<DirectX::Model> modelPlanet;
    std::unique_ptr<DirectX::GeometricPrimitive> primitiveCube;
    std::unique_ptr<DirectX::GeometricPrimitive> primitiveShape;


    // Room Textures
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> room_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ring_texture;

    // Create Body Effect
    std::unique_ptr<DirectX::BasicEffect> body_effect;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_colour_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_normal_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_emissive_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_background;

    // Create Blooming Effect
    // Create a buffer to load to
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_backBuffer;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneTex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneSRV;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_sceneRT;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_rt1SRV;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rt1RT;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_rt2SRV;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rt2RT;

    RECT m_bloomRect;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    float rotationFactor = 1;
    float lightRotationFactor = 1;

    float reticleDisplacement = 0;
    bool reticleOut = false;

    std::unique_ptr<DirectX::AudioEngine> m_audEngine;
    bool m_retryAudio;
    std::unique_ptr<DirectX::SoundEffect> sound_spaceShip;
    std::unique_ptr<DirectX::SoundEffect> sound_ambient;
    //std::unique_ptr<DirectX::SoundEffect> fire_laser;

    std::unique_ptr<DirectX::SoundEffectInstance> sound_spaceShip_instance;
    std::unique_ptr<DirectX::SoundEffectInstance> sound_ambient_instance;
    //std::unique_ptr<DirectX::SoundEffectInstance> fire_laser_instance;
};
