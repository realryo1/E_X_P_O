#include "taxi.h"
#include "field.h"
#include "collision.h"
#include "gameaudio.h"
#include "player.h"
#include "playercamera.h"
#include "billboard.h"
#include "BillboardDrawFont.h"
#include "font.h"
#include "MultiLineDrawFont.h"
#include "sprite2d.h"
#include "define.h"
#include "main.h"
#include "renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
	const char* TAXI_MARKER_TEXTURE = "asset\\texture\\makulogo.png";
	const char* TAXI_BEST_PATH = "asset\\taxi_best.txt";

	const float ROUND_SECONDS = 90.0f;
	// ワールド単位は 1 = 5m（メートル→ワールド単位は 0.2。plateau.md 7.2）。
	// 高度などの基準は実メートルで決め、ワールド単位へ直して使う。画面表示は実メートルに戻す
	const float UNIT_METERS = 5.0f;
	// 乗降ポイントは地上。パビリオンの外へ出た地面の点にリングを置き、見えているリングの中へ入ると乗り降りできる。
	// 判定はリングの描画サイズ（最小サイズの半分）と同じ半径の球。遠くでは見やすいよう描画だけ大きくなる
	const float MARKER_MIN_SIZE = 6.0f;
	const float WAYPOINT_MIN_SIZE = 8.0f;
	const float MARKER_HEIGHT = 2.0f;	// リングの中心の地上からの高さ。地面すれすれの機体も中に入る
	const float ENTRANCE_MARGIN = 4.0f;	// パビリオンの境界からこれ以上離れた点を乗降ポイントにする
	const float ENTRANCE_STEP = 1.5f;
	const float ENTRANCE_MAX = 120.0f;
	const float ARROW_SIZE = 72.0f;
	const float PICKUP_SPEED_RATIO = 2.0f;	// 迎えに行く間の移動速度の倍率

	// 大屋根リングの潜り抜けボーナス。屋根の下に入り、一定以上離れた位置で出たら成立（迎え中も送り中も有効）
	const int RING_PASS_BONUS = 200;
	const float RING_PASS_MIN_DISTANCE = 4.0f;	// 入った点と出た点の水平距離（20m）。出入りの揺れや引き返しを除く
	const float RING_ROOF_CLEARANCE = 0.1f;
	const float TRIP_SPEED_ESTIMATE = 6.5f;	// m/s。PLAYER_MOVE_SPEED_DEFAULT 0.12 x 60fps が約7.2
	const float TRIP_MARGIN_SECONDS = 12.0f;
	const float PICKUP_MIN = 60.0f;
	const float PICKUP_MAX = 260.0f;
	const float DROP_MIN = 150.0f;
	const float DROP_MAX = 450.0f;
	const int BASE_FARE = 100;
	const int FAIL_PENALTY = 150;
	const int CHIP_PER_COMBO = 100;
	const int PILLAR_COUNT = 6;
	const int MAX_SPOTS = 256;

	// 高度（地面からの高さ）。低空を飛ぶほど得、高く飛ぶと客が怖がって時間が早く減る
	const float LOW_ALTITUDE = 12.0f / UNIT_METERS;	// 大屋根リングの高さ（約12m）より低い
	const float HIGH_ALTITUDE = 60.0f / UNIT_METERS;
	const int LOW_BONUS_PER_SECOND = 30;
	const float HIGH_EXTRA_DRAIN = 0.5f;	// 高すぎる間は制限時間が 1.5 倍の速さで減る

	// お客さんの注文
	const float ORDER_LOW_ALTITUDE = 20.0f / UNIT_METERS;
	const float ORDER_LOW_ALLOWED_HIGH_SECONDS = 2.0f;
	const float ORDER_DETOUR_MIN_FROM_ENDS = 40.0f;
	const float ORDER_DETOUR_MAX_RATIO = 1.5f;	// 寄り道込みの道のりは直線の 1.5 倍まで
	const float ORDER_RUSH_TIME_RATIO = 0.7f;
	const int ORDER_REWARD_LOW = 300;
	const int ORDER_REWARD_NONSTOP = 300;
	const int ORDER_REWARD_DETOUR = 400;
	const int ORDER_REWARD_RUSH = 300;

	enum class Phase
	{
		Off,
		Pickup,
		Deliver,
		Result,
	};

	enum class Order
	{
		None,
		LowFlight,
		NonStop,
		Detour,
		Rush,
	};

	Phase g_Phase = Phase::Off;
	std::vector<FieldMissionSpot> g_Spots;
	int g_PickupSpot = -1;
	int g_DropSpot = -1;
	XMFLOAT3 g_PickupPoint = {};	// 乗降ポイント（地面の高さ）
	XMFLOAT3 g_DropPoint = {};
	XMFLOAT3 g_WaypointPoint = {};
	float g_RoundLeft = 0.0f;
	float g_TripLeft = 0.0f;
	float g_TripTotal = 0.0f;
	float g_TripDistance = 0.0f;
	int g_Score = 0;
	int g_Combo = 0;
	int g_Delivered = 0;
	int g_Failed = 0;
	int g_OrdersDone = 0;
	int g_Best = 0;
	bool g_NewBest = false;
	float g_ToastTimer = 0.0f;
	float g_Anim = 0.0f;
	bool g_HasLastTick = false;
	std::chrono::steady_clock::time_point g_LastTick;
	std::mt19937 g_Random{ std::random_device{}() };

	// 1回の送迎ごとの高度の集計
	float g_LowSeconds = 0.0f;
	float g_HighSeconds = 0.0f;
	float g_OrderHighSeconds = 0.0f;
	int g_AltitudeState = 0;	// 0=ふつう 1=低空 2=高すぎ

	// 注文
	Order g_Order = Order::None;
	int g_OrderWaypoint = -1;
	bool g_OrderWaypointReached = false;
	int g_OrderHitBase = 0;

	// 迎え中の移動速度ブースト
	bool g_SpeedBoosted = false;
	float g_BaseMoveSpeed = 0.0f;

	// 大屋根リングの潜り抜け
	bool g_UnderRoof = false;
	XMFLOAT3 g_RoofEnterPos = {};
	int g_RingPasses = 0;

	Billboard* g_Marker = nullptr;
	std::vector<Billboard*> g_Pillar;
	BillboardDrawFont* g_NameLabel = nullptr;
	float g_LabelHeight = 0.0f;
	bool g_MarkerIsDeliver = false;
	Billboard* g_WayMarker = nullptr;
	BillboardDrawFont* g_WayLabel = nullptr;
	float g_WayLabelHeight = 0.0f;

	DrawFont* g_pInstruct = nullptr;
	DrawFont* g_pOrder = nullptr;
	Sprite2D* g_pArrow = nullptr;	// asset/texture/arrow.png（上向き）を目的地の方へ回す
	DrawFont* g_pDistance = nullptr;
	DrawFont* g_pLowText = nullptr;
	DrawFont* g_pHighText = nullptr;
	DrawFont* g_pRoundTime = nullptr;
	DrawFont* g_pTripTime = nullptr;
	DrawFont* g_pScore = nullptr;
	DrawFont* g_pCombo = nullptr;
	MultiLineDrawFont* g_pToast = nullptr;
	Sprite2D* g_pResultBack = nullptr;
	DrawFont* g_pResultTitle = nullptr;
	DrawFont* g_pResultLine1 = nullptr;
	DrawFont* g_pResultLine2 = nullptr;
	DrawFont* g_pResultNew = nullptr;
	DrawFont* g_pResultHint = nullptr;

	const XMFLOAT4 PICKUP_COLOR = { 1.0f, 0.85f, 0.2f, 1.0f };
	const XMFLOAT4 DELIVER_COLOR = { 0.3f, 0.9f, 1.0f, 1.0f };
	const XMFLOAT4 WAYPOINT_COLOR = { 0.4f, 1.0f, 0.5f, 1.0f };
	const XMFLOAT4 ORDER_COLOR = { 1.0f, 0.6f, 0.2f, 1.0f };

	void SetTextIf(DrawFont* font, const std::string& text)
	{
		if (font && font->GetText() != text)
		{
			font->SetText(text);
		}
	}

	DrawFont* MakeFont(float x, float y, float size, XMFLOAT4 color, TextAlignment align)
	{
		// 共有アトラスはグリフを最初に作ったサイズで使い回すため、サイズの違うフォントが混ざると崩れる
		return new DrawFont({ x, y }, size, 0.0f, color, "", align, false);
	}

	float DistanceXZ(const XMFLOAT3& a, const XMFLOAT3& b)
	{
		const float dx = a.x - b.x;
		const float dz = a.z - b.z;
		return sqrtf(dx * dx + dz * dz);
	}

	// from から minD〜maxD にある候補をランダムに選ぶ。無ければ範囲の中央に最も近いものを選ぶ
	int PickSpot(const XMFLOAT3& from, float minD, float maxD, int exclude)
	{
		std::vector<int> inRange;
		int fallback = -1;
		float fallbackScore = 1.0e30f;
		const float mid = (minD + maxD) * 0.5f;
		for (int i = 0; i < static_cast<int>(g_Spots.size()); ++i)
		{
			if (i == exclude)
			{
				continue;
			}
			const float d = DistanceXZ(from, g_Spots[i].pos);
			if (d >= minD && d <= maxD)
			{
				inRange.push_back(i);
			}
			const float score = fabsf(d - mid);
			if (score < fallbackScore)
			{
				fallbackScore = score;
				fallback = i;
			}
		}
		if (!inRange.empty())
		{
			return inRange[g_Random() % inRange.size()];
		}
		return fallback;
	}

	// パビリオンの中心から toward の方向へ進み、どのパビリオンの外にも出た最初の地面の点を返す。
	// 中心は建物の中なので、そのままでは地上から入れない
	XMFLOAT3 ComputeEntrance(int spotIndex, const XMFLOAT3& toward)
	{
		const XMFLOAT3 center = g_Spots[spotIndex].pos;
		float dx = toward.x - center.x;
		float dz = toward.z - center.z;
		const float length = sqrtf(dx * dx + dz * dz);
		if (length < 0.001f)
		{
			dx = 0.0f;
			dz = 1.0f;
		}
		else
		{
			dx /= length;
			dz /= length;
		}

		XMFLOAT3 point = { center.x, Player_GetPos().y, center.z };
		for (float d = 0.0f; d <= ENTRANCE_MAX; d += ENTRANCE_STEP)
		{
			const float x = center.x + dx * d;
			const float z = center.z + dz * d;
			float groundY = 0.0f;
			if (!Field_SampleGroundY(x, z, &groundY))
			{
				continue;
			}
			point = { x, groundY, z };
			char label[8] = {};
			float distance = 0.0f;
			const XMFLOAT3 probe = { x, groundY + MARKER_HEIGHT, z };
			if (!Field_GetNearestPavilionLabel(probe, label, sizeof(label), &distance) ||
				distance >= ENTRANCE_MARGIN)
			{
				break;
			}
		}
		return point;
	}

	float Distance3D(const XMFLOAT3& a, const XMFLOAT3& b)
	{
		const float dy = a.y - b.y;
		const float xz = DistanceXZ(a, b);
		return sqrtf(xz * xz + dy * dy);
	}

	// リングの中心 center から radius 以内（リングの中に入っている）か
	bool IsInsideRing(const XMFLOAT3& center, float radius)
	{
		return Distance3D(Player_GetPos(), center) <= radius;
	}

	XMFLOAT3 TargetPos(void)
	{
		XMFLOAT3 pos = g_Phase == Phase::Deliver ? g_DropPoint : g_PickupPoint;
		pos.y += MARKER_HEIGHT;
		return pos;
	}

	XMFLOAT3 WaypointPos(void)
	{
		XMFLOAT3 pos = g_WaypointPoint;
		pos.y += MARKER_HEIGHT;
		return pos;
	}

	bool IsWaypointActive(void)
	{
		return g_Phase == Phase::Deliver &&
			g_Order == Order::Detour &&
			g_OrderWaypoint >= 0 &&
			!g_OrderWaypointReached;
	}

	// 地面からの高さ。地面が取れない場所では false
	bool GetAltitude(float* outAltitude)
	{
		const XMFLOAT3 pos = Player_GetPos();
		float groundY = 0.0f;
		if (!Field_SampleGroundY(pos.x, pos.z, &groundY))
		{
			return false;
		}
		*outAltitude = pos.y - groundY;
		return true;
	}

	void ApplyMarkerStyle(void)
	{
		const bool deliver = g_Phase == Phase::Deliver;
		const XMFLOAT4 color = deliver ? DELIVER_COLOR : PICKUP_COLOR;
		if (g_Marker && g_MarkerIsDeliver != deliver)
		{
			g_Marker->SetColor(color);
			for (Billboard* piece : g_Pillar)
			{
				piece->SetColor({ color.x, color.y, color.z, 0.55f });
			}
		}
		g_MarkerIsDeliver = deliver;
		if (g_NameLabel)
		{
			g_NameLabel->SetColor(color);
			const int index = deliver ? g_DropSpot : g_PickupSpot;
			SetTextIf(g_NameLabel, g_Spots[index].name);
		}
	}

	void LoadBest(void)
	{
		g_Best = 0;
		FILE* file = nullptr;
		if (fopen_s(&file, TAXI_BEST_PATH, "rb") == 0 && file)
		{
			int value = 0;
			if (fscanf_s(file, "%d", &value) == 1 && value > 0)
			{
				g_Best = value;
			}
			fclose(file);
		}
	}

	void SaveBest(void)
	{
		FILE* file = nullptr;
		if (fopen_s(&file, TAXI_BEST_PATH, "wb") == 0 && file)
		{
			fprintf(file, "%d\n", g_Best);
			fclose(file);
		}
	}

	void ShowToast(const std::string& text, XMFLOAT4 color)
	{
		if (!g_pToast)
		{
			return;
		}
		SetTextIf(g_pToast, text);
		g_pToast->SetColor(color);
		g_ToastTimer = 2.2f;
	}

	const char* OrderShortName(Order order)
	{
		switch (order)
		{
		case Order::LowFlight: return "低空キープ";
		case Order::NonStop: return "ノンストップ";
		case Order::Detour: return "寄り道";
		case Order::Rush: return "超特急";
		default: return "";
		}
	}

	int OrderReward(Order order)
	{
		switch (order)
		{
		case Order::LowFlight: return ORDER_REWARD_LOW;
		case Order::NonStop: return ORDER_REWARD_NONSTOP;
		case Order::Detour: return ORDER_REWARD_DETOUR;
		case Order::Rush: return ORDER_REWARD_RUSH;
		default: return 0;
		}
	}

	// 寄り道先。出発地・目的地から離れていて、道のりが長くなりすぎないパビリオンをランダムに選ぶ
	int PickDetourSpot(int pickup, int drop)
	{
		const XMFLOAT3& from = g_Spots[pickup].pos;
		const XMFLOAT3& to = g_Spots[drop].pos;
		const float direct = DistanceXZ(from, to);
		std::vector<int> candidates;
		for (int i = 0; i < static_cast<int>(g_Spots.size()); ++i)
		{
			if (i == pickup || i == drop)
			{
				continue;
			}
			const float a = DistanceXZ(from, g_Spots[i].pos);
			const float b = DistanceXZ(g_Spots[i].pos, to);
			if (a >= ORDER_DETOUR_MIN_FROM_ENDS &&
				b >= ORDER_DETOUR_MIN_FROM_ENDS &&
				a + b <= direct * ORDER_DETOUR_MAX_RATIO)
			{
				candidates.push_back(i);
			}
		}
		if (candidates.empty())
		{
			return -1;
		}
		return candidates[g_Random() % candidates.size()];
	}

	void ChooseOrder(void)
	{
		g_Order = Order::None;
		g_OrderWaypoint = -1;
		g_OrderWaypointReached = false;
		g_OrderHitBase = Player_GetHitCount();

		// 注文は毎回必ず付く。寄り道先が見つからなければ超特急にする
		const int roll = static_cast<int>(g_Random() % 100);
		if (roll < 25)
		{
			g_Order = Order::LowFlight;
		}
		else if (roll < 50)
		{
			g_Order = Order::NonStop;
		}
		else if (roll < 80)
		{
			g_OrderWaypoint = PickDetourSpot(g_PickupSpot, g_DropSpot);
			if (g_OrderWaypoint >= 0)
			{
				g_Order = Order::Detour;
				g_WaypointPoint = ComputeEntrance(g_OrderWaypoint, g_PickupPoint);
			}
			else
			{
				g_Order = Order::Rush;
			}
		}
		else
		{
			g_Order = Order::Rush;
		}
	}

	std::string BuildOrderText(void)
	{
		char text[128] = {};
		switch (g_Order)
		{
		case Order::LowFlight:
			if (g_OrderHighSeconds > ORDER_LOW_ALLOWED_HIGH_SECONDS)
			{
				return "注文：低空キープ  失敗…";
			}
			snprintf(
				text, sizeof(text), "注文：地面から%dm以下をキープ！  (高すぎ %.1f / %.1f 秒)",
				static_cast<int>(ORDER_LOW_ALTITUDE * UNIT_METERS + 0.5f), g_OrderHighSeconds, ORDER_LOW_ALLOWED_HIGH_SECONDS);
			return text;
		case Order::NonStop:
			if (Player_GetHitCount() != g_OrderHitBase)
			{
				return "注文：ノンストップ  失敗…（ぶつかった）";
			}
			return "注文：ノンストップ！ ぶつかるな！";
		case Order::Detour:
			if (g_OrderWaypointReached)
			{
				return "注文：寄り道  完了！";
			}
			return std::string("注文：『") + g_Spots[g_OrderWaypoint].name + "』に寄り道して！（緑のマーカー）";
		case Order::Rush:
			return "注文：超特急！ 制限時間が短い！";
		default:
			return "";
		}
	}

	bool IsOrderSuccess(void)
	{
		switch (g_Order)
		{
		case Order::LowFlight: return g_OrderHighSeconds <= ORDER_LOW_ALLOWED_HIGH_SECONDS;
		case Order::NonStop: return Player_GetHitCount() == g_OrderHitBase;
		case Order::Detour: return g_OrderWaypointReached;
		case Order::Rush: return true;
		default: return false;
		}
	}

	// 迎えに行く移動は退屈なので、その間だけ移動速度を2倍にする。送り中・終了時・中止時は元の速度に戻す。
	// 元の速度は、倍にする直前の値を覚えておく（デバッグ窓で速度を変えていても壊さない）
	void SetPickupSpeedBoost(bool on)
	{
		if (on && !g_SpeedBoosted)
		{
			g_BaseMoveSpeed = Player_GetMoveSpeed();
			Player_SetMoveSpeed(g_BaseMoveSpeed * PICKUP_SPEED_RATIO);
			g_SpeedBoosted = true;
		}
		else if (!on && g_SpeedBoosted)
		{
			Player_SetMoveSpeed(g_BaseMoveSpeed);
			g_SpeedBoosted = false;
		}
	}

	void BeginPickup(void)
	{
		SetPickupSpeedBoost(true);
		const XMFLOAT3 from = Player_GetPos();
		g_PickupSpot = PickSpot(from, PICKUP_MIN, PICKUP_MAX, -1);
		g_PickupPoint = ComputeEntrance(g_PickupSpot, from);
		g_DropSpot = -1;
		g_Order = Order::None;
		g_OrderWaypoint = -1;
		g_AltitudeState = 0;
		g_Phase = Phase::Pickup;
		ApplyMarkerStyle();
	}

	void BeginDeliver(void)
	{
		SetPickupSpeedBoost(false);
		g_DropSpot = PickSpot(g_Spots[g_PickupSpot].pos, DROP_MIN, DROP_MAX, g_PickupSpot);
		g_TripDistance = DistanceXZ(g_Spots[g_PickupSpot].pos, g_Spots[g_DropSpot].pos);
		g_DropPoint = ComputeEntrance(g_DropSpot, g_PickupPoint);
		g_LowSeconds = 0.0f;
		g_HighSeconds = 0.0f;
		g_OrderHighSeconds = 0.0f;
		g_AltitudeState = 0;
		ChooseOrder();

		float pathLength = g_TripDistance;
		if (g_Order == Order::Detour)
		{
			pathLength =
				DistanceXZ(g_Spots[g_PickupSpot].pos, g_Spots[g_OrderWaypoint].pos) +
				DistanceXZ(g_Spots[g_OrderWaypoint].pos, g_Spots[g_DropSpot].pos);
		}
		g_TripTotal = pathLength / TRIP_SPEED_ESTIMATE + TRIP_MARGIN_SECONDS;
		if (g_Order == Order::Rush)
		{
			g_TripTotal *= ORDER_RUSH_TIME_RATIO;
		}
		g_TripLeft = g_TripTotal;
		g_Phase = Phase::Deliver;
		ApplyMarkerStyle();
		if (g_WayLabel && g_Order == Order::Detour)
		{
			SetTextIf(g_WayLabel, g_Spots[g_OrderWaypoint].name);
		}
	}

	void FinishRound(void)
	{
		SetPickupSpeedBoost(false);
		g_Phase = Phase::Result;
		g_NewBest = g_Score > g_Best;
		if (g_NewBest)
		{
			g_Best = g_Score;
			SaveBest();
		}
		GameAudio_PlayGoal();
		GameAudio_SetBgmGoal();
		SetTextIf(g_pResultTitle, "ミッション終了");
		SetTextIf(g_pResultLine1, "スコア  " + std::to_string(g_Score));
		SetTextIf(
			g_pResultLine2,
			"送迎 " + std::to_string(g_Delivered) + " 件   注文達成 " +
			std::to_string(g_OrdersDone) + " 件   リング潜り " +
			std::to_string(g_RingPasses) + " 回   時間切れ " +
			std::to_string(g_Failed) + " 件   ベスト " + std::to_string(g_Best));
	}

	void OnArrivePickup(void)
	{
		Player_ActivateDash();
		GameAudio_PlayBoost();
		BeginDeliver();
		std::string text = "乗車！ 急いで！";
		if (g_Order != Order::None)
		{
			text += std::string("\nお客さんの注文：") + OrderShortName(g_Order);
		}
		ShowToast(text, PICKUP_COLOR);
	}

	void OnArriveDeliver(void)
	{
		++g_Combo;
		++g_Delivered;
		const int distanceFare = static_cast<int>(g_TripDistance * 0.5f);
		const int timeBonus = static_cast<int>((std::max)(g_TripLeft, 0.0f) * 20.0f);
		const int chip = (g_Combo - 1) * CHIP_PER_COMBO;
		const int lowBonus = static_cast<int>(g_LowSeconds * LOW_BONUS_PER_SECOND);
		int orderBonus = 0;
		const bool orderDone = g_Order != Order::None && IsOrderSuccess();
		if (orderDone)
		{
			orderBonus = OrderReward(g_Order);
			++g_OrdersDone;
		}
		const int total = BASE_FARE + distanceFare + timeBonus + chip + lowBonus + orderBonus;
		g_Score += total;
		g_RoundLeft += (std::min)((std::max)(g_TripLeft, 0.0f) * 0.5f, 8.0f);
		Player_ActivateDash();
		GameAudio_PlayGoal();

		char line1[64] = {};
		snprintf(line1, sizeof(line1), "到着！ +%d", total);
		std::string text = line1;
		std::string detail;
		auto addDetail = [&detail](const char* label, int value)
		{
			char part[48] = {};
			snprintf(part, sizeof(part), "%s+%d", label, value);
			if (!detail.empty())
			{
				detail += "  ";
			}
			detail += part;
		};
		if (chip > 0) addDetail("チップ", chip);
		if (lowBonus > 0) addDetail("低空", lowBonus);
		if (orderBonus > 0) addDetail("注文達成", orderBonus);
		if (g_Order != Order::None && !orderDone)
		{
			detail += detail.empty() ? "注文失敗…" : "  注文失敗…";
		}
		if (!detail.empty())
		{
			text += "\n" + detail;
		}
		ShowToast(text, DELIVER_COLOR);
		BeginPickup();
	}

	void OnTripTimeout(void)
	{
		g_Score = (std::max)(g_Score - FAIL_PENALTY, 0);
		g_Combo = 0;
		++g_Failed;
		GameAudio_PlayInvalid();
		ShowToast("時間切れ…  お客さん帰っちゃった", { 1.0f, 0.4f, 0.3f, 1.0f });
		BeginPickup();
	}

	std::string FormatSeconds(float seconds)
	{
		const int total = static_cast<int>((std::max)(seconds, 0.0f));
		char text[16] = {};
		snprintf(text, sizeof(text), "%02d:%02d", total / 60, total % 60);
		return text;
	}

	// 送迎中の高度の集計。低空ならボーナス時間、高すぎれば制限時間を余計に減らす
	void UpdateAltitude(float dt)
	{
		g_AltitudeState = 0;
		if (g_Phase != Phase::Deliver)
		{
			return;
		}
		float altitude = 0.0f;
		if (!GetAltitude(&altitude))
		{
			return;
		}
		if (altitude <= LOW_ALTITUDE)
		{
			g_AltitudeState = 1;
			g_LowSeconds += dt;
		}
		else if (altitude > HIGH_ALTITUDE)
		{
			g_AltitudeState = 2;
			g_HighSeconds += dt;
			g_TripLeft -= dt * HIGH_EXTRA_DRAIN;
		}
		if (altitude > ORDER_LOW_ALTITUDE)
		{
			g_OrderHighSeconds += dt;
		}
	}

	// 機体がリングの屋根の下にいるか（その位置のリングの最高面が、機体より上にある）
	bool IsUnderRingRoof(void)
	{
		const int ringId = Field_GetRingCollisionId();
		if (ringId < 0)
		{
			return false;
		}
		const XMFLOAT3 pos = Player_GetPos();
		float roofY = 0.0f;
		if (!Collision_SampleTopY(ringId, pos.x, pos.z, &roofY))
		{
			return false;
		}
		return pos.y < roofY - RING_ROOF_CLEARANCE;
	}

	void UpdateRingPass(void)
	{
		const bool under = IsUnderRingRoof();
		if (under && !g_UnderRoof)
		{
			g_RoofEnterPos = Player_GetPos();
		}
		else if (!under && g_UnderRoof &&
			DistanceXZ(g_RoofEnterPos, Player_GetPos()) >= RING_PASS_MIN_DISTANCE)
		{
			++g_RingPasses;
			g_Score += RING_PASS_BONUS;
			Player_ActivateDash();
			GameAudio_PlayBoost();
			char text[64] = {};
			snprintf(text, sizeof(text), "大屋根リング潜り抜け！ +%d", RING_PASS_BONUS);
			ShowToast(text, { 1.0f, 0.85f, 0.4f, 1.0f });
		}
		g_UnderRoof = under;
	}

	void UpdateWaypoint(void)
	{
		if (!IsWaypointActive())
		{
			return;
		}
		if (IsInsideRing(WaypointPos(), WAYPOINT_MIN_SIZE * 0.5f))
		{
			g_OrderWaypointReached = true;
			GameAudio_PlayBoost();
			ShowToast("寄り道OK！", WAYPOINT_COLOR);
		}
	}

	void UpdateMarkers(void)
	{
		if (!g_Marker || g_PickupSpot < 0)
		{
			return;
		}
		const XMFLOAT3 target = TargetPos();
		const float dist = DistanceXZ(Player_GetPos(), target);
		const float bob = sinf(g_Anim * 3.0f) * 0.8f;
		const float size = (std::min)((std::max)(dist * 0.04f, MARKER_MIN_SIZE), 26.0f);
		g_Marker->SetPos({ target.x, target.y + bob, target.z });
		g_Marker->SetDrawSize({ size, size });
		for (int i = 0; i < PILLAR_COUNT; ++i)
		{
			const float pieceSize = size * 0.45f;
			g_Pillar[i]->SetPos({ target.x, target.y + size * (0.7f + 0.55f * i) + bob, target.z });
			g_Pillar[i]->SetDrawSize({ pieceSize, pieceSize });
		}
		if (g_NameLabel)
		{
			// 既存のパビリオン名ラベルが 0.2。遠くでも読める程度に距離で大きくするが、近くでは小さく保つ
			const float labelHeight = (std::min)((std::max)(dist * 0.03f, 0.25f), 8.0f);
			if (fabsf(labelHeight - g_LabelHeight) > g_LabelHeight * 0.08f)
			{
				g_LabelHeight = labelHeight;
				g_NameLabel->SetWorldHeight(labelHeight);
			}
			g_NameLabel->SetWorldPos({ target.x, target.y + size * 0.9f + labelHeight, target.z });
		}

		if (IsWaypointActive() && g_WayMarker)
		{
			const XMFLOAT3 waypoint = WaypointPos();
			const float wayDist = DistanceXZ(Player_GetPos(), waypoint);
			const float waySize = (std::min)((std::max)(wayDist * 0.04f, WAYPOINT_MIN_SIZE), 22.0f);
			g_WayMarker->SetPos({ waypoint.x, waypoint.y + bob, waypoint.z });
			g_WayMarker->SetDrawSize({ waySize, waySize });
			if (g_WayLabel)
			{
				const float labelHeight = (std::min)((std::max)(wayDist * 0.03f, 0.25f), 8.0f);
				if (fabsf(labelHeight - g_WayLabelHeight) > g_WayLabelHeight * 0.08f)
				{
					g_WayLabelHeight = labelHeight;
					g_WayLabel->SetWorldHeight(labelHeight);
				}
				g_WayLabel->SetWorldPos({ waypoint.x, waypoint.y + waySize * 0.8f + labelHeight, waypoint.z });
			}
		}
	}
}

