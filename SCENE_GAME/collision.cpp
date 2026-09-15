#include "collision.h"
#include "glb_model.h"

#include <cmath>
#include <cfloat>
#include <cstring>
#include <vector>
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <windows.h>

using namespace DirectX;

namespace
{
	struct CollisionTriangle
	{
		XMFLOAT3 a;
		XMFLOAT3 b;
		XMFLOAT3 c;
	};

	static const float GRID_CELL_SIZE = 0.4f;
	static const size_t GRID_MIN_TRIANGLES = 64;
	static const int GRID_MAX_CELLS_PER_TRI_RING = 64;
	static const int GRID_MAX_CELLS_PER_TRI_BUILDING = 256;
	static const int GRID_MAX_CELLS = 2000000;
	static const float ASSIMP_GLOBAL_SCALE = 100.0f;

	struct CollisionMesh
	{
		std::vector<CollisionTriangle> localTris;
		std::vector<CollisionTriangle> worldTris;
		std::vector<XMFLOAT3> triMin;
		std::vector<XMFLOAT3> triMax;
		std::vector<int> largeTris;
		std::vector<unsigned int> visitStamp;
		std::vector<int> cellStart;
		std::vector<int> cellItems;
		unsigned int visitGen;
		float yBias;
		float cellSize;
		int gridOriginX;
		int gridOriginZ;
		int gridW;
		int gridH;
		XMMATRIX world;
		XMFLOAT3 boundsMin;
		XMFLOAT3 boundsMax;
		std::string sourceName;
		bool loadedFromBin;
		bool filterLattice;
		bool useGrid;
	};

	struct BakeJob
	{
		std::string glbPath;
		std::string binPath;
		XMMATRIX world;
		bool filterLattice;
	};

	struct BakeResult
	{
		CollisionMesh mesh;
		bool failed = false;
	};

	std::vector<CollisionMesh> g_Meshes;
	std::deque<BakeJob> g_Jobs;
	std::deque<BakeResult> g_Ready;
	std::thread g_Worker;
	std::mutex g_Mutex;
	std::atomic<bool> g_WorkerActive{ false };
	std::atomic<bool> g_WorkerJoinNeeded{ false };
	std::atomic<size_t> g_ProgressDone{ 0 };
	std::atomic<size_t> g_ProgressTotal{ 0 };
	std::atomic<int> g_ProgressStage{ COLLISION_STAGE_IDLE };
	std::vector<std::string> g_FrameHitNames;

	std::string PathStem(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		const size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
		const size_t dot = path.find_last_of('.');
		const size_t nameEnd =
			dot != std::string::npos && dot > nameStart ? dot : path.size();
		return path.substr(nameStart, nameEnd - nameStart);
	}

	std::string CollisionDisplayName(const std::string& sourceName)
	{
		if (sourceName == "expo_floor")
		{
			return "床";
		}
		if (sourceName == "expo_ring")
		{
			return "リング";
		}
		if (sourceName == "expo_pavilion_east_gate")
		{
			return "東ゲート高精細";
		}
		if (sourceName == "expo_pavilion_west_gate")
		{
			return "西ゲート高精細";
		}
		if (sourceName.find("expo_tile_lod2_far_") == 0)
		{
			return "遠景" + sourceName.substr(19);
		}
		if (sourceName.find("expo_tile_lod2_") == 0)
		{
			return "LOD2" + sourceName.substr(15);
		}
		return sourceName;
	}

	void RecordHit(const std::string& sourceName)
	{
		if (sourceName.empty())
		{
			return;
		}
		for (const std::string& existing : g_FrameHitNames)
		{
			if (existing == sourceName)
			{
				return;
			}
		}
		g_FrameHitNames.push_back(sourceName);
	}

	bool FileExists(const char* path)
	{
		if (!path) return false;
		const DWORD attrib = GetFileAttributesA(path);
		return attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	std::string DeriveBinPath(const char* glbPath)
	{
		if (!glbPath)
		{
			return {};
		}
		std::string path = glbPath;
		const char* needles[] = {
			"\\expomodel\\", "/expomodel/", "\\expomodel/", "/expomodel\\",
			"\\model\\", "/model/", "\\model/", "/model\\"
		};
		bool replaced = false;
		for (const char* needle : needles)
		{
			const size_t at = path.find(needle);
			if (at == std::string::npos)
			{
				continue;
			}
			const size_t len = std::strlen(needle);
			std::string mid = needle;
			const size_t expomodelAt = mid.find("expomodel");
			if (expomodelAt != std::string::npos)
			{
				mid.replace(expomodelAt, 9, "collision");
			}
			else
			{
				const size_t modelAt = mid.find("model");
				if (modelAt != std::string::npos)
				{
					mid.replace(modelAt, 5, "collision");
				}
			}
			path.replace(at, len, mid);
			replaced = true;
			break;
		}
		if (!replaced)
		{
			const size_t slash = path.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				path = std::string("asset\\collision\\") + path;
			}
			else
			{
				path = path.substr(0, slash + 1) + std::string("..\\collision\\") + path.substr(slash + 1);
			}
		}
		const size_t dot = path.find_last_of('.');
		const size_t slash = path.find_last_of("\\/");
		if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		{
			path.replace(dot, std::string::npos, ".bin");
		}
		else
		{
			path += ".bin";
		}
		return path;
	}

