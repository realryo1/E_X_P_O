/* eslint-disable no-console */
/**
 * meshoptimizerでGLBの三角形数を削減する。
 *
 * 使用方法:
 *   node tool/simplify_glb.cjs input.glb output.glb --ratio 0.4
 *   node tool/simplify_glb.cjs --config tool/expo_simplify.json
 *
 * BlenderのDecimateと同じアルゴリズムではないため、ratioは島ごとの目標値である。
 * テクスチャ、マテリアル、ノード階層はgltf-transformで保持する。
 * 網目は穴の縁が多く、面を減らすと板になる。その島は削減せず、
 * 位置とUVが同じ頂点だけ結合する。
 */

const fs = require("fs");
const path = require("path");
const { Document, NodeIO } = require("@gltf-transform/core");
const { compactPrimitive } = require("@gltf-transform/functions");
const { MeshoptSimplifier } = require("meshoptimizer");

function valueAfter(args, name) {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
}

function parseNumber(value, name) {
  const number = Number(value);
  if (!Number.isFinite(number) || number <= 0 || number > 1) {
    throw new Error(`${name} は0より大きく1以下の数値で指定してください: ${value}`);
  }
  return number;
}

function parsePositive(value, name, fallback) {
  if (value === undefined) {
    return fallback;
  }
  const number = Number(value);
  if (!Number.isFinite(number) || number < 0) {
    throw new Error(`${name} は0以上の数値で指定してください: ${value}`);
  }
  return number;
}

function parseOne(args) {
  if (args[0] === "--config") {
    if (!args[1]) {
      throw new Error("--config にJSONファイルを指定してください");
    }
    const config = JSON.parse(fs.readFileSync(args[1], "utf8"));
    if (!Array.isArray(config.simplifications) || config.simplifications.length === 0) {
      throw new Error("設定ファイルのsimplificationsが空です");
    }
    return config.simplifications.map((entry) => ({
      input: entry.input,
      output: entry.output || entry.input,
      ratio: parseNumber(entry.ratio, "ratio"),
      error: parsePositive(entry.error, "error", 0.01),
      uvWeight: parsePositive(entry.uvWeight, "uvWeight", 1),
      uvSeam: parsePositive(entry.uvSeam, "uvSeam", 0.01),
    }));
  }

  if (!args[0] || !args[1]) {
    throw new Error(
      "使用方法: node simplify_glb.cjs input.glb output.glb --ratio 0.4 [--error 0.01] [--uv-weight 1] [--uv-seam 0.01]",
    );
  }
  return [{
    input: args[0],
    output: args[1],
    ratio: parseNumber(valueAfter(args, "--ratio") || "0.4", "--ratio"),
    error: parsePositive(valueAfter(args, "--error"), "--error", 0.01),
    uvWeight: parsePositive(valueAfter(args, "--uv-weight"), "--uv-weight", 1),
    uvSeam: parsePositive(valueAfter(args, "--uv-seam"), "--uv-seam", 0.01),
  }];
}

function floatAttribute(accessor) {
  const count = accessor.getCount();
  const size = accessor.getElementSize();
  const array = accessor.getArray();
  if (array instanceof Float32Array && !accessor.getNormalized()) {
    return array;
  }
  const out = new Float32Array(count * size);
  const element = [];
  for (let vertex = 0; vertex < count; ++vertex) {
    accessor.getElement(vertex, element);
    out.set(element, vertex * size);
  }
  return out;
}

function positionKey(positionArray, vertex) {
  const offset = vertex * 3;
  return `${positionArray[offset]},${positionArray[offset + 1]},${positionArray[offset + 2]}`;
}

function uvDistance(uvArray, left, right) {
  const du = uvArray[left * 2] - uvArray[right * 2];
  const dv = uvArray[left * 2 + 1] - uvArray[right * 2 + 1];
  return Math.hypot(du, dv);
}

function findRoot(parent, index) {
  let root = index;
  while (parent[root] !== root) {
    root = parent[root];
  }
  let cursor = index;
  while (parent[cursor] !== root) {
    const next = parent[cursor];
    parent[cursor] = root;
    cursor = next;
  }
  return root;
}

function shareUsedPositions(positionArray, uvArray, usedIndices) {
  const uniquePositions = [];
  const uniqueUvs = [];
  const representatives = [];
  const positionMap = new Map();
  const vertexRemap = new Map();
  for (let i = 0; i < usedIndices.length; ++i) {
    const vertex = usedIndices[i];
    if (vertexRemap.has(vertex)) {
      continue;
    }
    const key = positionKey(positionArray, vertex);
    let unique = positionMap.get(key);
    if (unique === undefined) {
      unique = representatives.length;
      positionMap.set(key, unique);
      representatives.push(vertex);
      const offset = vertex * 3;
      uniquePositions.push(
        positionArray[offset],
        positionArray[offset + 1],
        positionArray[offset + 2],
      );
      if (uvArray) {
        uniqueUvs.push(uvArray[vertex * 2], uvArray[vertex * 2 + 1]);
      }
    }
    vertexRemap.set(vertex, unique);
  }
  return { uniquePositions, uniqueUvs, representatives, vertexRemap };
}