void Taxi_Initialize(void)
{
	g_Phase = Phase::Off;
	LoadBest();

	const float cx = SCREEN_X * 0.5f;
	// 縦の配置: 指示 → (ui.cpp のパビリオン名の帯が y=86〜138 に出る) → 注文 → 矢印 → 距離 → 高度の表示
	g_pInstruct = MakeFont(cx, 44.0f, 30.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, TA_MIDDLE);
	g_pOrder = MakeFont(cx, 162.0f, 24.0f, ORDER_COLOR, TA_MIDDLE);
	g_pArrow = new Sprite2D(
		{ cx, 240.0f },
		{ ARROW_SIZE, ARROW_SIZE },
		0.0f,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\arrow.png");
	// 距離・高度・ボーナスは、マーカーと重ならないよう画面の下に置く
	g_pDistance = MakeFont(cx, SCREEN_Y - 56.0f, 26.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, TA_MIDDLE);
	g_pLowText = MakeFont(cx, SCREEN_Y - 98.0f, 26.0f, { 0.4f, 1.0f, 0.5f, 1.0f }, TA_MIDDLE);
	g_pLowText->SetText("低空飛行ボーナス！");
	g_pHighText = MakeFont(cx, SCREEN_Y - 98.0f, 26.0f, { 1.0f, 0.35f, 0.3f, 1.0f }, TA_MIDDLE);
	g_pHighText->SetText("高すぎ！ お客さんが怖がってる！");
	g_pRoundTime = MakeFont(SCREEN_X - 40.0f, 175.0f, 28.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, TA_END);
	g_pTripTime = MakeFont(SCREEN_X - 40.0f, 213.0f, 24.0f, { 1.0f, 0.9f, 0.3f, 1.0f }, TA_END);
	g_pScore = MakeFont(40.0f, SCREEN_Y - 110.0f, 30.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, TA_START);
	g_pCombo = MakeFont(40.0f, SCREEN_Y - 70.0f, 24.0f, { 1.0f, 0.75f, 0.2f, 1.0f }, TA_START);
	// トーストは複数行（到着の内訳など）を出すので、改行を扱える MultiLineDrawFont にする
	g_pToast = new MultiLineDrawFont(
		{ cx, SCREEN_Y * 0.5f }, 36.0f, 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, "", 1.4f, TA_MIDDLE);

	g_pResultBack = new Sprite2D(
		{ cx, 370.0f },
		{ 960.0f, 360.0f },
		0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.6f },
		BLENDSTATE_ALFA,
		L"asset\\texture\\fade.png");
	g_pResultTitle = MakeFont(cx, 260.0f, 56.0f, { 1.0f, 0.9f, 0.3f, 1.0f }, TA_MIDDLE);
	g_pResultLine1 = MakeFont(cx, 345.0f, 44.0f, { 1.0f, 1.0f, 1.0f, 1.0f }, TA_MIDDLE);
	g_pResultLine2 = MakeFont(cx, 410.0f, 21.0f, { 0.85f, 0.85f, 0.85f, 1.0f }, TA_MIDDLE);
	g_pResultNew = MakeFont(cx, 465.0f, 34.0f, { 1.0f, 0.35f, 0.2f, 1.0f }, TA_MIDDLE);
	g_pResultNew->SetText("ベスト更新！");
	g_pResultHint = MakeFont(cx, 520.0f, 24.0f, { 0.8f, 0.8f, 0.8f, 1.0f }, TA_MIDDLE);
	g_pResultHint->SetText("決定でフリー飛行へ戻る");
}