	XMFLOAT3 TransformPoint(const XMFLOAT3& p, const XMMATRIX& world)
	{
		XMVECTOR v = XMVector3Transform(XMLoadFloat3(&p), world);
		XMFLOAT3 out;
		XMStoreFloat3(&out, v);
		return out;
	}

	void ExpandBounds(XMFLOAT3* bMin, XMFLOAT3* bMax, const CollisionTriangle& worldTri)
	{
		const XMFLOAT3 pts[3] = { worldTri.a, worldTri.b, worldTri.c };
		for (int p = 0; p < 3; ++p)
		{
			bMin->x = (bMin->x < pts[p].x) ? bMin->x : pts[p].x;
			bMin->y = (bMin->y < pts[p].y) ? bMin->y : pts[p].y;
			bMin->z = (bMin->z < pts[p].z) ? bMin->z : pts[p].z;
			bMax->x = (bMax->x > pts[p].x) ? bMax->x : pts[p].x;
			bMax->y = (bMax->y > pts[p].y) ? bMax->y : pts[p].y;
			bMax->z = (bMax->z > pts[p].z) ? bMax->z : pts[p].z;
		}
	}

	int GridCell(float v, float cellSize)
	{
		return static_cast<int>(floorf(v / cellSize));
	}

	bool KeepRingCollisionTriangle(const CollisionTriangle& tri)
	{
		const XMVECTOR a = XMLoadFloat3(&tri.a);
		const XMVECTOR b = XMLoadFloat3(&tri.b);
		const XMVECTOR c = XMLoadFloat3(&tri.c);
		const XMVECTOR cross = XMVector3Cross(XMVectorSubtract(b, a), XMVectorSubtract(c, a));
		const float twiceArea = XMVectorGetX(XMVector3Length(cross));
		if (twiceArea < 1.0e-8f)
		{
			return false;
		}
		const float area = 0.5f * twiceArea;
		const float ny = XMVectorGetY(cross) / twiceArea;
		const float minWalkArea = 0.004f;
		const float minWallArea = 0.02f;
		if (ny > 0.35f && area >= minWalkArea)
		{
			return true;
		}
		if (area >= minWallArea)
		{
			return true;
		}
		return false;
	}

