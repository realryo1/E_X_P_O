#define NOMINMAX

#include "model.h"
#include "anim_sprite3d.h"

#include "camera.h"
#include <algorithm>
#include <cmath>
#include <map>

// クォータニオン球面線形補間
static XMFLOAT4 QuatSlerp(const XMFLOAT4& q1, const XMFLOAT4& q2, float t)
{
	XMVECTOR v1 = XMLoadFloat4(&q1);
	XMVECTOR v2 = XMLoadFloat4(&q2);
	XMVECTOR result = XMQuaternionSlerp(v1, v2, t);
	XMFLOAT4 qResult;
	XMStoreFloat4(&qResult, result);
	return qResult;
}

// クォータニオンから回転行列への変換
static XMMATRIX QuatToMatrix(const XMFLOAT4& q)
{
	XMVECTOR quat = XMLoadFloat4(&q);
	return XMMatrixRotationQuaternion(quat);
}

// アロケーションを避けるため、suffixes のどれかで終わっているかだけを判定し、
// 一致した場合はそのベース名の長さを返す。一致しない場合は0を返す。
static size_t GetAssimpFbxBaseNameLength(const char* nodeName)
{
	if (!nodeName) return 0;
	size_t len = strlen(nodeName);

	static const char* suffixes[] = {
		"_$AssimpFbx$_Translation",
		"_$AssimpFbx$_Rotation",
		"_$AssimpFbx$_Scaling",
		"_$AssimpFbx$_PreRotation",
		"_$AssimpFbx$_PostRotation",
		"_$AssimpFbx$_RotationPivot",
		"_$AssimpFbx$_RotationPivotInverse",
		"_$AssimpFbx$_ScalingPivot",
		"_$AssimpFbx$_ScalingPivotInverse",
		"_$AssimpFbx$_RotationOffset",
		"_$AssimpFbx$_ScalingOffset",
		"_$AssimpFbx$_GeometricTranslation",
		"_$AssimpFbx$_GeometricRotation",
		"_$AssimpFbx$_GeometricScaling",
	};
	for (const char* suffix : suffixes)
	{
		size_t suffixLen = strlen(suffix);
		if (len > suffixLen &&
			strcmp(nodeName + (len - suffixLen), suffix) == 0)
		{
			return len - suffixLen;
		}
	}
	return 0;
}

