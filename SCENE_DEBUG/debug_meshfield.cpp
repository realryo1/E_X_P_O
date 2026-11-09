#pragma execution_character_set("utf-8")
/*==============================================================================
   凹凸地面＋プレイヤー検証シーン [debug_meshfield.cpp]

   ・地面       : 手続き生成の起伏メッシュ。GetHeight() で三角形平面補間して高さ・法線を返す
   ・プレイヤー : AnimSprite3D（GPUスキニング）。Idle / Run / Attack は別FBXから
                  LoadAnimationFile() で読み込む（Mixamo系の Akai.fbx + Akai_*.fbx 想定）
   ・操作       : 矢印キー=移動(カメラ基準) / Enter=ジャンプ / F=攻撃
                  カメラは DebugCamera（WASD+Space/Shift 自由飛行、右ドラッグで視点）
   ・アセット   : asset\debug\ （git管理外。権利の都合でリポジトリには含めない）
==============================================================================*/

#include <d3d11.h>
#include <DirectXMath.h>
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "debug_meshfield.h"
#include "renderer.h"
#include "shadermanager.h"
#include "camera.h"
#include "debugcamera.h"
#include "anim_sprite3d.h"
#include "texture.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"
#include "main.h"
#include "define.h"

using namespace DirectX;

namespace
{
	// ---- アセット ----
	const char* kModelPath = "asset\\debug\\model\\Akai.fbx";
	const char* kIdlePath = "asset\\debug\\model\\Akai_Idle.fbx";
	const char* kRunPath = "asset\\debug\\model\\Akai_Run.fbx";
	const char* kAttackPath = "asset\\debug\\model\\Akai_Attack.fbx";
	const wchar_t* kGroundTexturePath = L"asset\\debug\\texture\\sura.jpg";
	const char* kRootBone = "mixamorig:Hips";	// ルートモーション除去の対象ボーン

	// ---- 地面 ----
	constexpr int kGridCount = 65;				// 頂点数（1辺）。マス数は 64
	constexpr float kGridSpacing = 1.0f;		// 1マスの大きさ(m)
	constexpr float kHalfSize = (kGridCount - 1) * kGridSpacing * 0.5f;
	constexpr float kTextureTile = 4.0f;		// テクスチャ1枚が覆う長さ(m)

	// ---- プレイヤー ----
	constexpr float kFixedStep = 1.0f / 60.0f;
	constexpr float kModelScale = 0.01f;		// FBXはcm単位
	constexpr float kModelYawOffsetDeg = 180.0f;	// モデルは -Z 向きで読み込まれるため補正する
	constexpr float kFootRadius = 0.25f;		// 足元の接地確認に使う半径(m)。傾き後のめり込み防止用
	constexpr float kMoveAccel = 30.0f;			// 加速度。抵抗とのつり合いで最高速 = accel / drag
	constexpr float kMoveDrag = 6.0f;			// → 最高速 5m/s
	constexpr float kGravity = 20.0f;
	constexpr float kJumpSpeed = 7.0f;
	constexpr float kHeightSmoothRate = 18.0f;	// 表示高さの追従の速さ(1/s)。小さいほど滑らか
	constexpr float kGroundSnap = 0.25f;		// 下り坂で浮かないための吸着距離(m)
	constexpr float kTurnRate = 0.2f;			// 向き補間率（1フレームあたり）
	constexpr float kAttackTimeout = 3.0f;		// 攻撃アニメが終わらなかった時の保険

	struct FieldVertex
	{
		XMFLOAT3 position;
		XMFLOAT3 normal;
		XMFLOAT4 color;
		XMFLOAT2 texCoord;
	};

	enum PlayerState
	{
		STATE_GROUND,
		STATE_ATTACK,
	};

	struct Player
	{
		AnimSprite3D* model = nullptr;
		XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
		XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
		bool grounded = false;
		float displayY = 0.0f;			// 表示用の平滑化した高さ
		bool displayYReady = false;
		PlayerState state = STATE_GROUND;
		float stateTime = 0.0f;
		float yawDeg = 0.0f;
	};

	std::vector<float> g_heights;				// 頂点の高さ [z * kGridCount + x]
	std::vector<XMFLOAT3> g_normals;			// 頂点の法線
	ID3D11Buffer* g_vertexBuffer = nullptr;
	ID3D11Buffer* g_indexBuffer = nullptr;
	UINT g_indexCount = 0;
	ID3D11ShaderResourceView* g_texture = nullptr;

