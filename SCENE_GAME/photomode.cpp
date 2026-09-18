#include "photomode.h"

#include "camera.h"
#include "course.h"
#include "imgui/imgui.h"
#include "input_manager.h"
#include "mouse.h"
#include "player.h"
#include "playercamera.h"
#include "renderer.h"
#include "sunlight.h"

namespace
{
	bool g_Active = false;
	bool g_ShowPlayer = true;
	float g_Fov = 55.0f;
	float g_MoveSpeed = 0.5f;
	float g_PosterizeLevels = 0.0f;
	float g_Noise = 0.0f;
	float g_FilmGrain = 0.0f;
	float g_RgbShift = 0.0f;
	SunlightPhotoSettings g_LightSettings = {};
	SunlightPhotoSettings g_PreviousLightSettings = {};
	float g_PreviousFov = 55.0f;

	void ApplyPhotoState(void)
	{
		Sunlight_SetPhotoSettings(g_LightSettings);
		PlayerCamera_SetFreeCameraFov(g_Fov);
		PlayerCamera_SetFreeCameraActive(true);
		PlayerCamera_SetFreeCameraMoveSpeed(g_MoveSpeed);
		Player_SetVisible(g_ShowPlayer);
		Direct3D_SetPhotoParameters(
			g_PosterizeLevels,
			g_Noise,
			g_FilmGrain,
			g_RgbShift);
	}
}

void PhotoMode_Enter(void)
{
	if (g_Active)
	{
		return;
	}

	g_Active = true;
	Sunlight_GetPhotoSettings(&g_PreviousLightSettings);
	g_LightSettings = g_PreviousLightSettings;
	g_PreviousFov = GetCamera() ? GetCamera()->GetFov() : 55.0f;
	g_Fov = g_PreviousFov;
	g_MoveSpeed = PlayerCamera_GetFreeCameraMoveSpeed();
	g_ShowPlayer = true;
	g_PosterizeLevels = 0.0f;
	g_Noise = 0.0f;
	g_FilmGrain = 0.0f;
	g_RgbShift = 0.0f;

	Player_SetControlEnabled(false);
	PlayerCamera_SetFreeCameraActive(true);
	ApplyPhotoState();
	UnLockMouse();
	RequestRedraw();
}

void PhotoMode_Exit(void)
{
	if (!g_Active)
	{
		return;
	}

	g_Active = false;
	Sunlight_SetPhotoSettings(g_PreviousLightSettings);
	PlayerCamera_SetFreeCameraFov(g_PreviousFov);
	PlayerCamera_SetFreeCameraActive(false);
	Player_SetVisible(true);
	Player_SetControlEnabled(!Course_IsRaceCountdown());
	Direct3D_SetPhotoParameters(0.0f, 0.0f, 0.0f, 0.0f);
	LockMouse();
	RequestRedraw();
}

bool PhotoMode_IsActive(void)
{
	return g_Active;
}

void PhotoMode_Update(void)
{
	if (!g_Active)
	{
		return;
	}

	if (Input_IsActionTrigger(INPUT_ACTION_PAUSE) ||
		Input_IsActionTrigger(INPUT_ACTION_CANCEL))
	{
		PhotoMode_Exit();
		return;
	}

	ApplyPhotoState();
}

void PhotoMode_DrawOverlay(void)
{
	if (!g_Active || Direct3D_IsTakingScreenshot())
	{
		return;
	}

	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Photo Mode", &open))
	{
		ImGui::TextUnformatted(
			"WASD / Space / Shift: move   RMB: look");
		ImGui::TextUnformatted("Esc: exit");
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::SliderFloat("FOV", &g_Fov, 20.0f, 120.0f, "%.1f"))
			{
				PlayerCamera_SetFreeCameraFov(g_Fov);
			}
			if (ImGui::SliderFloat(
				"Move Speed",
				&g_MoveSpeed,
				0.01f,
				20.0f,
				"%.2f"))
			{
				PlayerCamera_SetFreeCameraMoveSpeed(g_MoveSpeed);
			}
			if (ImGui::Checkbox("Show Player", &g_ShowPlayer))
			{
				Player_SetVisible(g_ShowPlayer);
			}
			if (ImGui::Button("Align to Player Camera"))
			{
				PlayerCamera_SnapFreeCameraToPlayer();
			}
		}

		if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat(
				"Azimuth",
				&g_LightSettings.azimuth,
				-180.0f,
				180.0f,
				"%.1f");
			ImGui::SliderFloat(
				"Elevation",
				&g_LightSettings.elevation,
				-10.0f,
				90.0f,
				"%.1f");
			ImGui::ColorEdit3("Color", &g_LightSettings.color.x);
			ImGui::SliderFloat(
				"Intensity",
				&g_LightSettings.intensity,
				0.0f,
				20.0f,
				"%.2f");
			ImGui::SliderFloat(
				"Ambient Scale",
				&g_LightSettings.ambientScale,
				0.0f,
				4.0f,
				"%.2f");
		}

		if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat(
				"Posterize Levels",
				&g_PosterizeLevels,
				0.0f,
				16.0f,
				"%.0f");
			ImGui::SliderFloat("Noise", &g_Noise, 0.0f, 1.0f, "%.3f");
			ImGui::SliderFloat(
				"Film Grain",
				&g_FilmGrain,
				0.0f,
				1.0f,
				"%.3f");
			ImGui::SliderFloat(
				"RGB Shift",
				&g_RgbShift,
				0.0f,
				0.02f,
				"%.4f");
		}

		if (ImGui::Button("Reset Effects"))
		{
			g_PosterizeLevels = 0.0f;
			g_Noise = 0.0f;
			g_FilmGrain = 0.0f;
			g_RgbShift = 0.0f;
		}

		Sunlight_SetPhotoSettings(g_LightSettings);
		Direct3D_SetPhotoParameters(
			g_PosterizeLevels,
			g_Noise,
			g_FilmGrain,
			g_RgbShift);
	}
	ImGui::End();

	if (!open)
	{
		PhotoMode_Exit();
	}
}