// ノード階層を再帰的に走査してボーン最終行列を計算する内部関数
static void CalcBoneMatricesRecursive(
	const aiNode* node,
	XMMATRIX parentTransform,
	const AnimationClip& clip,
	double time,
	const std::unordered_map<std::string, int>& nodeToAnimIndex,
	MODEL* model,
	BoneMatrices& outMatrices,
	const AnimationClip* overrideClip = nullptr,
	double overrideTime = 0.0,
	const std::unordered_map<std::string, int>* overrideNodeToAnimIndex = nullptr,
	const std::vector<std::string>* overrideBoneNames = nullptr,
	bool isOverrideActive = false)
{
	const char* nodeNameCStr = node->mName.data;

	// このノードのローカル変換（デフォルトはノードのバインドポーズ変換）
	XMMATRIX nodeTransform = AiMatrixToXMMatrix(node->mTransformation);

	// $AssimpFbx$ ノードの処理:
	size_t baseLen = GetAssimpFbxBaseNameLength(nodeNameCStr);
	bool isFbxSubNode = (baseLen > 0);

	// オーバーライド状態の決定
	bool currentOverrideActive = isOverrideActive;
	if (!currentOverrideActive && overrideBoneNames)
	{
		for (const auto& boneName : *overrideBoneNames)
		{
			if (isFbxSubNode)
			{
				if (boneName.size() == baseLen &&
					strncmp(nodeNameCStr, boneName.c_str(), baseLen) == 0)
				{
					currentOverrideActive = true;
					break;
				}
			}
			else
			{
				if (strcmp(nodeNameCStr, boneName.c_str()) == 0)
				{
					currentOverrideActive = true;
					break;
				}
			}
		}
	}

	if (isFbxSubNode)
	{
		// 対応する元ノードにアニメーションチャンネルが存在する場合は変換を無視
		const auto& targetMap = (currentOverrideActive && overrideNodeToAnimIndex) ? *overrideNodeToAnimIndex : nodeToAnimIndex;
		std::string baseName(nodeNameCStr, baseLen);
		if (targetMap.find(baseName) != targetMap.end())
		{
			nodeTransform = XMMatrixIdentity();
		}
	}

	std::string nodeName(nodeNameCStr); // マップ検索キーとしてアロケーション

	// アニメーションチャンネルがあれば補間値で上書き
	if (currentOverrideActive && overrideClip && overrideNodeToAnimIndex)
	{
		auto it = overrideNodeToAnimIndex->find(nodeName);
		if (it != overrideNodeToAnimIndex->end())
		{
			int trackIdx = it->second;
			if (trackIdx >= 0 && trackIdx < (int)overrideClip->tracks.size())
			{
				const BoneKeyframes& kf = overrideClip->tracks[trackIdx];

				XMFLOAT3 trans = AnimSprite3D::InterpolateVec3(kf.trans, overrideTime);
				XMFLOAT4 rot = AnimSprite3D::InterpolateQuat(kf.rot, overrideTime);
				XMFLOAT3 scale = AnimSprite3D::InterpolateVec3(kf.scale, overrideTime);
				if (kf.scale.empty()) scale = XMFLOAT3(1.0f, 1.0f, 1.0f);

				XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
				XMMATRIX R = QuatToMatrix(rot);
				XMMATRIX T = XMMatrixTranslation(trans.x, trans.y, trans.z);

				nodeTransform = S * R * T;
			}
		}
	}
	else
	{
		auto it = nodeToAnimIndex.find(nodeName);
		if (it != nodeToAnimIndex.end())
		{
			int trackIdx = it->second;
			if (trackIdx >= 0 && trackIdx < (int)clip.tracks.size())
			{
				const BoneKeyframes& kf = clip.tracks[trackIdx];

				XMFLOAT3 trans = AnimSprite3D::InterpolateVec3(kf.trans, time);
				XMFLOAT4 rot = AnimSprite3D::InterpolateQuat(kf.rot, time);
				XMFLOAT3 scale = AnimSprite3D::InterpolateVec3(kf.scale, time);
				if (kf.scale.empty()) scale = XMFLOAT3(1.0f, 1.0f, 1.0f);

				XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
				XMMATRIX R = QuatToMatrix(rot);
				XMMATRIX T = XMMatrixTranslation(trans.x, trans.y, trans.z);

				nodeTransform = S * R * T;
			}
		}
	}

	XMMATRIX globalTransform = nodeTransform * parentTransform;

	// このノードがボーンならば最終行列を計算
	auto boneIt = model->BoneNameToIndex.find(nodeName);
	if (boneIt != model->BoneNameToIndex.end())
	{
		unsigned int boneIdx = boneIt->second;
		if (boneIdx < BoneMatrices::MAX_BONES)
		{
			outMatrices.matrices[boneIdx] = model->BoneOffsetMatrices[boneIdx] * globalTransform * model->GlobalInverseTransform;
		}
	}

	// 子ノードを再帰
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		CalcBoneMatricesRecursive(
			node->mChildren[i],
			globalTransform,
			clip,
			time,
			nodeToAnimIndex,
			model,
			outMatrices,
			overrideClip,
			overrideTime,
			overrideNodeToAnimIndex,
			overrideBoneNames,
			currentOverrideActive
		);
	}
}

void AnimSprite3D::InitializeBones()
{
	if (!m_Model || !m_Model->AiScene)
	{
		return;
	}

	m_BoneCount = m_Model->TotalBoneCount;


	for (unsigned int i = 0; i < BoneMatrices::MAX_BONES; i++)
	{
		m_BoneMatrices.matrices[i] = XMMatrixIdentity();
	}
	m_BoneMatrices.boneCount = m_BoneCount;
}