	Player g_player;
	bool g_assetsReady = false;
	std::vector<std::string> g_missingFiles;

	DrawFont* g_pHintFont = nullptr;
	DrawFont* g_pStateFont = nullptr;

	// ------------------------------------------------------------
	// 地面の高さ関数（滑らかな起伏）
	// ------------------------------------------------------------
	float FieldFunction(float x, float z)
	{
		return 1.4f * std::sin(x * 0.13f) * std::cos(z * 0.11f)
			+ 0.55f * std::sin(x * 0.37f + z * 0.29f)
			+ 0.18f * std::sin(z * 0.83f - x * 0.5f);
	}

	bool FileExists(const char* path)
	{
		const DWORD attr = GetFileAttributesA(path);
		return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	// ------------------------------------------------------------
	// 地面の生成
	// ------------------------------------------------------------
	void CreateField()
	{
		g_heights.assign(kGridCount * kGridCount, 0.0f);
		g_normals.assign(kGridCount * kGridCount, XMFLOAT3(0.0f, 1.0f, 0.0f));

		std::vector<FieldVertex> vertices(kGridCount * kGridCount);
		const float e = 0.1f;
		for (int z = 0; z < kGridCount; ++z)
		{
			for (int x = 0; x < kGridCount; ++x)
			{
				const int i = z * kGridCount + x;
				const float px = x * kGridSpacing - kHalfSize;
				const float pz = z * kGridSpacing - kHalfSize;
				const float h = FieldFunction(px, pz);

				// 解析的な勾配から法線を作る（頂点間の折れを出さない）
				const float dhdx = (FieldFunction(px + e, pz) - FieldFunction(px - e, pz)) / (2.0f * e);
				const float dhdz = (FieldFunction(px, pz + e) - FieldFunction(px, pz - e)) / (2.0f * e);
				XMFLOAT3 n;
				XMStoreFloat3(&n, XMVector3Normalize(XMVectorSet(-dhdx, 1.0f, -dhdz, 0.0f)));

				g_heights[i] = h;
				g_normals[i] = n;
				vertices[i].position = { px, h, pz };
				vertices[i].normal = n;
				vertices[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
				vertices[i].texCoord = { px / kTextureTile, pz / kTextureTile };
			}
		}

		// 1マス = 2三角形。対角線は i1-i2（GetHeight の平面補間と同じ分割）
		std::vector<uint32_t> indices;
		indices.reserve((kGridCount - 1) * (kGridCount - 1) * 6);
		for (int z = 0; z < kGridCount - 1; ++z)
		{
			for (int x = 0; x < kGridCount - 1; ++x)
			{
				const uint32_t i0 = z * kGridCount + x;
				const uint32_t i1 = i0 + 1;
				const uint32_t i2 = i0 + kGridCount;
				const uint32_t i3 = i2 + 1;
				indices.push_back(i0);
				indices.push_back(i2);
				indices.push_back(i1);
				indices.push_back(i1);
				indices.push_back(i2);
				indices.push_back(i3);
			}
		}
		g_indexCount = (UINT)indices.size();

		ID3D11Device* device = GetDevice();

		D3D11_BUFFER_DESC vertexDesc{};
		vertexDesc.ByteWidth = (UINT)(sizeof(FieldVertex) * vertices.size());
		vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
		vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA vertexData{};
		vertexData.pSysMem = vertices.data();
		device->CreateBuffer(&vertexDesc, &vertexData, &g_vertexBuffer);

		D3D11_BUFFER_DESC indexDesc{};
		indexDesc.ByteWidth = (UINT)(sizeof(uint32_t) * indices.size());
		indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA indexData{};
		indexData.pSysMem = indices.data();
		device->CreateBuffer(&indexDesc, &indexData, &g_indexBuffer);
	}

	// 位置 → マス番号と、マス内の位置(0〜1)。範囲外は端にクランプする
	void LocateCell(float x, float z, int& cx, int& cz, float& u, float& v)
	{
		float gx = (x + kHalfSize) / kGridSpacing;
		float gz = (z + kHalfSize) / kGridSpacing;
		const float maxG = (float)(kGridCount - 1);
		gx = std::fmin(std::fmax(gx, 0.0f), maxG - 0.0001f);
		gz = std::fmin(std::fmax(gz, 0.0f), maxG - 0.0001f);
		cx = (int)gx;
		cz = (int)gz;
		u = gx - cx;
		v = gz - cz;
	}

	// 描画メッシュと同じ三角形で高さを返す
	float GetHeight(float x, float z)
	{
		int cx, cz;
		float u, v;
		LocateCell(x, z, cx, cz, u, v);

		const int i0 = cz * kGridCount + cx;
		const float h0 = g_heights[i0];
		const float h1 = g_heights[i0 + 1];
		const float h2 = g_heights[i0 + kGridCount];
		const float h3 = g_heights[i0 + kGridCount + 1];

		if (u + v <= 1.0f)
		{
			return h0 + u * (h1 - h0) + v * (h2 - h0);		// 三角形 (i0, i1, i2)
		}
		return h3 + (1.0f - u) * (h1 - h3) + (1.0f - v) * (h2 - h3);	// 三角形 (i1, i2, i3)
	}

	// ------------------------------------------------------------
	// プレイヤー
	// ------------------------------------------------------------
	bool CreatePlayer()
	{
		g_missingFiles.clear();
		const char* required[] = { kModelPath, kIdlePath, kRunPath, kAttackPath };
		for (const char* path : required)
		{
			if (!FileExists(path))
			{
				g_missingFiles.push_back(path);
			}
		}
		if (!g_missingFiles.empty())
		{
			return false;
		}

		g_player = Player();
		g_player.position = { 0.0f, GetHeight(0.0f, 0.0f), 0.0f };
		g_player.model = new AnimSprite3D(
			g_player.position,
			{ kModelScale, kModelScale, kModelScale },
			{ 0.0f, 0.0f, 0.0f },
			kModelPath,
			S_PHONG);
		g_player.model->SetAnimationBlendDuration(0.2);

		// Idle/Run/Attack は別FBX。Hips の水平移動は除去し、前進はこちらの位置更新に任せる
		g_player.model->LoadAnimationFile(kIdlePath, "Idle", kRootBone);
		g_player.model->LoadAnimationFile(kRunPath, "Run", kRootBone);
		g_player.model->LoadAnimationFile(kAttackPath, "Attack", kRootBone);
		g_player.model->PlayAnimationByName("Idle", true);
		return true;
	}

	void DestroyPlayer()
	{
		if (g_player.model)
		{
			delete g_player.model;
			g_player.model = nullptr;
		}
	}

	// カメラの向き（水平成分）。矢印キーの移動基準にする
	void GetCameraBasis(XMFLOAT3& forward, XMFLOAT3& right)
	{
		forward = { 0.0f, 0.0f, 1.0f };
		if (GetCamera())
		{
			const XMMATRIX invView = XMMatrixInverse(nullptr, GetCamera()->GetView());
			XMFLOAT3 f;
			XMStoreFloat3(&f, XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), invView));
			const float len = std::sqrt(f.x * f.x + f.z * f.z);
			if (len > 0.0001f)	// 真上・真下を向いている時は直前の向きを使う
			{
				forward = { f.x / len, 0.0f, f.z / len };
			}
		}
		right = { forward.z, 0.0f, -forward.x };
	}

