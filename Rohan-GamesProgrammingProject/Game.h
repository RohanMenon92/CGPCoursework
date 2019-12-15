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
    void ReceiveInput();
    void Update(DX::StepTimer const& timer);

    void Render();
    void RenderSpriteBatch();
    void RenderShape();
    void RenderRoom();
    void RenderTeapot();
    void RenderAimReticle();

    void Clear();

    void LoadTextures();

    void CreateDeviceDependentResources();
    void CreateEffects();
    void ReadShaders();
    void CreateWindowSizeDependentResources();
    void Create3DModels();
    void CreateBlurParameters(float width, float height);
    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer m_timer;

    // For rendering aim reticle
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont> m_font;
    
    //std::unique_ptr<DirectX::EffectFactory> m_fxFactory;
    std::unique_ptr<DirectX::BasicEffect> m_effect;
    //std::unique_ptr<DirectX::EnvironmentMapEffect> m_env_effect;

    std::unique_ptr<PrimitiveBatch<VertexPositionColor>> m_batch;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    // End Aim Reticle

    // For 3D Shapes
    DirectX::SimpleMath::Matrix m_world; // World Matrix
    DirectX::SimpleMath::Matrix m_view; // View Matrix
    DirectX::SimpleMath::Matrix m_proj; // Projection mAtrix

    RECT m_fullscreenRect;
    RECT spriteDrawingRect;

    RenderTexture* m_CreateRenderPass;

    // Camera
    DirectX::SimpleMath::Vector3 m_cameraPos;
    float m_pitch;
    float m_yaw;

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
    std::unique_ptr<DirectX::Model> modelBody;
    std::unique_ptr<DirectX::Model> modelGoblin;
    std::unique_ptr<DirectX::Model> modelNanosuit;
    std::unique_ptr<DirectX::Model> modelPlanet;
    std::unique_ptr<DirectX::GeometricPrimitive> modelCube;
    std::unique_ptr<DirectX::GeometricPrimitive> modelShape;
    std::unique_ptr<DirectX::GeometricPrimitive> modelTeapot;

    // Room Textures
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> room_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cube_map_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> teapot_texture;

    // Create Body Effect
    std::unique_ptr<DirectX::BasicEffect> body_effect;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_colour_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_normal_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> body_emissive_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_background;

};