XMFLOAT3 AnimSprite3D::InterpolateVec3(const std::vector<KeyVec3>& keys, double time)
{
	if (keys.empty())
	{
		return XMFLOAT3(0.0f, 0.0f, 0.0f);
	}

	if (keys.size() == 1)
	{
		return keys[0].value;
	}

	// 時間の範囲外チェック
	if (time <= keys.front().time)
	{
		return keys.front().value;
	}
	if (time >= keys.back().time)
	{
		return keys.back().value;
	}

	// キーフレームを二分探索で見つける
	size_t keyIndex = 0;
	size_t low = 0;
	size_t high = keys.size() - 2;
	while (low <= high)
	{
		size_t mid = low + (high - low) / 2;
		if (time >= keys[mid].time && time < keys[mid + 1].time)
		{
			keyIndex = mid;
			break;
		}
		else if (time < keys[mid].time)
		{
			if (mid == 0) break;
			high = mid - 1;
		}
		else
		{
			low = mid + 1;
		}
	}

	const KeyVec3& key1 = keys[keyIndex];
	const KeyVec3& key2 = keys[keyIndex + 1];

	double timeDiff = key2.time - key1.time;
	if (timeDiff <= 0.0)
	{
		return key1.value;
	}

	float t = (float)((time - key1.time) / timeDiff);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	XMFLOAT3 result;
	result.x = key1.value.x + (key2.value.x - key1.value.x) * t;
	result.y = key1.value.y + (key2.value.y - key1.value.y) * t;
	result.z = key1.value.z + (key2.value.z - key1.value.z) * t;

	return result;
}

