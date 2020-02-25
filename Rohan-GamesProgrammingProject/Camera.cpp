#include <Camera.h>
#include <SimpleMath.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;

const float ROTATION_GAIN = 0.01f;
const float MOVEMENT_GAIN = 3.f;

// Creates a camera at the specified position
Camera::Camera(float x, float y, float z) :
	m_Pitch(0),
	m_Yaw(0)
{
	position = XMFLOAT3(x, y, z);
	startPosition = XMFLOAT3(x, y, z);
	XMStoreFloat4(&rotation, XMQuaternionIdentity());
	xRotation = 0;
	yRotation = 0;

	XMStoreFloat4x4(&viewMatrix, XMMatrixIdentity());
	XMStoreFloat4x4(&projMatrix, XMMatrixIdentity());

	// For Camera controls
	m_keyboard = std::make_unique<Keyboard>();
	m_mouse = std::make_unique<Mouse>();
}

// Nothing to really do
Camera::~Camera()
{ }


// Moves the camera relative to its orientation
void Camera::MoveRelative(float x, float y, float z)
{
	// Rotate the desired movement vector
	XMVECTOR dir = XMVector3Rotate(XMVectorSet(x, y, z, 0), XMLoadFloat4(&rotation));

	// Move in that direction
	XMStoreFloat3(&position, XMLoadFloat3(&position) + dir);
}

// Moves the camera in world space (not local space)
void Camera::MoveAbsolute(float x, float y, float z)
{
	// Simple add, no need to load/store
	position.x += x;
	position.y += y;
	position.z += z;
}

// Rotate on the X and/or Y axis
void Camera::Rotate(float x, float y)
{
	// Adjust the current rotation
	xRotation += x;
	yRotation += y;

	// Clamp the x between PI/2 and -PI/2
	xRotation = max(min(xRotation, XM_PIDIV2), -XM_PIDIV2);

	// Recreate the quaternion
	XMStoreFloat4(&rotation, XMQuaternionRotationRollPitchYaw(xRotation, yRotation, 0));
}

void Camera::Initialize()
{
	//m_mouse->SetWindow(window);
}

// Camera's update, which looks for key presses
void Camera::Update(float dt)
{
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
		m_pitch = max(-limit, m_pitch);
		m_pitch = min(+limit, m_pitch);

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

	// Keyboard input calculation

	// Current speed
	float speed = dt * MOVEMENT_GAIN;

	auto kb = m_keyboard->GetState();

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

	MoveRelative(move.x, move.y, move.z);

	// Update the view every frame - could be optimized
	UpdateViewMatrix();
}

// Creates a new view matrix based on current position and orientation
void Camera::UpdateViewMatrix()
{
	// Rotate the standard "forward" matrix by our rotation
	// This gives us our "look direction"
	XMVECTOR dir = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&rotation));

	XMMATRIX view = XMMatrixLookToLH(
		XMLoadFloat3(&position),
		dir,
		XMVectorSet(0, 1, 0, 0));

	XMStoreFloat4x4(&viewMatrix, XMMatrixTranspose(view));
}

// Updates the projection matrix
void Camera::UpdateProjectionMatrix(float aspectRatio)
{
	XMMATRIX P = XMMatrixPerspectiveFovLH(
		0.25f * XM_PI,		// Field of View Angle
		aspectRatio,		// Aspect ratio
		0.1f,				// Near clip plane distance
		100.0f);			// Far clip plane distance
	XMStoreFloat4x4(&projMatrix, XMMatrixTranspose(P)); // Transpose for HLSL!
}