	std::vector<CollisionTriangle> LoadLocalTrisBin(const char* path)
	{
		std::vector<CollisionTriangle> tris;
		if (!FileExists(path))
		{
			return tris;
		}

		FILE* file = nullptr;
		if (fopen_s(&file, path, "rb") != 0 || !file)
		{
			return tris;
		}

		char magic[4] = {};
		unsigned int version = 0;
		unsigned int vertexCount = 0;
		unsigned int triangleCount = 0;
		unsigned int flags = 0;
		if (fread(magic, 1, 4, file) != 4
			|| fread(&version, 4, 1, file) != 1
			|| fread(&vertexCount, 4, 1, file) != 1
			|| fread(&triangleCount, 4, 1, file) != 1
			|| fread(&flags, 4, 1, file) != 1
			|| memcmp(magic, "EXCL", 4) != 0
			|| version != 1)
		{
			fclose(file);
			return tris;
		}

		std::vector<XMFLOAT3> verts(vertexCount);
		if (vertexCount > 0)
		{
			if (fread(verts.data(), sizeof(XMFLOAT3), vertexCount, file) != vertexCount)
			{
				fclose(file);
				return tris;
			}
		}
		std::vector<unsigned int> indices(static_cast<size_t>(triangleCount) * 3);
		if (triangleCount > 0)
		{
			if (fread(indices.data(), sizeof(unsigned int), indices.size(), file) != indices.size())
			{
				fclose(file);
				return tris;
			}
		}
		fclose(file);

		tris.reserve(triangleCount);
		for (unsigned int i = 0; i < triangleCount; ++i)
		{
			const unsigned int ia = indices[i * 3 + 0];
			const unsigned int ib = indices[i * 3 + 1];
			const unsigned int ic = indices[i * 3 + 2];
			if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount)
			{
				continue;
			}
			CollisionTriangle tri;
			tri.a = {
				verts[ia].x * ASSIMP_GLOBAL_SCALE,
				verts[ia].y * ASSIMP_GLOBAL_SCALE,
				-verts[ia].z * ASSIMP_GLOBAL_SCALE
			};
			tri.b = {
				verts[ic].x * ASSIMP_GLOBAL_SCALE,
				verts[ic].y * ASSIMP_GLOBAL_SCALE,
				-verts[ic].z * ASSIMP_GLOBAL_SCALE
			};
			tri.c = {
				verts[ib].x * ASSIMP_GLOBAL_SCALE,
				verts[ib].y * ASSIMP_GLOBAL_SCALE,
				-verts[ib].z * ASSIMP_GLOBAL_SCALE
			};
			tris.push_back(tri);
		}
		return tris;
	}

	std::vector<CollisionTriangle> ImportLocalTris(const char* path)
	{
		std::vector<CollisionTriangle> tris;
		if (!FileExists(path))
		{
			return tris;
		}

		std::vector<XMFLOAT3> vertices;
		if (!GlbModel::ImportCollisionTriangles(path, &vertices) ||
			vertices.size() < 3)
		{
			return tris;
		}

		tris.reserve(vertices.size() / 3);
		for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
		{
			CollisionTriangle tri;
			tri.a = vertices[i + 0];
			tri.b = vertices[i + 1];
			tri.c = vertices[i + 2];
			tris.push_back(tri);
		}
		return tris;
	}

	void BuildFlatGrid(CollisionMesh* mesh, int maxCellsPerTri)
	{
		if (!mesh)
		{
			return;
		}
		mesh->useGrid = false;
		mesh->cellStart.clear();
		mesh->cellItems.clear();
		mesh->largeTris.clear();
		mesh->triMin.clear();
		mesh->triMax.clear();
		mesh->visitStamp.clear();
		mesh->visitGen = 1;
		mesh->gridW = 0;
		mesh->gridH = 0;
		mesh->cellSize = GRID_CELL_SIZE;

		const size_t count = mesh->worldTris.size();
		if (count < GRID_MIN_TRIANGLES)
		{
			return;
		}

		mesh->triMin.resize(count);
		mesh->triMax.resize(count);
		for (size_t i = 0; i < count; ++i)
		{
			XMFLOAT3 tMin = { FLT_MAX, FLT_MAX, FLT_MAX };
			XMFLOAT3 tMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			ExpandBounds(&tMin, &tMax, mesh->worldTris[i]);
			mesh->triMin[i] = tMin;
			mesh->triMax[i] = tMax;
		}

		float cellSize = GRID_CELL_SIZE;
		int originX = 0;
		int originZ = 0;
		int gridW = 1;
		int gridH = 1;
		for (;;)
		{
			originX = GridCell(mesh->boundsMin.x, cellSize);
			originZ = GridCell(mesh->boundsMin.z, cellSize);
			const int maxX = GridCell(mesh->boundsMax.x, cellSize);
			const int maxZ = GridCell(mesh->boundsMax.z, cellSize);
			gridW = maxX - originX + 1;
			gridH = maxZ - originZ + 1;
			if (gridW < 1) gridW = 1;
			if (gridH < 1) gridH = 1;
			const long long cells = static_cast<long long>(gridW) * static_cast<long long>(gridH);
			if (cells <= GRID_MAX_CELLS)
			{
				break;
			}
			cellSize *= 2.0f;
		}

		const int cellCount = gridW * gridH;
		std::vector<int> counts(static_cast<size_t>(cellCount), 0);
		mesh->largeTris.clear();
		g_ProgressStage = COLLISION_STAGE_GRID;
		g_ProgressDone = 0;
		g_ProgressTotal = count;

		auto cellIndex = [&](int cx, int cz) -> int
		{
			const int x = cx - originX;
			const int z = cz - originZ;
			if (x < 0 || z < 0 || x >= gridW || z >= gridH)
			{
				return -1;
			}
			return z * gridW + x;
		};

		for (size_t i = 0; i < count; ++i)
		{
			const int x0 = GridCell(mesh->triMin[i].x, cellSize);
			const int x1 = GridCell(mesh->triMax[i].x, cellSize);
			const int z0 = GridCell(mesh->triMin[i].z, cellSize);
			const int z1 = GridCell(mesh->triMax[i].z, cellSize);
			const int spanX = x1 - x0 + 1;
			const int spanZ = z1 - z0 + 1;
			if (spanX > 0 && spanZ > 0
				&& spanX <= maxCellsPerTri
				&& spanZ <= maxCellsPerTri
				&& spanX * spanZ <= maxCellsPerTri)
			{
				for (int cx = x0; cx <= x1; ++cx)
				{
					for (int cz = z0; cz <= z1; ++cz)
					{
						const int idx = cellIndex(cx, cz);
						if (idx >= 0)
						{
							counts[static_cast<size_t>(idx)] += 1;
						}
					}
				}
			}
			else
			{
				mesh->largeTris.push_back(static_cast<int>(i));
			}
			g_ProgressDone = i + 1;
		}

		mesh->cellStart.resize(static_cast<size_t>(cellCount) + 1);
		mesh->cellStart[0] = 0;
		for (int i = 0; i < cellCount; ++i)
		{
			mesh->cellStart[static_cast<size_t>(i) + 1] =
				mesh->cellStart[static_cast<size_t>(i)] + counts[static_cast<size_t>(i)];
		}
		mesh->cellItems.assign(static_cast<size_t>(mesh->cellStart.back()), 0);
		std::vector<int> cursor = mesh->cellStart;

		for (size_t i = 0; i < count; ++i)
		{
			const int x0 = GridCell(mesh->triMin[i].x, cellSize);
			const int x1 = GridCell(mesh->triMax[i].x, cellSize);
			const int z0 = GridCell(mesh->triMin[i].z, cellSize);
			const int z1 = GridCell(mesh->triMax[i].z, cellSize);
			const int spanX = x1 - x0 + 1;
			const int spanZ = z1 - z0 + 1;
			if (!(spanX > 0 && spanZ > 0
				&& spanX <= maxCellsPerTri
				&& spanZ <= maxCellsPerTri
				&& spanX * spanZ <= maxCellsPerTri))
			{
				continue;
			}
			for (int cx = x0; cx <= x1; ++cx)
			{
				for (int cz = z0; cz <= z1; ++cz)
				{
					const int idx = cellIndex(cx, cz);
					if (idx < 0)
					{
						continue;
					}
					mesh->cellItems[static_cast<size_t>(cursor[static_cast<size_t>(idx)])] = static_cast<int>(i);
					cursor[static_cast<size_t>(idx)] += 1;
				}
			}
		}

		mesh->visitStamp.assign(count, 0);
		mesh->visitGen = 1;
		mesh->cellSize = cellSize;
		mesh->gridOriginX = originX;
		mesh->gridOriginZ = originZ;
		mesh->gridW = gridW;
		mesh->gridH = gridH;
		mesh->useGrid = true;
	}

	CollisionMesh BakeMesh(
		std::vector<CollisionTriangle> localTris,
		const XMMATRIX& world,
		bool filterLattice)
	{
		CollisionMesh mesh = {};
		mesh.localTris = std::move(localTris);
		mesh.world = world;
		mesh.yBias = 0.0f;
		mesh.filterLattice = filterLattice;
		mesh.visitGen = 1;
		mesh.useGrid = false;
		mesh.boundsMin = { 0.0f, 0.0f, 0.0f };
		mesh.boundsMax = { 0.0f, 0.0f, 0.0f };
		if (mesh.localTris.empty())
		{
			return mesh;
		}

		g_ProgressStage = COLLISION_STAGE_TRANSFORM;
		g_ProgressDone = 0;
		g_ProgressTotal = mesh.localTris.size();

		mesh.worldTris.reserve(mesh.localTris.size());
		XMFLOAT3 bMin = { FLT_MAX, FLT_MAX, FLT_MAX };
		XMFLOAT3 bMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (size_t i = 0; i < mesh.localTris.size(); ++i)
		{
			CollisionTriangle worldTri;
			worldTri.a = TransformPoint(mesh.localTris[i].a, world);
			worldTri.b = TransformPoint(mesh.localTris[i].b, world);
			worldTri.c = TransformPoint(mesh.localTris[i].c, world);
			if (filterLattice && !KeepRingCollisionTriangle(worldTri))
			{
				g_ProgressDone = i + 1;
				continue;
			}
			ExpandBounds(&bMin, &bMax, worldTri);
			mesh.worldTris.push_back(worldTri);
			g_ProgressDone = i + 1;
		}

		if (mesh.worldTris.empty())
		{
			return mesh;
		}
		mesh.boundsMin = bMin;
		mesh.boundsMax = bMax;
		BuildFlatGrid(
			&mesh,
			filterLattice
				? GRID_MAX_CELLS_PER_TRI_RING
				: GRID_MAX_CELLS_PER_TRI_BUILDING);
		if (!filterLattice)
		{
			std::vector<CollisionTriangle>().swap(mesh.localTris);
		}
		return mesh;
	}

	std::vector<CollisionTriangle> LoadLocalTris(
		const BakeJob& job,
		bool* loadedFromBin)
	{
		if (loadedFromBin)
		{
			*loadedFromBin = false;
		}
		g_ProgressStage = COLLISION_STAGE_LOAD;
		g_ProgressDone = 0;
		g_ProgressTotal = 1;
		std::vector<CollisionTriangle> tris;
		if (!job.binPath.empty() && FileExists(job.binPath.c_str()))
		{
			tris = LoadLocalTrisBin(job.binPath.c_str());
			if (!tris.empty() && loadedFromBin)
			{
				*loadedFromBin = true;
			}
		}
		if (tris.empty() && !job.glbPath.empty())
		{
			tris = ImportLocalTris(job.glbPath.c_str());
		}
		g_ProgressDone = 1;
		return tris;
	}

	void WorkerMain(void)
	{
		for (;;)
		{
			BakeJob job;
			{
				std::lock_guard<std::mutex> lock(g_Mutex);
				if (g_Jobs.empty())
				{
					g_WorkerActive = false;
					g_WorkerJoinNeeded = true;
					g_ProgressStage = COLLISION_STAGE_IDLE;
					return;
				}
				job = std::move(g_Jobs.front());
				g_Jobs.pop_front();
			}

			BakeResult result;
			bool loadedFromBin = false;
			std::vector<CollisionTriangle> tris =
				LoadLocalTris(job, &loadedFromBin);
			if (tris.empty())
			{
				result.failed = true;
			}
			else
			{
				result.mesh = BakeMesh(
					std::move(tris),
					job.world,
					job.filterLattice);
				result.mesh.sourceName = PathStem(job.glbPath);
				result.mesh.loadedFromBin = loadedFromBin;
				result.failed = result.mesh.worldTris.empty() && result.mesh.localTris.empty();
			}
			{
				std::lock_guard<std::mutex> lock(g_Mutex);
				g_Ready.push_back(std::move(result));
			}
		}
	}

	void JoinWorkerIfNeeded(void)
	{
		if (g_WorkerJoinNeeded && g_Worker.joinable())
		{
			g_Worker.join();
			g_WorkerJoinNeeded = false;
		}
	}

	void EnsureWorker(void)
	{
		JoinWorkerIfNeeded();
		if (g_WorkerActive)
		{
			return;
		}
		if (g_Worker.joinable())
		{
			g_Worker.join();
		}
		g_WorkerActive = true;
		g_WorkerJoinNeeded = false;
		g_Worker = std::thread(WorkerMain);
	}

	bool AabbOverlap(const XMFLOAT3& aMin, const XMFLOAT3& aMax, const XMFLOAT3& bMin, const XMFLOAT3& bMax)
	{
		return aMin.x <= bMax.x && aMax.x >= bMin.x
			&& aMin.y <= bMax.y && aMax.y >= bMin.y
			&& aMin.z <= bMax.z && aMax.z >= bMin.z;
	}

	bool IsHighDetailGate(const CollisionMesh& mesh)
	{
		return mesh.sourceName == "expo_pavilion_east_gate" ||
			mesh.sourceName == "expo_pavilion_west_gate";
	}

	bool IsLod2Mesh(const CollisionMesh& mesh)
	{
		return mesh.sourceName.find("expo_tile_lod2_") == 0;
	}

	bool IsInsideHighDetailGate(
		const XMFLOAT3& aabbMin,
		const XMFLOAT3& aabbMax)
	{
		for (const CollisionMesh& gate : g_Meshes)
		{
			if (!IsHighDetailGate(gate) || gate.worldTris.empty())
			{
				continue;
			}
			if (AabbOverlap(
				aabbMin,
				aabbMax,
				XMFLOAT3(
					gate.boundsMin.x,
					gate.boundsMin.y + gate.yBias,
					gate.boundsMin.z),
				XMFLOAT3(
					gate.boundsMax.x,
					gate.boundsMax.y + gate.yBias,
					gate.boundsMax.z)))
			{
				return true;
			}
		}
		return false;
	}

	XMFLOAT3 ClosestPointOnTriangle(const XMFLOAT3& p, const CollisionTriangle& tri)
	{
		const XMVECTOR a = XMLoadFloat3(&tri.a);
		const XMVECTOR b = XMLoadFloat3(&tri.b);
		const XMVECTOR c = XMLoadFloat3(&tri.c);
		const XMVECTOR pVec = XMLoadFloat3(&p);

		const XMVECTOR ab = XMVectorSubtract(b, a);
		const XMVECTOR ac = XMVectorSubtract(c, a);
		const XMVECTOR ap = XMVectorSubtract(pVec, a);

		const float d1 = XMVectorGetX(XMVector3Dot(ab, ap));
		const float d2 = XMVectorGetX(XMVector3Dot(ac, ap));
		if (d1 <= 0.0f && d2 <= 0.0f)
		{
			XMFLOAT3 out;
			XMStoreFloat3(&out, a);
			return out;
		}

		const XMVECTOR bp = XMVectorSubtract(pVec, b);
		const float d3 = XMVectorGetX(XMVector3Dot(ab, bp));
		const float d4 = XMVectorGetX(XMVector3Dot(ac, bp));
		if (d3 >= 0.0f && d4 <= d3)
		{
			XMFLOAT3 out;
			XMStoreFloat3(&out, b);
			return out;
		}

		const float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
		{
			const float v = d1 / (d1 - d3);
			XMFLOAT3 out;
			XMStoreFloat3(&out, XMVectorAdd(a, XMVectorScale(ab, v)));
			return out;
		}

		const XMVECTOR cp = XMVectorSubtract(pVec, c);
		const float d5 = XMVectorGetX(XMVector3Dot(ab, cp));
		const float d6 = XMVectorGetX(XMVector3Dot(ac, cp));
		if (d6 >= 0.0f && d5 <= d6)
		{
			XMFLOAT3 out;
			XMStoreFloat3(&out, c);
			return out;
		}

		const float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
		{
			const float w = d2 / (d2 - d6);
			XMFLOAT3 out;
			XMStoreFloat3(&out, XMVectorAdd(a, XMVectorScale(ac, w)));
			return out;
		}

		const float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
		{
			const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			XMFLOAT3 out;
			XMStoreFloat3(&out, XMVectorAdd(b, XMVectorScale(XMVectorSubtract(c, b), w)));
			return out;
		}

		const float denom = 1.0f / (va + vb + vc);
		const float v = vb * denom;
		const float w = vc * denom;
		XMFLOAT3 out;
		XMStoreFloat3(&out, XMVectorAdd(a, XMVectorAdd(XMVectorScale(ab, v), XMVectorScale(ac, w))));
		return out;
	}

	bool ResolveTriangle(
		CollisionMesh& mesh,
		int triIndex,
		XMFLOAT3* center,
		const XMFLOAT3& half,
		const XMFLOAT3& aabbMin,
		const XMFLOAT3& aabbMax,
		bool* grounded)
	{
		if (triIndex < 0 || triIndex >= static_cast<int>(mesh.worldTris.size()))
		{
			return false;
		}
		if (mesh.useGrid
			&& triIndex < static_cast<int>(mesh.triMin.size())
			&& !AabbOverlap(
				aabbMin,
				aabbMax,
				XMFLOAT3(mesh.triMin[triIndex].x, mesh.triMin[triIndex].y + mesh.yBias, mesh.triMin[triIndex].z),
				XMFLOAT3(mesh.triMax[triIndex].x, mesh.triMax[triIndex].y + mesh.yBias, mesh.triMax[triIndex].z)))
		{
			return false;
		}

		CollisionTriangle tri = mesh.worldTris[triIndex];
		tri.a.y += mesh.yBias;
		tri.b.y += mesh.yBias;
		tri.c.y += mesh.yBias;
		const XMFLOAT3 closest = ClosestPointOnTriangle(*center, tri);
		const float dx = center->x - closest.x;
		const float dy = center->y - closest.y;
		const float dz = center->z - closest.z;
		const float nx = (dx < 0.0f) ? -dx : dx;
		const float ny = (dy < 0.0f) ? -dy : dy;
		const float nz = (dz < 0.0f) ? -dz : dz;
		if (nx > half.x || ny > half.y || nz > half.z)
		{
			return false;
		}

		const float px = half.x - nx;
		const float py = half.y - ny;
		const float pz = half.z - nz;
		if (px <= py && px <= pz)
		{
			center->x += (dx < 0.0f) ? -px : px;
		}
		else if (py <= pz)
		{
			center->y += (dy < 0.0f) ? -py : py;
			if (dy > 0.0f && grounded)
			{
				*grounded = true;
			}
		}
		else
		{
			center->z += (dz < 0.0f) ? -pz : pz;
		}
		return true;
	}

	bool ResolveAgainstTriangles(XMFLOAT3* center, const XMFLOAT3& half, bool* grounded)
	{
		if (!center) return false;
		bool hit = false;
		const XMFLOAT3 aabbMin = {
			center->x - half.x,
			center->y - half.y,
			center->z - half.z
		};
		const XMFLOAT3 aabbMax = {
			center->x + half.x,
			center->y + half.y,
			center->z + half.z
		};
		const bool insideHighDetailGate =
			IsInsideHighDetailGate(aabbMin, aabbMax);

		for (CollisionMesh& mesh : g_Meshes)
		{
			if (mesh.worldTris.empty())
			{
				continue;
			}
			// ゲートは高精細メッシュを正とする。
			// 遠景補完LOD2にも同じゲート形状が残るため、
			// ゲートAABB内だけLOD2側を無効にして二重判定を防ぐ。
			if (insideHighDetailGate && IsLod2Mesh(mesh))
			{
				continue;
			}
			if (!AabbOverlap(
				aabbMin,
				aabbMax,
				XMFLOAT3(mesh.boundsMin.x, mesh.boundsMin.y + mesh.yBias, mesh.boundsMin.z),
				XMFLOAT3(mesh.boundsMax.x, mesh.boundsMax.y + mesh.yBias, mesh.boundsMax.z)))
			{
				continue;
			}

			if (mesh.useGrid)
			{
				mesh.visitGen += 1;
				if (mesh.visitGen == 0)
				{
					if (!mesh.visitStamp.empty())
					{
						memset(mesh.visitStamp.data(), 0, mesh.visitStamp.size() * sizeof(unsigned int));
					}
					mesh.visitGen = 1;
				}

				auto consider = [&](int triIndex)
				{
					if (triIndex < 0 || triIndex >= static_cast<int>(mesh.visitStamp.size()))
					{
						return;
					}
					if (mesh.visitStamp[triIndex] == mesh.visitGen)
					{
						return;
					}
					mesh.visitStamp[triIndex] = mesh.visitGen;
					if (ResolveTriangle(mesh, triIndex, center, half, aabbMin, aabbMax, grounded))
					{
						hit = true;
						RecordHit(mesh.sourceName);
					}
				};

				for (int triIndex : mesh.largeTris)
				{
					consider(triIndex);
				}

				if (mesh.gridW > 0 && mesh.gridH > 0 && mesh.cellSize > 0.0f)
				{
					int x0 = GridCell(aabbMin.x, mesh.cellSize) - mesh.gridOriginX;
					int x1 = GridCell(aabbMax.x, mesh.cellSize) - mesh.gridOriginX;
					int z0 = GridCell(aabbMin.z, mesh.cellSize) - mesh.gridOriginZ;
					int z1 = GridCell(aabbMax.z, mesh.cellSize) - mesh.gridOriginZ;
					if (x0 < 0) x0 = 0;
					if (z0 < 0) z0 = 0;
					if (x1 >= mesh.gridW) x1 = mesh.gridW - 1;
					if (z1 >= mesh.gridH) z1 = mesh.gridH - 1;
					for (int gx = x0; gx <= x1; ++gx)
					{
						for (int gz = z0; gz <= z1; ++gz)
						{
							const int cell = gz * mesh.gridW + gx;
							const int start = mesh.cellStart[static_cast<size_t>(cell)];
							const int end = mesh.cellStart[static_cast<size_t>(cell) + 1];
							for (int it = start; it < end; ++it)
							{
								consider(mesh.cellItems[static_cast<size_t>(it)]);
							}
						}
					}
				}
			}
			else
			{
				for (int i = 0; i < static_cast<int>(mesh.worldTris.size()); ++i)
				{
					if (ResolveTriangle(mesh, i, center, half, aabbMin, aabbMax, grounded))
					{
						hit = true;
						RecordHit(mesh.sourceName);
					}
				}
			}
		}
		return hit;
	}

	int SubstepCount(float distance, float halfAxis)
	{
		float maxStep = halfAxis * 0.5f;
		if (maxStep < 0.01f)
		{
			maxStep = 0.01f;
		}
		int steps = 1;
		if (distance > maxStep)
		{
			steps = static_cast<int>(ceilf(distance / maxStep));
		}
		if (steps > 32)
		{
			steps = 32;
		}
		return steps;
	}

	void MoveAxisSubsteps(
		XMFLOAT3* center,
		const XMFLOAT3& half,
		XMFLOAT3 delta,
		bool* grounded)
	{
		const float ax = (delta.x < 0.0f) ? -delta.x : delta.x;
		const float ay = (delta.y < 0.0f) ? -delta.y : delta.y;
		const float az = (delta.z < 0.0f) ? -delta.z : delta.z;
		float limitHalf = half.x;
		if (half.y < limitHalf) limitHalf = half.y;
		if (half.z < limitHalf) limitHalf = half.z;
		const int steps = SubstepCount(ax + ay + az, limitHalf);
		const float inv = 1.0f / static_cast<float>(steps);
		const XMFLOAT3 step = { delta.x * inv, delta.y * inv, delta.z * inv };
		for (int i = 0; i < steps; ++i)
		{
			center->x += step.x;
			center->y += step.y;
			center->z += step.z;
			if (ResolveAgainstTriangles(center, half, grounded))
			{
				ResolveAgainstTriangles(center, half, grounded);
			}
		}
	}
}