void Taxi_Finalize(void)
{
	Taxi_Stop();
	SAFE_DELETE(g_pInstruct);
	SAFE_DELETE(g_pOrder);
	SAFE_DELETE(g_pArrow);
	SAFE_DELETE(g_pDistance);
	SAFE_DELETE(g_pLowText);
	SAFE_DELETE(g_pHighText);
	SAFE_DELETE(g_pRoundTime);
	SAFE_DELETE(g_pTripTime);
	SAFE_DELETE(g_pScore);
	SAFE_DELETE(g_pCombo);
	SAFE_DELETE(g_pToast);
	SAFE_DELETE(g_pResultBack);
	SAFE_DELETE(g_pResultTitle);
	SAFE_DELETE(g_pResultLine1);
	SAFE_DELETE(g_pResultLine2);
	SAFE_DELETE(g_pResultNew);
	SAFE_DELETE(g_pResultHint);
}

bool Taxi_Start(void)
{
	g_Spots.assign(MAX_SPOTS, FieldMissionSpot{});
	const int count = Field_GetMissionSpots(g_Spots.data(), MAX_SPOTS);
	g_Spots.resize(count);
	if (count < 3 || !Player_IsReady())
	{
		g_Spots.clear();
		return false;
	}

	g_RoundLeft = ROUND_SECONDS;
	g_Score = 0;
	g_Combo = 0;
	g_Delivered = 0;
	g_Failed = 0;
	g_OrdersDone = 0;
	g_RingPasses = 0;
	g_UnderRoof = false;
	g_NewBest = false;
	g_ToastTimer = 0.0f;
	g_Anim = 0.0f;
	g_HasLastTick = false;
	g_Order = Order::None;
	g_AltitudeState = 0;
	LoadBest();

	if (!g_Marker)
	{
		g_Marker = new Billboard(
			{ 0.0f, 0.0f, 0.0f }, { 8.0f, 8.0f }, { 0.0f, 0.0f, 0.0f },
			TAXI_MARKER_TEXTURE, true);
		g_Marker->SetBillboardMode(true);
		g_Marker->SetWallFadeEnabled(false);
		g_Marker->SetIgnoreLighting(true);
		g_Marker->SetColor(PICKUP_COLOR);
		for (int i = 0; i < PILLAR_COUNT; ++i)
		{
			Billboard* piece = new Billboard(
				{ 0.0f, 0.0f, 0.0f }, { 3.0f, 3.0f }, { 0.0f, 0.0f, 0.0f },
				TAXI_MARKER_TEXTURE, true);
			piece->SetBillboardMode(true);
			piece->SetWallFadeEnabled(false);
			piece->SetIgnoreLighting(true);
			piece->SetColor({ PICKUP_COLOR.x, PICKUP_COLOR.y, PICKUP_COLOR.z, 0.55f });
			g_Pillar.push_back(piece);
		}
		g_NameLabel = new BillboardDrawFont(
			{ 0.0f, 0.0f, 0.0f }, 4.0f, PICKUP_COLOR, "");
		g_LabelHeight = 4.0f;

		g_WayMarker = new Billboard(
			{ 0.0f, 0.0f, 0.0f }, { 6.0f, 6.0f }, { 0.0f, 0.0f, 0.0f },
			TAXI_MARKER_TEXTURE, true);
		g_WayMarker->SetBillboardMode(true);
		g_WayMarker->SetWallFadeEnabled(false);
		g_WayMarker->SetIgnoreLighting(true);
		g_WayMarker->SetColor(WAYPOINT_COLOR);
		g_WayLabel = new BillboardDrawFont(
			{ 0.0f, 0.0f, 0.0f }, 3.0f, WAYPOINT_COLOR, "");
		g_WayLabelHeight = 3.0f;
	}
	g_MarkerIsDeliver = true;	// 初回のスタイル適用を必ず通す

	BeginPickup();
	GameAudio_SetBgmRace();
	ShowToast("送迎ミッション開始！\n乗り降りは地上のマーカー。低く降りてこい！", { 1.0f, 0.9f, 0.3f, 1.0f });
	return true;
}