	void UpdatePlayer()
	{
		Player& p = g_player;
		if (!p.model)
		{
			return;
		}

		const float dt = kFixedStep;

		// ---- 入力 ----
		XMFLOAT3 camForward, camRight;
		GetCameraBasis(camForward, camRight);

		float inputX = 0.0f;
		float inputZ = 0.0f;
		if (p.state == STATE_GROUND)
		{
			if (Keyboard_IsKeyDown(KK_UP))    { inputX += camForward.x; inputZ += camForward.z; }
			if (Keyboard_IsKeyDown(KK_DOWN))  { inputX -= camForward.x; inputZ -= camForward.z; }
			if (Keyboard_IsKeyDown(KK_RIGHT)) { inputX += camRight.x;   inputZ += camRight.z; }
			if (Keyboard_IsKeyDown(KK_LEFT))  { inputX -= camRight.x;   inputZ -= camRight.z; }
		}
		const float inputLen = std::sqrt(inputX * inputX + inputZ * inputZ);
		const bool moving = inputLen > 0.0001f;

		// ---- 状態ごとの処理 ----
		if (p.state == STATE_GROUND)
		{
			if (moving)
			{
				inputX /= inputLen;
				inputZ /= inputLen;
				p.velocity.x += inputX * kMoveAccel * dt;
				p.velocity.z += inputZ * kMoveAccel * dt;

				// 進行方向へ向きを回す（最短角で補間）
				const float targetYaw = XMConvertToDegrees(std::atan2(inputX, inputZ));
				float diff = std::fmod(targetYaw - p.yawDeg + 540.0f, 360.0f) - 180.0f;
				p.yawDeg += diff * kTurnRate;

				p.model->PlayAnimationByName("Run", true);
			}
			else
			{
				p.model->PlayAnimationByName("Idle", true);
			}

			if (Keyboard_IsKeyDownTrigger(KK_ENTER) && p.grounded)
			{
				p.velocity.y = kJumpSpeed;
			}
			if (Keyboard_IsKeyDownTrigger(KK_F) && p.grounded)
			{
				p.state = STATE_ATTACK;
				p.stateTime = 0.0f;
				p.model->PlayAnimationByName("Attack", false);
			}
		}
		else // STATE_ATTACK
		{
			p.stateTime += dt;
			const bool animFinished = !p.model->IsAnimationPlaying() && !p.model->IsAnimationBlending();
			if ((p.stateTime > 0.2f && animFinished) || p.stateTime > kAttackTimeout)
			{
				p.state = STATE_GROUND;
			}
		}

		// ---- 物理 ----
		const bool wasGrounded = p.grounded;
		p.velocity.y -= kGravity * dt;
		p.velocity.x -= p.velocity.x * kMoveDrag * dt;
		p.velocity.z -= p.velocity.z * kMoveDrag * dt;
		p.position.x += p.velocity.x * dt;
		p.position.y += p.velocity.y * dt;
		p.position.z += p.velocity.z * dt;

		// 地面の外へ出ない
		const float limit = kHalfSize - 0.5f;
		p.position.x = std::fmin(std::fmax(p.position.x, -limit), limit);
		p.position.z = std::fmin(std::fmax(p.position.z, -limit), limit);

		// 地面に沿わせる。接地中の下り坂は吸着させて浮かないようにする
		const float groundY = GetHeight(p.position.x, p.position.z);
		if (p.position.y <= groundY ||
			(wasGrounded && p.velocity.y <= 0.0f && p.position.y - groundY < kGroundSnap))
		{
			p.position.y = groundY;
			if (p.velocity.y < 0.0f)
			{
				p.velocity.y = 0.0f;
			}
			p.grounded = true;
		}
		else
		{
			p.grounded = false;
		}

		// 足元周囲の最も高い地面に合わせて表示位置を持ち上げる（体は傾けず、斜面でも足がめり込まないように）
		float lift = 0.0f;
		if (p.grounded)
		{
			const float offsets[4][2] = { { kFootRadius, 0.0f }, { -kFootRadius, 0.0f }, { 0.0f, kFootRadius }, { 0.0f, -kFootRadius } };
			for (const auto& o : offsets)
			{
				lift = std::fmax(lift, GetHeight(p.position.x + o[0], p.position.z + o[1]) - p.position.y);
			}
		}
		const float modelYawDeg = p.yawDeg + kModelYawOffsetDeg;

		// 表示高さは目標高さへ指数平滑して、段差・三角形境界でのガクつきを抑える（空中は実際の高さに追従）
		const float targetY = p.position.y + lift;
		if (!p.displayYReady || !p.grounded)
		{
			p.displayY = targetY;
			p.displayYReady = true;
		}
		else
		{
			p.displayY += (targetY - p.displayY) * (1.0f - std::exp(-kHeightSmoothRate * dt));
		}

		p.model->SetPos({ p.position.x, p.displayY, p.position.z });
		p.model->SetRot({ 0.0f, modelYawDeg, 0.0f });
		p.model->UpdateAnimation(dt);
	}
}