XMFLOAT4 AnimSprite3D::InterpolateQuat(const std::vector<KeyQuat>& keys, double time)
{
	if (keys.empty())
	{
		return XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	if (keys.size() == 1)
	{
		return keys[0].value;
	}

	// 時間の範囲外チェック
	if (time <= keys.front().time)
	{
		return keys.front().value;
	}
	if (time >= keys.back().time)
	{
		return keys.back().value;
	}

	// キーフレームを二分探索で見つける
	size_t keyIndex = 0;
	size_t low = 0;
	size_t high = keys.size() - 2;
	while (low <= high)
	{
		size_t mid = low + (high - low) / 2;
		if (time >= keys[mid].time && time < keys[mid + 1].time)
		{
			keyIndex = mid;
			break;
		}
		else if (time < keys[mid].time)
		{
			if (mid == 0) break;
			high = mid - 1;
		}
		else
		{
			low = mid + 1;
		}
	}

	const KeyQuat& key1 = keys[keyIndex];
	const KeyQuat& key2 = keys[keyIndex + 1];

	double timeDiff = key2.time - key1.time;
	if (timeDiff <= 0.0)
	{
		return key1.value;
	}

	float t = (float)((time - key1.time) / timeDiff);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	XMFLOAT4 result = QuatSlerp(key1.value, key2.value, t);
	
	return result;
}

void AnimSprite3D::UpdateAnimation(float dt)
{
	m_BoneMatricesUpdated = false;

	// ブレンド処理
	if (m_BlendState.isBlending)
	{
		m_BlendState.blendElapsed += dt;
		
		if (m_BlendState.blendElapsed >= m_BlendState.blendDuration)
		{
			// ブレンド完了：新しいアニメーションに切り替え
			m_BlendState.isBlending = false;
			SetAnimationClip(m_BlendState.targetClip);
			PlayAnimation(m_AnimState.loop);
		}
	}

	if (!m_AnimState.play || !m_AnimState.clip)
	{
		return;
	}

	// dt（秒単位）をティック単位に変換して時間を進める
	double ticksPerSecond = m_AnimState.clip->tps;
	if (ticksPerSecond <= 0.0)
	{
		ticksPerSecond = 24.0;  // デフォルト値
	}

	m_AnimState.time += dt * ticksPerSecond;

	// アニメーション終了判定
	if (m_AnimState.time >= m_AnimState.clip->duration)
	{
		if (m_AnimState.loop)
		{
			double duration = m_AnimState.clip->duration;
			if (duration > 0.0)
			{
				// ループ処理：時間を持ち越さず、正確にリセット
				m_AnimState.time = fmod(m_AnimState.time, duration);
				if (m_AnimState.time < 0.0)
					m_AnimState.time += duration;
			}
		}
		else
		{
			m_AnimState.time = m_AnimState.clip->duration;
			m_AnimState.play = false;
		}
	}

	// オーバーライド用の時間更新
	if (m_OverrideAnimState.isActive && m_OverrideAnimState.play)
	{
		double overrideTicksPerSecond = m_OverrideAnimState.clip->tps;
		if (overrideTicksPerSecond <= 0.0)
		{
			overrideTicksPerSecond = 24.0;
		}

		m_OverrideAnimState.time += dt * overrideTicksPerSecond * m_OverrideAnimState.speed;

		if (m_OverrideAnimState.time >= m_OverrideAnimState.clip->duration)
		{
			if (m_OverrideAnimState.loop)
			{
				double duration = m_OverrideAnimState.clip->duration;
				if (duration > 0.0)
				{
					m_OverrideAnimState.time = fmod(m_OverrideAnimState.time, duration);
					if (m_OverrideAnimState.time < 0.0)
						m_OverrideAnimState.time += duration;
				}
			}
			else
			{
				m_OverrideAnimState.time = m_OverrideAnimState.clip->duration;
				m_OverrideAnimState.play = false;
			}
		}
	}

	UpdateBoneMatrices();
}

void AnimSprite3D::UpdateBoneMatrices()
{
	if (!m_Model || !m_Model->AiScene || !m_Model->HasSkinning)
		return;

	if (m_BoneMatricesUpdated)
		return;

	// ブレンド中の場合は特別処理
	if (m_BlendState.isBlending)
	{
		double blendT = (m_BlendState.blendDuration > 0.0) ? (m_BlendState.blendElapsed / m_BlendState.blendDuration) : 1.0;
		if (blendT > 1.0) blendT = 1.0;
		
		UpdateBoneMatricesForState(m_BlendState.previousState, m_BoneMatrices);
		
		BoneMatrices targetMatrices;
		AnimationState tempState;
		tempState.clip = m_BlendState.targetClip;
		tempState.time = 0.0;
		UpdateBoneMatricesForState(tempState, targetMatrices);
		
		float blendF = (float)blendT;
		for (unsigned int i = 0; i < m_Model->TotalBoneCount && i < BoneMatrices::MAX_BONES; i++)
		{
			XMFLOAT4X4 fromM, toM;
			XMStoreFloat4x4(&fromM, m_BoneMatrices.matrices[i]);
			XMStoreFloat4x4(&toM, targetMatrices.matrices[i]);
			
			XMFLOAT4X4 blendM;
			float* pf = (float*)&fromM;
			float* pt = (float*)&toM;
			float* pb = (float*)&blendM;
			for (int j = 0; j < 16; j++)
				pb[j] = pf[j] * (1.0f - blendF) + pt[j] * blendF;
			
			m_BoneMatrices.matrices[i] = XMLoadFloat4x4(&blendM);
		}
		return;
	}

	if (!m_AnimState.clip || m_AnimState.clip->tracks.empty())
		return;

	const AnimationClip& clip = *m_AnimState.clip;
	double time = m_AnimState.time;
	if (time < 0.0) time = 0.0;
	if (time > clip.duration) time = clip.duration;

	// 部分アニメーションオーバーライドが有効であれば取得
	const AnimationClip* pOverrideClip = nullptr;
	double overrideTime = 0.0;
	const std::unordered_map<std::string, int>* pOverrideNodeToAnimIndex = nullptr;
	const std::vector<std::string>* pOverrideBoneNames = nullptr;

	if (m_OverrideAnimState.isActive && m_OverrideAnimState.play)
	{
		pOverrideClip = m_OverrideAnimState.clip;
		overrideTime = m_OverrideAnimState.time;
		pOverrideNodeToAnimIndex = &m_OverrideAnimState.nodeToAnimIndex;
		pOverrideBoneNames = &m_OverrideAnimState.startBoneNames;
	}

	// ノード階層走査を開始（ルートノードから）
	CalcBoneMatricesRecursive(
		m_Model->AiScene->mRootNode,
		XMMatrixIdentity(),
		clip, time,
		m_Model->NodeToAnimIndex,
		m_Model,
		m_BoneMatrices,
		pOverrideClip,
		overrideTime,
		pOverrideNodeToAnimIndex,
		pOverrideBoneNames,
		false
	);

	m_BoneMatrices.boneCount = m_Model->TotalBoneCount;
	m_BoneMatricesUpdated = true;
}

void AnimSprite3D::Draw(void)
{
	if (!IsVisibleFromCamera())
	{
		return;
	}

	if (m_Model)
	{
		XMFLOAT4 drawColor = m_UseOriginalColor ? m_OriginalColor : m_Color;
		bool shouldApplyColorReplace = !m_UseOriginalColor;

		ModelAnimationDraw(
			m_Model,
			GetPos(),
			GetRot(),
			GetScale(),
			m_BoneMatrices,
			drawColor,
			shouldApplyColorReplace,
			m_ShaderType,
			m_AnimState.clip,
			m_AnimState.time
		);
	}
	else
	{
	}
}

// Assimpアニメーション情報からAnimationClipを生成
AnimationClip AnimSprite3D::ExtractAnimationFromAssimp(const aiAnimation* aiAnim)
{
    AnimationClip clip;
    
    clip.duration = aiAnim->mDuration;
    clip.tps = aiAnim->mTicksPerSecond;
    
    if (clip.tps == 0.0)
    {
        clip.tps = 24.0;
    }
    
    
    // チャンネルインデックスをそのままトラックインデックスとして使用
    // RenderNodeAnimationのNodeToAnimIndex（ノード名→チャンネルインデックス）と一致させる
    clip.tracks.resize(aiAnim->mNumChannels);
    
    for (unsigned int c = 0; c < aiAnim->mNumChannels; c++)
    {
        aiNodeAnim* nodeAnim = aiAnim->mChannels[c];
        
        BoneKeyframes& keyframes = clip.tracks[c];
        
        // 位置キーフレーム
        keyframes.trans.resize(nodeAnim->mNumPositionKeys);
        for (unsigned int k = 0; k < nodeAnim->mNumPositionKeys; k++)
        {
            const aiVectorKey& key = nodeAnim->mPositionKeys[k];
            keyframes.trans[k].time = key.mTime;
            keyframes.trans[k].value = XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z);
        }
        
        // 回転キーフレーム
        keyframes.rot.resize(nodeAnim->mNumRotationKeys);
        for (unsigned int k = 0; k < nodeAnim->mNumRotationKeys; k++)
        {
            const aiQuatKey& key = nodeAnim->mRotationKeys[k];
            keyframes.rot[k].time = key.mTime;
            keyframes.rot[k].value = XMFLOAT4(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w);
        }
        
        // スケーリングキーフレーム
        keyframes.scale.resize(nodeAnim->mNumScalingKeys);
        for (unsigned int k = 0; k < nodeAnim->mNumScalingKeys; k++)
        {
            const aiVectorKey& key = nodeAnim->mScalingKeys[k];
            keyframes.scale[k].time = key.mTime;
            keyframes.scale[k].value = XMFLOAT3(key.mValue.x, key.mValue.y, key.mValue.z);
        }
    }
    
    
    return clip;
}