void Taxi_Stop(void)
{
	SetPickupSpeedBoost(false);
	g_Phase = Phase::Off;
	g_Spots.clear();
	SAFE_DELETE(g_Marker);
	for (Billboard* piece : g_Pillar)
	{
		delete piece;
	}
	g_Pillar.clear();
	SAFE_DELETE(g_NameLabel);
	SAFE_DELETE(g_WayMarker);
	SAFE_DELETE(g_WayLabel);
}

bool Taxi_IsFinished(void)
{
	return g_Phase == Phase::Result;
}

void Taxi_Update(void)
{
	if (g_Phase == Phase::Off || !Player_IsReady())
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	float dt = 0.0f;
	if (g_HasLastTick)
	{
		dt = std::chrono::duration<float>(now - g_LastTick).count();
		dt = (std::min)((std::max)(dt, 0.0f), 0.1f);	// メニューやフォトモード明けで時間が飛ばないようにする
	}
	g_LastTick = now;
	g_HasLastTick = true;
	g_Anim += dt;
	if (g_ToastTimer > 0.0f)
	{
		g_ToastTimer -= dt;
	}

	if (g_Phase == Phase::Result)
	{
		return;
	}

	g_RoundLeft -= dt;
	if (g_RoundLeft <= 0.0f)
	{
		g_RoundLeft = 0.0f;
		FinishRound();
		return;
	}

	UpdateAltitude(dt);
	UpdateWaypoint();
	UpdateRingPass();

	if (g_Phase == Phase::Deliver)
	{
		g_TripLeft -= dt;
		if (g_TripLeft <= 0.0f)
		{
			OnTripTimeout();
		}
	}

	if (IsInsideRing(TargetPos(), MARKER_MIN_SIZE * 0.5f))
	{
		if (g_Phase == Phase::Pickup)
		{
			OnArrivePickup();
		}
		else if (g_Phase == Phase::Deliver)
		{
			OnArriveDeliver();
		}
	}
	UpdateMarkers();
}

