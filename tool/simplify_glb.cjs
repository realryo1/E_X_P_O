/* eslint-disable no-console */
/**
 * meshoptimizerでGLBの三角形数を削減する。
 *
 * 使用方法:
 *   node tool/simplify_glb.cjs input.glb output.glb --ratio 0.4
 *   node tool/simplify_glb.cjs --config tool/expo_simplify.json
 *
 * BlenderのDecimateと同じアルゴリズムではないため、ratioは目標値である。
 * テクスチャ、マテリアル、ノード階層はgltf-transformで保持する。
 */

const fs = require("fs");
const path = require("path");
const { NodeIO } = require("@gltf-transform/core");
const { weld } = require("@gltf-transform/functions");
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
      error: entry.error === undefined ? 0.01 : Number(entry.error),
    }));
  }

  if (!args[0] || !args[1]) {
    throw new Error(
      "使用方法: node simplify_glb.cjs input.glb output.glb --ratio 0.4 [--error 0.01]",
    );
  }
  return [{
    input: args[0],
    output: args[1],
    ratio: parseNumber(valueAfter(args, "--ratio") || "0.4", "--ratio"),
    error: Number(valueAfter(args, "--error") || "0.01"),
  }];
}

async function simplifyOne(io, entry) {
  if (!fs.existsSync(entry.input)) {
    throw new Error(`入力GLBがありません: ${entry.input}`);
  }
  if (!Number.isFinite(entry.error) || entry.error < 0) {
    throw new Error(`errorは0以上の数値で指定してください: ${entry.error}`);
  }

  const document = await io.read(entry.input);
  for (const mesh of document.getRoot().listMeshes()) {
    for (const primitive of mesh.listPrimitives()) {
      const position = primitive.getAttribute("POSITION");
      const indices = primitive.getIndices();
      if (!position || !indices || primitive.getMode() !== 4) {
        continue;
      }
      const positionArray = position.getArray();
      const indexArray = indices.getArray();
      if (!(positionArray instanceof Float32Array) || !indexArray) {
        continue;
      }
      const sourceIndices = Uint32Array.from(indexArray);
      const targetIndexCount = Math.floor(
        (entry.ratio * sourceIndices.length) / 3,
      ) * 3;
      // 公式GLBはUVや法線の境界で頂点が分割されているため、
      // そのままではmeshoptimizerがトポロジーを辿れない。
      // 位置だけを一時的に共有化し、削減後は代表元の属性を使う。
      const uniquePositions = [];
      const representatives = [];
      const positionMap = new Map();
      const vertexRemap = new Uint32Array(position.getCount());
      for (let vertex = 0; vertex < position.getCount(); ++vertex) {
        const offset = vertex * 3;
        const key =
          `${positionArray[offset]},${positionArray[offset + 1]},` +
          `${positionArray[offset + 2]}`;
        let unique = positionMap.get(key);
        if (unique === undefined) {
          unique = representatives.length;
          positionMap.set(key, unique);
          representatives.push(vertex);
          uniquePositions.push(
            positionArray[offset],
            positionArray[offset + 1],
            positionArray[offset + 2],
          );
        }
        vertexRemap[vertex] = unique;
      }
      const remappedIndices = Uint32Array.from(
        sourceIndices,
        (index) => vertexRemap[index],
      );
      const [simplifiedIndices] = MeshoptSimplifier.simplify(
        remappedIndices,
        Float32Array.from(uniquePositions),
        3,
        targetIndexCount,
        entry.error,
      );
      const result = Uint32Array.from(
        simplifiedIndices,
        (index) => representatives[index],
      );
      if (result.length < 3) {
        continue;
      }
      primitive.setIndices(
        document.createAccessor().setType("SCALAR").setArray(result),
      );
    }
  }
  await document.transform(weld());

  fs.mkdirSync(path.dirname(entry.output), { recursive: true });
  const temporaryOutput = `${entry.output}.simplify.tmp.glb`;
  try {
    await io.write(temporaryOutput, document);
    // Windowsのrenameは既存ファイルを上書きしないため、先に削除する。
    fs.rmSync(entry.output, { force: true });
    fs.renameSync(temporaryOutput, entry.output);
  } finally {
    fs.rmSync(temporaryOutput, { force: true });
    if (typeof document.dispose === "function") {
      document.dispose();
    }
  }
  console.log(
    `simplified ${entry.input} -> ${entry.output} (ratio ${entry.ratio}, error ${entry.error})`,
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