bool Collision_StartAdd(
	const char* glbPath,
	const XMMATRIX& world,
	bool filterLattice)
{
	if (!glbPath)
	{
		return false;
	}
	BakeJob job;
	job.glbPath = glbPath;
	job.binPath = DeriveBinPath(glbPath);
	job.world = world;
	job.filterLattice = filterLattice;
	if (!FileExists(job.binPath.c_str()) && !FileExists(job.glbPath.c_str()))
	{
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_Mutex);
		g_Jobs.push_back(std::move(job));
	}
	EnsureWorker();
	return true;
}

CollisionPumpResult Collision_Pump(int* outMeshId)
{
	if (outMeshId)
	{
		*outMeshId = -1;
	}

	JoinWorkerIfNeeded();

	BakeResult ready;
	bool hasReady = false;
	{
		std::lock_guard<std::mutex> lock(g_Mutex);
		if (!g_Ready.empty())
		{
			ready = std::move(g_Ready.front());
			g_Ready.pop_front();
			hasReady = true;
		}
	}

	if (hasReady)
	{
		if (ready.failed)
		{
			return COLLISION_PUMP_FAILED;
		}
		g_Meshes.push_back(std::move(ready.mesh));
		if (outMeshId)
		{
			*outMeshId = static_cast<int>(g_Meshes.size()) - 1;
		}
		return COLLISION_PUMP_DONE;
	}

	if (g_WorkerActive)
	{
		return COLLISION_PUMP_BUSY;
	}

	bool hasJobs = false;
	{
		std::lock_guard<std::mutex> lock(g_Mutex);
		hasJobs = !g_Jobs.empty();
	}
	if (hasJobs)
	{
		EnsureWorker();
		return COLLISION_PUMP_BUSY;
	}
	return COLLISION_PUMP_IDLE;
}