function borderVertexFraction(indices, uniqueCount) {
  const edgeCounts = new Map();
  for (let i = 0; i < indices.length; i += 3) {
    for (let edge = 0; edge < 3; ++edge) {
      const a = indices[i + edge];
      const b = indices[i + ((edge + 1) % 3)];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      edgeCounts.set(key, (edgeCounts.get(key) || 0) + 1);
    }
  }
  const border = new Set();
  for (const [key, count] of edgeCounts) {
    if (count !== 1) {
      continue;
    }
    const parts = key.split(",");
    border.add(Number(parts[0]));
    border.add(Number(parts[1]));
  }
  return uniqueCount > 0 ? border.size / uniqueCount : 1;
}

function weldByPositionUv(indices, positionArray, uvArray) {
  const keyMap = new Map();
  const vertexToRep = new Map();
  const welded = new Uint32Array(indices.length);
  for (let i = 0; i < indices.length; ++i) {
    const vertex = indices[i];
    let representative = vertexToRep.get(vertex);
    if (representative === undefined) {
      let key = positionKey(positionArray, vertex);
      if (uvArray) {
        key +=
          `,${Math.round(uvArray[vertex * 2] * 1e5)},` +
          `${Math.round(uvArray[vertex * 2 + 1] * 1e5)}`;
      }
      representative = keyMap.get(key);
      if (representative === undefined) {
        representative = vertex;
        keyMap.set(key, representative);
      }
      vertexToRep.set(vertex, representative);
    }
    welded[i] = representative;
  }
  return welded;
}

function simplifyIndexGroup(sourceIndices, positionArray, uvArray, entry) {
  if (sourceIndices.length < 24) {
    return sourceIndices;
  }
  const shared = shareUsedPositions(positionArray, uvArray, sourceIndices);
  const remappedIndices = Uint32Array.from(
    sourceIndices,
    (index) => shared.vertexRemap.get(index),
  );
  // ほぼ全頂点が穴の縁だと、削減すると網目が板になる。
  if (borderVertexFraction(remappedIndices, shared.representatives.length) >= 0.9) {
    return sourceIndices;
  }
  const targetIndexCount = Math.max(
    12,
    Math.floor((entry.ratio * sourceIndices.length) / 3) * 3,
  );
  const flags = ["LockBorder"];
  let simplifiedShared;
  if (uvArray) {
    [simplifiedShared] = MeshoptSimplifier.simplifyWithAttributes(
      remappedIndices,
      Float32Array.from(shared.uniquePositions),
      3,
      Float32Array.from(shared.uniqueUvs),
      2,
      [entry.uvWeight, entry.uvWeight],
      null,
      targetIndexCount,
      entry.error,
      flags,
    );
  } else {
    [simplifiedShared] = MeshoptSimplifier.simplify(
      remappedIndices,
      Float32Array.from(shared.uniquePositions),
      3,
      targetIndexCount,
      entry.error,
      flags,
    );
  }
  if (simplifiedShared.length < 3) {
    return sourceIndices;
  }
  return Uint32Array.from(
    simplifiedShared,
    (index) => shared.representatives[index],
  );
}

function collectUvIslands(sourceIndices, positionArray, uvArray, uvSeam) {
  const triangleCount = sourceIndices.length / 3;
  const parent = new Int32Array(triangleCount);
  for (let triangle = 0; triangle < triangleCount; ++triangle) {
    parent[triangle] = triangle;
  }
  const edges = new Map();
  for (let triangle = 0; triangle < triangleCount; ++triangle) {
    const verts = [
      sourceIndices[triangle * 3],
      sourceIndices[triangle * 3 + 1],
      sourceIndices[triangle * 3 + 2],
    ];
    for (let edge = 0; edge < 3; ++edge) {
      const a = verts[edge];
      const b = verts[(edge + 1) % 3];
      const ka = positionKey(positionArray, a);
      const kb = positionKey(positionArray, b);
      const key = ka < kb ? `${ka}|${kb}` : `${kb}|${ka}`;
      const list = edges.get(key);
      const current = { triangle, a, b };
      if (!list) {
        edges.set(key, [current]);
        continue;
      }
      for (let i = 0; i < list.length; ++i) {
        const previous = list[i];
        const aligned = ka === positionKey(positionArray, previous.a);
        const match = aligned
          ? uvDistance(uvArray, a, previous.a) < uvSeam
            && uvDistance(uvArray, b, previous.b) < uvSeam
          : uvDistance(uvArray, a, previous.b) < uvSeam
            && uvDistance(uvArray, b, previous.a) < uvSeam;
        if (!match) {
          continue;
        }
        const left = findRoot(parent, triangle);
        const right = findRoot(parent, previous.triangle);
        if (left !== right) {
          parent[right] = left;
        }
      }
      list.push(current);
    }
  }
  const islands = new Map();
  for (let triangle = 0; triangle < triangleCount; ++triangle) {
    const root = findRoot(parent, triangle);
    let list = islands.get(root);
    if (!list) {
      list = [];
      islands.set(root, list);
    }
    list.push(triangle);
  }
  return islands;
}