// ボーン名からインデックスを探す
int AnimSprite3D::FindBoneIndex(const char* boneName)
{
    // ボーン一覧で検索
    for (int i = 0; i < m_BoneCount; i++)
    {
        if (strcmp(m_AiBones[i]->mName.data, boneName) == 0)
        {
            return i;
        }
    }
    
    // ボーンがない場合、モデルのNodeToAnimIndexを使用
    if (m_Model && !m_Model->NodeToAnimIndex.empty())
    {
        auto it = m_Model->NodeToAnimIndex.find(boneName);
        if (it != m_Model->NodeToAnimIndex.end())
        {
            return it->second;
        }
        return -1;
    }
    
    return -1;
}

// ============================================================
// 複数アニメーション対応: FBX内のアニメーションを名前で直接再生
// =============================================================

bool AnimSprite3D::PlayAnimationByName(const char* animName, bool loop)
{
    if (!m_Model || !m_Model->AiScene || !animName)
    {
        return false;
    }

    // 現在再生中のアニメーションと同じ名前の場合は無視
    if (m_AnimState.play && m_AnimState.currentAnimName == animName)
    {
        return true;  // 既に再生中なので成功として扱う
    }


    // FBX内から名前でアニメーションを検索（完全一致 → 部分一致の順）
    aiAnimation* foundAnim = nullptr;

    // まず完全一致で検索
    for (unsigned int i = 0; i < m_Model->AiScene->mNumAnimations; i++)
    {
        aiAnimation* aiAnim = m_Model->AiScene->mAnimations[i];
        
        if (strcmp(aiAnim->mName.data, animName) == 0)
        {
            foundAnim = aiAnim;
            break;
        }
    }

    // 完全一致が見つからなかった場合、部分一致で検索
    if (!foundAnim)
    {
        for (unsigned int i = 0; i < m_Model->AiScene->mNumAnimations; i++)
        {
            aiAnimation* aiAnim = m_Model->AiScene->mAnimations[i];
            std::string name = aiAnim->mName.data;
            if (name.find(animName) != std::string::npos)
            {
                foundAnim = aiAnim;
                break;
            }
        }
    }

    // アニメーションが1つだけの場合はそれを使用（名前不一致でも）
    if (!foundAnim && m_Model->AiScene->mNumAnimations == 1)
    {
        foundAnim = m_Model->AiScene->mAnimations[0];
    }

    if (foundAnim)
    {
        // NodeToAnimIndexを再生対象のアニメーションのチャンネルから再構築
        m_Model->NodeToAnimIndex.clear();
        for (unsigned int c = 0; c < foundAnim->mNumChannels; c++)
        {
            m_Model->NodeToAnimIndex[foundAnim->mChannels[c]->mNodeName.data] = c;
        }

        // キャッシュから取得、無ければ生成
        const AnimationClip* pClip = nullptr;
        auto it = m_Model->AnimationClips.find(animName);
        if (it != m_Model->AnimationClips.end())
        {
            pClip = it->second;
        }
        else
        {
            AnimationClip* newClip = new AnimationClip(ExtractAnimationFromAssimp(foundAnim));
            m_Model->AnimationClips[animName] = newClip;
            pClip = newClip;
        }
        
        // 別のアニメーションが再生中の場合、ブレンド遷移を開始
        if (m_AnimState.play && m_AnimState.currentAnimName != animName)
        {
            m_BlendState.previousState = m_AnimState;
            m_BlendState.targetClip = pClip;
            m_BlendState.isBlending = true;
            m_BlendState.blendElapsed = 0.0;
            m_BlendState.blendDuration = 0.3;
        }
        else
        {
            // 初回起動時は通常の再生開始
            SetAnimationClip(pClip);
            PlayAnimation(loop);
        }
        
        m_AnimState.currentAnimName = animName;
        m_AnimState.loop = loop;
        
        return true;
    }

    return false;
}

