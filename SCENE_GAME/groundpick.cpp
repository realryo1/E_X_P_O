#include "groundpick.h"

#include "camera.h"
#include "field.h"
#include "imgui/imgui.h"

using namespace DirectX;

bool GroundPick_BuildRay(float screenX, float screenY, XMVECTOR* origin, XMVECTOR* direction)
{
	Camera* camera = GetCamera();
	if (!camera)
	{
		return false;
	}
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	if (size.x <= 1.0f || size.y <= 1.0f)
	{
		return false;
	}
	const XMMATRIX view = camera->GetView();
	const XMMATRIX proj = camera->GetProjection();
	const XMVECTOR nearPoint = XMVector3Unproject(
		XMVectorSet(screenX, screenY, 0.0f, 1.0f),
		0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
		proj, view, XMMatrixIdentity());
	const XMVECTOR farPoint = XMVector3Unproject(
		XMVectorSet(screenX, screenY, 1.0f, 1.0f),
		0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
		proj, view, XMMatrixIdentity());
	*origin = nearPoint;
	*direction = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
	return true;
}

bool GroundPick_Pick(
	float screenX,
	float screenY,
	float maxDistance,
	float step,
	XMFLOAT3* outPoint)
{
	XMVECTOR origin;
	XMVECTOR direction;
	if (!GroundPick_BuildRay(screenX, screenY, &origin, &direction))
	{
		return false;
	}

	float previousT = 0.0f;
	bool previousAbove = false;
	for (float t = 0.0f; t <= maxDistance; t += step)
	{
		XMFLOAT3 point;
		XMStoreFloat3(&point, XMVectorAdd(origin, XMVectorScale(direction, t)));
		float groundY = 0.0f;
		if (!Field_SampleGroundY(point.x, point.z, &groundY))
		{
			previousAbove = false;
			continue;
		}

		const bool above = point.y > groundY;
		if (!above && previousAbove)
		{
			// previousT..t の間で床をまたいだので二分探索で詰める。
			float low = previousT;
			float high = t;
			for (int i = 0; i < 10; ++i)
			{
				const float mid = (low + high) * 0.5f;
				XMFLOAT3 midPoint;
				XMStoreFloat3(&midPoint, XMVectorAdd(origin, XMVectorScale(direction, mid)));
				float midGround = 0.0f;
				if (Field_SampleGroundY(midPoint.x, midPoint.z, &midGround) &&
					midPoint.y <= midGround)
				{
					high = mid;
				}
				else
				{
					low = mid;
				}
			}
			XMStoreFloat3(outPoint, XMVectorAdd(origin, XMVectorScale(direction, high)));
			Field_SampleGroundY(outPoint->x, outPoint->z, &outPoint->y);
			return true;
		}
		previousAbove = above;
		previousT = t;
	}
	return false;
}

bool GroundPick_Project(const XMFLOAT3& world, XMFLOAT2* outScreen)
{
	Camera* camera = GetCamera();
	if (!camera)
	{
		return false;
	}
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	const XMVECTOR screen = XMVector3Project(
		XMLoadFloat3(&world),
		0.0f, 0.0f, size.x, size.y, 0.0f, 1.0f,
		camera->GetProjection(), camera->GetView(), XMMatrixIdentity());
	const float depth = XMVectorGetZ(screen);
	if (depth < 0.0f || depth > 1.0f)
	{
		return false;
	}
	outScreen->x = XMVectorGetX(screen);
	outScreen->y = XMVectorGetY(screen);
	return true;
}