void Collision_GetPumpProgress(size_t* done, size_t* total, int* stage)
{
	if (done)
	{
		*done = g_ProgressDone.load();
	}
	if (total)
	{
		*total = g_ProgressTotal.load();
	}
	if (stage)
	{
		*stage = g_ProgressStage.load();
	}
}

void Collision_GetSourceStatus(char* out, size_t outSize)
{
	if (!out || outSize == 0)
	{
		return;
	}
	strcpy_s(out, outSize, "衝突 AABB");
	if (g_Meshes.empty())
	{
		strcat_s(out, outSize, " なし");
		return;
	}
	for (const CollisionMesh& mesh : g_Meshes)
	{
		const std::string displayName = CollisionDisplayName(mesh.sourceName);
		char item[96] = {};
		sprintf_s(
			item,
			" %s:%s",
			displayName.c_str(),
			mesh.loadedFromBin ? "BIN" : "GLB");
		if (strlen(out) + strlen(item) + 1 >= outSize)
		{
			break;
		}
		strcat_s(out, outSize, item);
	}
}

void Collision_GetLastHitStatus(char* out, size_t outSize)
{
	if (!out || outSize == 0)
	{
		return;
	}
	if (g_FrameHitNames.empty())
	{
		strcpy_s(out, outSize, "衝突なし");
		return;
	}
	strcpy_s(out, outSize, "衝突");
	for (const std::string& sourceName : g_FrameHitNames)
	{
		const std::string displayName = CollisionDisplayName(sourceName);
		char item[128] = {};
		sprintf_s(item, " %s", displayName.c_str());
		if (strlen(out) + strlen(item) + 1 >= outSize)
		{
			break;
		}
		strcat_s(out, outSize, item);
	}
}