unsigned int AnimSprite3D::GetAnimationCount() const
{
    if (!m_Model || !m_Model->AiScene)
    {
        return 0;
    }
    return m_Model->AiScene->mNumAnimations;
}

const char* AnimSprite3D::GetAnimationName(unsigned int index) const
{
    if (!m_Model || !m_Model->AiScene || index >= m_Model->AiScene->mNumAnimations)
    {
        return nullptr;
    }
    return m_Model->AiScene->mAnimations[index]->mName.data;
}

bool AnimSprite3D::PlayAnimationByIndex(unsigned int index, bool loop)
{
    if (!m_Model || !m_Model->AiScene || index >= m_Model->AiScene->mNumAnimations)
    {
        return false;
    }

    aiAnimation* aiAnim = m_Model->AiScene->mAnimations[index];

    // NodeToAnimIndexを再生対象のアニメーションのチャンネルから再構築
    m_Model->NodeToAnimIndex.clear();
    for (unsigned int c = 0; c < aiAnim->mNumChannels; c++)
    {
        m_Model->NodeToAnimIndex[aiAnim->mChannels[c]->mNodeName.data] = c;
    }

    const char* animName = aiAnim->mName.data;
    const AnimationClip* pClip = nullptr;
    auto it = m_Model->AnimationClips.find(animName);
    if (it != m_Model->AnimationClips.end())
    {
        pClip = it->second;
    }
    else
    {
        AnimationClip* newClip = new AnimationClip(ExtractAnimationFromAssimp(aiAnim));
        m_Model->AnimationClips[animName] = newClip;
        pClip = newClip;
    }
    SetAnimationClip(pClip);
    PlayAnimation(loop);
    m_AnimState.currentAnimName = animName;

    return true;
}

