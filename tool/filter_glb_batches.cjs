/* eslint-disable no-console */
/**
 * gltf-transformで書いたGLBのまま、_BATCHIDで三角形を残す／捨てる。
 * Pythonの再パックを通さないので、LOD2と同じ埋め込みWebP経路を保てる。
 *
 *   node tool/filter_glb_batches.cjs input.glb output.glb --keep 2,4 --drop-no-uv
 *   node tool/filter_glb_batches.cjs input.glb output.glb --drop 5,21
 *   node tool/filter_glb_batches.cjs input.glb output.glb --keep 2,4 --split-batch
 */
const fs = require("fs");
const path = require("path");
const { NodeIO } = require("@gltf-transform/core");
const { EXTTextureWebP } = require("@gltf-transform/extensions");

const GLB_HEADER_SIZE = 12;
const GLB_CHUNK_HEADER_SIZE = 8;
const GLB_JSON_CHUNK = 0x4e4f534a;
const GLB_BIN_CHUNK = 0x004e4942;

function align4(value) {
  return (value + 3) & ~3;
}

function parseIdList(text) {
  if (!text) return new Set();
  return new Set(
    text
      .split(",")
      .map((part) => Number(part.trim()))
      .filter((value) => Number.isInteger(value)),
  );
}

function removeJsonExtensions(glb, extensionNames) {
  const jsonLength = glb.readUInt32LE(12);
  const jsonType = glb.readUInt32LE(16);
  if (jsonType !== GLB_JSON_CHUNK) {
    throw new Error("GLBの先頭チャンクがJSONではありません");
  }
  const jsonStart = 20;
  const jsonEnd = jsonStart + jsonLength;
  const drop = new Set(extensionNames);
  const json = JSON.parse(glb.toString("utf8", jsonStart, jsonEnd).replace(/\s+$/, ""));
  if (Array.isArray(json.textures) && drop.has("EXT_texture_webp")) {
    for (const texture of json.textures) {
      const webp = texture && texture.extensions && texture.extensions.EXT_texture_webp;
      if (webp && Number.isInteger(webp.source)) {
        texture.source = webp.source;
        delete texture.extensions.EXT_texture_webp;
        if (Object.keys(texture.extensions).length === 0) {
          delete texture.extensions;
        }
      }
    }
  }
  if (json.extensions) {
    for (const name of drop) {
      delete json.extensions[name];
    }
    if (Object.keys(json.extensions).length === 0) {
      delete json.extensions;
    }
  }
  for (const key of ["extensionsUsed", "extensionsRequired"]) {
    if (Array.isArray(json[key])) {
      json[key] = json[key].filter((name) => !drop.has(name));
      if (json[key].length === 0) {
        delete json[key];
      }
    }
  }
  const jsonData = Buffer.from(JSON.stringify(json), "utf8");
  const paddedJson = Buffer.concat([
    jsonData,
    Buffer.alloc(align4(jsonData.length) - jsonData.length, 0x20),
  ]);
  let offset = jsonEnd;
  const chunks = [];
  while (offset + GLB_CHUNK_HEADER_SIZE <= glb.length) {
    const chunkLength = glb.readUInt32LE(offset);
    const chunkStart = offset + GLB_CHUNK_HEADER_SIZE;
    const chunkEnd = chunkStart + chunkLength;
    chunks.push(glb.subarray(chunkStart, chunkEnd));
    offset = chunkEnd;
  }
  const binData = chunks.length > 0 ? chunks[chunks.length - 1] : Buffer.alloc(0);
  const outputLength =
    GLB_HEADER_SIZE +
    GLB_CHUNK_HEADER_SIZE +
    paddedJson.length +
    GLB_CHUNK_HEADER_SIZE +
    binData.length;
  const uint32 = (value) => {
    const buffer = Buffer.alloc(4);
    buffer.writeUInt32LE(value, 0);
    return buffer;
  };
  return Buffer.concat([
    Buffer.from("glTF"),
    Buffer.from([2, 0, 0, 0]),
    uint32(outputLength),
    uint32(paddedJson.length),
    uint32(GLB_JSON_CHUNK),
    paddedJson,
    uint32(binData.length),
    uint32(GLB_BIN_CHUNK),
    binData,
  ]);
}

function batchIdOf(prim) {
  return prim.getAttribute("_BATCHID") || prim.getAttribute("_FEATURE_ID_0");
}