void Collision_SetWorld(int meshId, const XMMATRIX& world)
{
	if (meshId < 0 || meshId >= static_cast<int>(g_Meshes.size()))
	{
		return;
	}
	CollisionMesh& mesh = g_Meshes[meshId];
	if (!mesh.worldTris.empty())
	{
		XMFLOAT4X4 fa;
		XMFLOAT4X4 fb;
		XMStoreFloat4x4(&fa, mesh.world);
		XMStoreFloat4x4(&fb, world);
		bool sameLinear = true;
		for (int r = 0; r < 3 && sameLinear; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				if (fabsf(fa.m[r][c] - fb.m[r][c]) > 1.0e-4f)
				{
					sameLinear = false;
					break;
				}
			}
		}
		if (sameLinear
			&& fabsf(fa._41 - fb._41) <= 1.0e-4f
			&& fabsf(fa._43 - fb._43) <= 1.0e-4f)
		{
			mesh.yBias += fb._42 - fa._42;
			mesh.world = world;
			return;
		}
	}
	if (mesh.localTris.empty())
	{
		return;
	}
	const std::string sourceName = mesh.sourceName;
	const bool loadedFromBin = mesh.loadedFromBin;
	mesh = BakeMesh(mesh.localTris, world, mesh.filterLattice);
	mesh.sourceName = sourceName;
	mesh.loadedFromBin = loadedFromBin;
}