void DebugMeshFieldScene_Initialize(void)
{
	CreateField();
	g_texture = LoadTexture(kGroundTexturePath);
	g_assetsReady = CreatePlayer();

	DebugCamera_Initialize({ 0.0f, 6.0f, -12.0f }, 0.0f, 20.0f);
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	g_pHintFont = new DrawFont(
		{ SCREEN_X / 2.0f, 30.0f },
		22.0f,
		0.0f,
		{ 0.6f, 0.6f, 0.6f, 1 },
		"Arrows:Move  Enter:Jump  F:Attack  WASD/Space/Shift:Camera  RightDrag:Look  Tab:Next"
	);
	g_pHintFont->PreCacheGlyphs();

	g_pStateFont = new DrawFont(
		{ SCREEN_X / 2.0f, SCREEN_Y - 60.0f },
		28.0f,
		0.0f,
		{ 0.8f, 0.8f, 0.2f, 1 },
		""
	);
}

void DebugMeshFieldScene_Update(void)
{
	DebugCamera_Update();
	if (GetCamera())
	{
		SetCameraPosition(GetCamera()->GetPos());
	}

	UpdatePlayer();
	RequestRedraw();	// アニメーションがあるので毎フレーム描き直す

	if (g_pStateFont)
	{
		if (!g_assetsReady)
		{
			std::string text = "Missing asset: ";
			text += g_missingFiles.empty() ? "?" : g_missingFiles.front();
			g_pStateFont->SetText(text);
		}
		else
		{
			g_pStateFont->SetText(g_player.state == STATE_ATTACK ? "ATTACK" : (g_player.grounded ? "GROUND" : "AIR"));
		}
		g_pStateFont->PreCacheGlyphs();
	}
}