function addSplitPrimitive(document, mesh, source, buffer, indices, batchId) {
  const maxIndex = indices.reduce(
    (maximum, value) => Math.max(maximum, value),
    0,
  );
  const array = maxIndex <= 65535
    ? new Uint16Array(indices)
    : new Uint32Array(indices);
  const accessor = document
    .createAccessor()
    .setType("SCALAR")
    .setArray(array);
  if (buffer) {
    accessor.setBuffer(buffer);
  }

  const primitive = document.createPrimitive();
  for (const semantic of source.listSemantics()) {
    primitive.setAttribute(semantic, source.getAttribute(semantic));
  }
  primitive
    .setIndices(accessor)
    .setMaterial(source.getMaterial())
    .setMode(source.getMode())
    .setExtras({
      ...source.getExtras(),
      expo_batch_id: batchId,
    });
  mesh.addPrimitive(primitive);
}

async function main() {
  const args = process.argv.slice(2);
  const inputPath = args[0];
  const outputPath = args[1];
  if (!inputPath || !outputPath) {
    throw new Error(
      "使用方法: node filter_glb_batches.cjs input.glb output.glb [--keep 1,2] [--drop 5,21] [--drop-no-uv] [--drop-untextured-only] [--split-batch]",
    );
  }
  const keepIndex = args.indexOf("--keep");
  const dropIndex = args.indexOf("--drop");
  const keepIds = parseIdList(keepIndex >= 0 ? args[keepIndex + 1] : "");
  const dropIds = parseIdList(dropIndex >= 0 ? args[dropIndex + 1] : "");
  const dropNoUv = args.includes("--drop-no-uv");
  const dropUntexturedOnly = args.includes("--drop-untextured-only");
  const splitBatch = args.includes("--split-batch");
  if (keepIds.size === 0 && dropIds.size === 0) {
    throw new Error("--keep または --drop を指定してください");
  }

  const io = new NodeIO().registerExtensions([EXTTextureWebP]);
  const document = await io.read(inputPath);
  const buffer = document.getRoot().listBuffers()[0];

  for (const mesh of document.getRoot().listMeshes()) {
    const primitives = mesh.listPrimitives().slice();
    for (const prim of primitives) {
      if (dropUntexturedOnly && prim.getAttribute("TEXCOORD_0")) {
        continue;
      }
      if (dropNoUv && !prim.getAttribute("TEXCOORD_0")) {
        prim.dispose();
        continue;
      }
      const batch = batchIdOf(prim);
      const indices = prim.getIndices();
      if (!batch || !indices) {
        if (keepIds.size > 0) {
          prim.dispose();
        }
        continue;
      }
      const kept = [];
      const split = new Map();
      const count = indices.getCount();
      for (let i = 0; i + 2 < count; i += 3) {
        const a = indices.getScalar(i);
        const b = indices.getScalar(i + 1);
        const c = indices.getScalar(i + 2);
        const ids = [batch.getScalar(a), batch.getScalar(b), batch.getScalar(c)];
        const keepTriangle = keepIds.size > 0
          ? ids.some((id) => keepIds.has(id))
          : ids.every((id) => !dropIds.has(id));
        if (keepTriangle) {
          kept.push(a, b, c);
          if (splitBatch) {
            const batchIds = [...new Set(ids)].filter((id) =>
              keepIds.size > 0 ? keepIds.has(id) : !dropIds.has(id),
            );
            for (const batchId of batchIds) {
              if (!split.has(batchId)) split.set(batchId, []);
              split.get(batchId).push(a, b, c);
            }
          }
        }
      }
      if (kept.length === 0) {
        prim.dispose();
        continue;
      }
      if (splitBatch) {
        for (const [batchId, batchIndices] of split) {
          addSplitPrimitive(document, mesh, prim, buffer, batchIndices, batchId);
        }
        prim.dispose();
        continue;
      }

      const maxIndex = kept.reduce(
        (maximum, value) => Math.max(maximum, value),
        0,
      );
      // インデックス本数ではなく、実際の最大値でcomponentTypeを決める。
      // 本数が少なくても65535を超える参照は16bitへ変換できない。
      const array = maxIndex <= 65535
        ? new Uint16Array(kept)
        : new Uint32Array(kept);
      const next = document
        .createAccessor()
        .setType("SCALAR")
        .setArray(array);
      if (buffer) {
        next.setBuffer(buffer);
      }
      prim.setIndices(next);
    }
  }

  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  await io.write(outputPath, document);
  const rewritten = fs.readFileSync(outputPath);
  fs.writeFileSync(outputPath, removeJsonExtensions(rewritten, ["EXT_texture_webp"]));
}

main().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