void Collision_Clear(void)
{
	if (g_Worker.joinable())
	{
		{
			std::lock_guard<std::mutex> lock(g_Mutex);
			g_Jobs.clear();
		}
		g_Worker.join();
	}
	g_WorkerActive = false;
	g_WorkerJoinNeeded = false;
	{
		std::lock_guard<std::mutex> lock(g_Mutex);
		g_Jobs.clear();
		g_Ready.clear();
	}
	g_Meshes.clear();
	g_FrameHitNames.clear();
	g_ProgressDone = 0;
	g_ProgressTotal = 0;
	g_ProgressStage = COLLISION_STAGE_IDLE;
}

bool Collision_GetBounds(int meshId, XMFLOAT3* bmin, XMFLOAT3* bmax)
{
	if (meshId < 0 || meshId >= static_cast<int>(g_Meshes.size()) || !bmin || !bmax)
	{
		return false;
	}
	const CollisionMesh& mesh = g_Meshes[meshId];
	if (mesh.worldTris.empty())
	{
		return false;
	}
	*bmin = mesh.boundsMin;
	*bmax = mesh.boundsMax;
	bmin->y += mesh.yBias;
	bmax->y += mesh.yBias;
	return true;
}

bool Collision_MoveAABB(
	XMFLOAT3 center,
	XMFLOAT3 halfExtents,
	XMFLOAT3 delta,
	XMFLOAT3* outCenter,
	bool* grounded)
{
	if (!outCenter)
	{
		return false;
	}

	g_FrameHitNames.clear();
	bool onGround = false;
	MoveAxisSubsteps(&center, halfExtents, XMFLOAT3(delta.x, 0.0f, delta.z), nullptr);
	MoveAxisSubsteps(&center, halfExtents, XMFLOAT3(0.0f, delta.y, 0.0f), &onGround);

	if (grounded)
	{
		*grounded = onGround;
	}
	*outCenter = center;
	return true;
}