void Taxi_Draw(void)
{
	if ((g_Phase != Phase::Pickup && g_Phase != Phase::Deliver) || !g_Marker)
	{
		return;
	}
	g_Marker->Draw();
	for (Billboard* piece : g_Pillar)
	{
		piece->Draw();
	}
	if (g_NameLabel)
	{
		g_NameLabel->Draw();
	}
	if (IsWaypointActive())
	{
		if (g_WayMarker)
		{
			g_WayMarker->Draw();
		}
		if (g_WayLabel)
		{
			g_WayLabel->Draw();
		}
	}
}

void Taxi_DrawHud(void)
{
	if (g_Phase == Phase::Off)
	{
		return;
	}

	if (g_Phase == Phase::Result)
	{
		if (g_pResultBack) g_pResultBack->Draw();
		if (g_pResultTitle) g_pResultTitle->Draw();
		if (g_pResultLine1) g_pResultLine1->Draw();
		if (g_pResultLine2) g_pResultLine2->Draw();
		if (g_NewBest && g_pResultNew && fmodf(g_Anim, 0.6f) < 0.45f)
		{
			g_pResultNew->Draw();
		}
		if (g_pResultHint) g_pResultHint->Draw();
		return;
	}

	const bool deliver = g_Phase == Phase::Deliver;
	const XMFLOAT3 playerPos = Player_GetPos();
	const XMFLOAT3 target = TargetPos();
	const float dist = Distance3D(playerPos, target);
	const int pickupIndex = g_PickupSpot;
	const int dropIndex = g_DropSpot;

	if (pickupIndex >= 0 && g_pInstruct)
	{
		if (deliver && dropIndex >= 0)
		{
			SetTextIf(
				g_pInstruct,
				std::string("お客さんを『") + g_Spots[dropIndex].name + "』へ送り届けろ！");
		}
		else
		{
			SetTextIf(
				g_pInstruct,
				std::string("『") + g_Spots[pickupIndex].name + "』のお客さんを迎えに行け！");
		}
		g_pInstruct->Draw();
	}

	if (deliver && g_Order != Order::None && g_pOrder)
	{
		SetTextIf(g_pOrder, BuildOrderText());
		g_pOrder->Draw();
	}

	if (g_pArrow)
	{
		// 進行方向（カメラのヨー）に対する目的地の向き。0度＝画面の上、正＝右へ回る
		const float targetYaw = atan2f(target.x - playerPos.x, target.z - playerPos.z);
		const float relative = targetYaw - XMConvertToRadians(PlayerCamera_GetYaw());
		g_pArrow->SetRot(XMConvertToDegrees(relative));
		g_pArrow->Draw();
	}
	if (g_pDistance)
	{
		char text[96] = {};
		float altitude = 0.0f;
		GetAltitude(&altitude);
		snprintf(
			text, sizeof(text), "リングまで %d m   高度 %d m",
			static_cast<int>(dist * UNIT_METERS),
			static_cast<int>((std::max)(altitude, 0.0f) * UNIT_METERS));
		SetTextIf(g_pDistance, text);
		g_pDistance->Draw();
	}
	if (deliver)
	{
		if (g_AltitudeState == 1 && g_pLowText)
		{
			g_pLowText->Draw();
		}
		else if (g_AltitudeState == 2 && g_pHighText)
		{
			g_pHighText->Draw();
		}
	}
	if (g_pRoundTime)
	{
		SetTextIf(g_pRoundTime, "TIME " + FormatSeconds(g_RoundLeft));
		g_pRoundTime->Draw();
	}
	if (deliver && g_pTripTime)
	{
		char text[32] = {};
		snprintf(text, sizeof(text), "お客さん %.1f 秒", (std::max)(g_TripLeft, 0.0f));
		SetTextIf(g_pTripTime, text);
		g_pTripTime->SetColor(g_TripLeft < 8.0f
			? XMFLOAT4(1.0f, 0.3f, 0.25f, 1.0f)
			: XMFLOAT4(1.0f, 0.9f, 0.3f, 1.0f));
		g_pTripTime->Draw();
	}
	if (g_pScore)
	{
		SetTextIf(g_pScore, "SCORE " + std::to_string(g_Score));
		g_pScore->Draw();
	}
	if (g_pCombo && g_Combo > 0)
	{
		SetTextIf(g_pCombo, std::to_string(g_Combo) + " 連続送迎！");
		g_pCombo->Draw();
	}
	if (g_pToast && g_ToastTimer > 0.0f)
	{
		g_pToast->Draw();
	}
}