// ============================================================
// ブレンド用補助関数
// ============================================================

void AnimSprite3D::UpdateBoneMatricesForState(const AnimationState& state, BoneMatrices& outMatrices)
{
	if (!m_Model || !m_Model->AiScene || !m_Model->HasSkinning)
	{
		for (unsigned int i = 0; i < BoneMatrices::MAX_BONES; i++)
			outMatrices.matrices[i] = XMMatrixIdentity();
		return;
	}

	if (!state.clip || state.clip->tracks.empty())
	{
		for (unsigned int i = 0; i < BoneMatrices::MAX_BONES; i++)
			outMatrices.matrices[i] = XMMatrixIdentity();
		return;
	}

	const AnimationClip& clip = *state.clip;
	double time = state.time;
	if (time < 0.0) time = 0.0;
	if (time > clip.duration) time = clip.duration;

	CalcBoneMatricesRecursive(
		m_Model->AiScene->mRootNode,
		XMMatrixIdentity(),
		clip, time,
		m_Model->NodeToAnimIndex,
		m_Model,
		outMatrices
	);

	outMatrices.boneCount = m_Model->TotalBoneCount;
}

bool AnimSprite3D::PlayOverrideAnimation(const char* animName, const char* startBoneName, bool loop)
{
	std::vector<std::string> bones;
	if (startBoneName)
	{
		bones.push_back(startBoneName);
	}
	return PlayOverrideAnimation(animName, bones, loop);
}

bool AnimSprite3D::PlayOverrideAnimation(const char* animName, const std::vector<std::string>& startBoneNames, bool loop)
{
	if (!m_Model || !m_Model->AiScene || !animName || startBoneNames.empty())
	{
		return false;
	}

	aiAnimation* foundAnim = nullptr;

	// 完全一致で検索
	for (unsigned int i = 0; i < m_Model->AiScene->mNumAnimations; i++)
	{
		aiAnimation* aiAnim = m_Model->AiScene->mAnimations[i];
		if (strcmp(aiAnim->mName.data, animName) == 0)
		{
			foundAnim = aiAnim;
			break;
		}
	}

	// 部分一致で検索
	if (!foundAnim)
	{
		for (unsigned int i = 0; i < m_Model->AiScene->mNumAnimations; i++)
		{
			aiAnimation* aiAnim = m_Model->AiScene->mAnimations[i];
			std::string name = aiAnim->mName.data;
			if (name.find(animName) != std::string::npos)
			{
				foundAnim = aiAnim;
				break;
			}
		}
	}

	if (foundAnim)
	{
		m_OverrideAnimState.nodeToAnimIndex.clear();
		for (unsigned int c = 0; c < foundAnim->mNumChannels; c++)
		{
			m_OverrideAnimState.nodeToAnimIndex[foundAnim->mChannels[c]->mNodeName.data] = c;
		}

		const AnimationClip* pClip = nullptr;
		auto it = m_Model->AnimationClips.find(animName);
		if (it != m_Model->AnimationClips.end())
		{
			pClip = it->second;
		}
		else
		{
			AnimationClip* newClip = new AnimationClip(ExtractAnimationFromAssimp(foundAnim));
			m_Model->AnimationClips[animName] = newClip;
			pClip = newClip;
		}

		m_OverrideAnimState.clip = pClip;
		m_OverrideAnimState.time = 0.0;
		m_OverrideAnimState.play = true;
		m_OverrideAnimState.loop = loop;
		m_OverrideAnimState.speed = 1.0;
		m_OverrideAnimState.currentAnimName = animName;
		m_OverrideAnimState.startBoneNames = startBoneNames;
		m_OverrideAnimState.isActive = true;

		return true;
	}

	return false;
}

void AnimSprite3D::StopOverrideAnimation()
{
	m_OverrideAnimState.isActive = false;
	m_OverrideAnimState.play = false;
}