function writePrimitiveIndices(primitive, indices) {
  const document = Document.fromGraph(primitive.getGraph());
  if (!document) {
    throw new Error("プリミティブからDocumentを取得できません");
  }
  primitive.setIndices(
    document.createAccessor().setType("SCALAR").setArray(indices),
  );
  compactPrimitive(primitive);
}

function simplifyPrimitive(primitive, entry) {
  const position = primitive.getAttribute("POSITION");
  const indices = primitive.getIndices();
  if (!position || !indices || primitive.getMode() !== 4) {
    return { source: 0, result: 0 };
  }
  const positionArray = floatAttribute(position);
  const indexArray = indices.getArray();
  if (!indexArray) {
    return { source: 0, result: 0 };
  }
  const sourceIndices = Uint32Array.from(indexArray);
  const texcoord = primitive.getAttribute("TEXCOORD_0");
  const uvArray = texcoord ? floatAttribute(texcoord) : null;

  let result;
  if (!uvArray) {
    result = simplifyIndexGroup(sourceIndices, positionArray, null, entry);
  } else {
    const islands = collectUvIslands(
      sourceIndices,
      positionArray,
      uvArray,
      entry.uvSeam,
    );
    const chunks = [];
    let total = 0;
    for (const triangleIds of islands.values()) {
      const islandIndices = new Uint32Array(triangleIds.length * 3);
      for (let i = 0; i < triangleIds.length; ++i) {
        const triangle = triangleIds[i];
        islandIndices[i * 3] = sourceIndices[triangle * 3];
        islandIndices[i * 3 + 1] = sourceIndices[triangle * 3 + 1];
        islandIndices[i * 3 + 2] = sourceIndices[triangle * 3 + 2];
      }
      const simplified = simplifyIndexGroup(
        islandIndices,
        positionArray,
        uvArray,
        entry,
      );
      chunks.push(simplified);
      total += simplified.length;
    }
    result = new Uint32Array(total);
    let offset = 0;
    for (let i = 0; i < chunks.length; ++i) {
      result.set(chunks[i], offset);
      offset += chunks[i].length;
    }
  }

  if (result.length < 3) {
    return { source: sourceIndices.length, result: sourceIndices.length };
  }
  result = weldByPositionUv(result, positionArray, uvArray);
  writePrimitiveIndices(primitive, result);
  return { source: sourceIndices.length, result: result.length };
}

async function simplifyOne(io, entry) {
  if (!fs.existsSync(entry.input)) {
    throw new Error(`入力GLBがありません: ${entry.input}`);
  }

  const document = await io.read(entry.input);

  let sourceIndices = 0;
  let resultIndices = 0;
  for (const mesh of document.getRoot().listMeshes()) {
    for (const primitive of mesh.listPrimitives()) {
      const counts = simplifyPrimitive(primitive, entry);
      sourceIndices += counts.source;
      resultIndices += counts.result;
    }
  }

  fs.mkdirSync(path.dirname(entry.output), { recursive: true });
  const temporaryOutput = `${entry.output}.simplify.tmp.glb`;
  try {
    await io.write(temporaryOutput, document);
    fs.rmSync(entry.output, { force: true });
    fs.renameSync(temporaryOutput, entry.output);
  } finally {
    fs.rmSync(temporaryOutput, { force: true });
    if (typeof document.dispose === "function") {
      document.dispose();
    }
  }
  const sourceFaces = sourceIndices / 3;
  const resultFaces = resultIndices / 3;
  console.log(
    `simplified ${entry.input} -> ${entry.output}` +
      ` (ratio ${entry.ratio}, error ${entry.error}, uvWeight ${entry.uvWeight},` +
      ` uvSeam ${entry.uvSeam}, faces ${sourceFaces} -> ${resultFaces})`,
  );
}

async function main() {
  const entries = parseOne(process.argv.slice(2));
  await MeshoptSimplifier.ready;
  const io = new NodeIO();
  for (const entry of entries) {
    await simplifyOne(io, entry);
  }
}

main().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