void DebugMeshFieldScene_Draw(void)
{
	if (!g_vertexBuffer || !g_indexBuffer || !GetCamera())
	{
		return;
	}

	// 深度バッファ付きのシーン用レンダーターゲットへ切り替える。
	// 呼ばないと深度なしで描かれ、メッシュの描画順どおりに前後関係が決まってしまう。
	Direct3D_BeginScene();
	SetDepthEnable(true);
	SetCullState(CULLSTATE_NONE);
	SetCameraPosition(GetCamera()->GetPos());

	LIGHT light = {};
	light.Enable = TRUE;
	light.Position = XMFLOAT4(0.0f, 30.0f, 0.0f, 1.0f);
	light.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	light.Ambient = XMFLOAT4(0.85f, 0.90f, 0.95f, 1.0f);
	light.PointLightParam = XMFLOAT4(150.0f, 0.8f, 0.0f, 0.0f);
	SetLight(light);

	// ---- 地面 ----
	MATERIAL material = {};
	material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	material.Specular = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	material.Emission = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	material.Shininess = 16.0f;
	SetMaterial(material);

	ID3D11DeviceContext* context = GetDeviceContext();
	ShaderManager* shader = GetShader(S_LAMBERT);
	context->IASetInputLayout(shader->GetVertexLayout());
	context->VSSetShader(shader->GetVertexShader(), nullptr, 0);
	context->PSSetShader(shader->GetPixelShader(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &g_texture);
	SetDefaultSampler();

	SetWorldMatrix(XMMatrixIdentity());
	SetViewMatrix(GetCamera()->GetView());
	SetProjectionMatrix(GetCamera()->GetProjection());

	UINT stride = sizeof(FieldVertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
	context->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->DrawIndexed(g_indexCount, 0, 0);

	// ---- プレイヤー ----
	if (g_player.model)
	{
		SetParameter(XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f));
		g_player.model->Draw();
	}

	SetCullState(CULLSTATE_BACK);

	// シーン用ターゲットに描いた3D結果をバックバッファへ合成する（以降の2Dはバックバッファへ直接描く）
	Direct3D_ApplySsao();

	SetDepthEnable(false);
	if (g_pHintFont) g_pHintFont->Draw();
	if (g_pStateFont) g_pStateFont->Draw();
	DebugCamera_Draw();
}

void DebugMeshFieldScene_Finalize(void)
{
	DestroyPlayer();

	if (g_vertexBuffer) { g_vertexBuffer->Release(); g_vertexBuffer = nullptr; }
	if (g_indexBuffer) { g_indexBuffer->Release(); g_indexBuffer = nullptr; }
	g_texture = nullptr;
	g_heights.clear();
	g_normals.clear();
	g_missingFiles.clear();
	g_assetsReady = false;

	DebugCamera_Finalize();
	Camera_Finalize();

	if (g_pHintFont) { delete g_pHintFont; g_pHintFont = nullptr; }
	if (g_pStateFont) { delete g_pStateFont; g_pStateFont = nullptr; }
}
