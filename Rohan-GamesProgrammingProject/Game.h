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
    void GetDefaultSize( int& width, int& height ) const;

    void AimReticleCreateBatch();

private:
    void ReceiveInput();
    void Update(DX::StepTimer const& timer);
    void RotateSphere(float elapsedTime);

    void Render();
    void RenderSphere();
    void RenderRoom();
    void RenderAimReticle();

    void Clear();

    void LoadTextures();
    
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void Create3DModels();
    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                           m_timer;

    // For rendering aim reticle
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<DirectX::BasicEffect> m_effect;
    std::unique_ptr<PrimitiveBatch<VertexPositionColor>> m_batch;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    // End Aim Reticle

    // For 3D Shapes
    DirectX::SimpleMath::Matrix sphere_world; // World Matrix
    DirectX::SimpleMath::Matrix m_view; // View Matrix
    DirectX::SimpleMath::Matrix m_proj; // Projection mAtrix
    std::unique_ptr<DirectX::GeometricPrimitive> m_sphere;

    std::unique_ptr<DirectX::GeometricPrimitive> m_room;
    // End 3D Shapes

    // Camera
    DirectX::SimpleMath::Vector3 m_cameraPos;
    float m_pitch;
    float m_yaw;
    
    // Textures
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> earth_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> room_texture;

};
