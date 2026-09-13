/* eslint-disable no-console */
/**
 * Draco圧縮されたGLBを、標準の頂点属性を持つ非圧縮GLBへ変換する。
 *
 * 依存関係:
 *   npm install --prefix tool
 *
 * CESIUM_RTCは既存のAssimp経路が解釈しないため、GLBから除去する。
 * EXT_texture_webpは必須拡張宣言を外し、標準のtexture.sourceへ付け替える。
 * 埋め込みWebP画像本体は残し、実行時のDirectXTex WIC経路で読む。
 * 元のRTC_CENTERはprepare_expo_model.pyのメタデータへ保存される。
 */

const fs = require("fs");
const path = require("path");
const { NodeIO } = require("@gltf-transform/core");
const {
  KHRDracoMeshCompression,
  EXTTextureWebP,
} = require("@gltf-transform/extensions");
const draco3d = require("draco3dgltf");

const GLB_HEADER_SIZE = 12;
const GLB_CHUNK_HEADER_SIZE = 8;
const GLB_JSON_CHUNK = 0x4e4f534a;
const GLB_BIN_CHUNK = 0x004e4942;

function align4(value) {
  return (value + 3) & ~3;
}

function removeJsonExtensions(glb, extensionNames) {
  if (glb.length < GLB_HEADER_SIZE + GLB_CHUNK_HEADER_SIZE) {
    throw new Error("GLBヘッダーが短すぎます");
  }

  if (glb.toString("ascii", 0, 4) !== "glTF") {
    throw new Error("GLB magicが不正です");
  }

  const jsonLength = glb.readUInt32LE(12);
  const jsonType = glb.readUInt32LE(16);
  if (jsonType !== GLB_JSON_CHUNK) {
    throw new Error("GLBの先頭チャンクがJSONではありません");
  }

  const jsonStart = 20;
  const jsonEnd = jsonStart + jsonLength;
  if (jsonEnd > glb.length) {
    throw new Error("GLB JSONチャンクが実サイズを超えています");
  }

  const drop = new Set(extensionNames);
  const json = JSON.parse(
    glb.toString("utf8", jsonStart, jsonEnd).replace(/\s+$/, ""),
  );
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
    const chunkType = glb.readUInt32LE(offset + 4);
    const chunkStart = offset + GLB_CHUNK_HEADER_SIZE;
    const chunkEnd = chunkStart + chunkLength;
    if (chunkEnd > glb.length) {
      throw new Error("GLBチャンクが実サイズを超えています");
    }
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

function uint32(value) {
  const buffer = Buffer.alloc(4);
  buffer.writeUInt32LE(value, 0);
  return buffer;
}

async function main() {
  const [, , inputPath, outputPath] = process.argv;
  if (!inputPath || !outputPath) {
    throw new Error("使用方法: node decode_draco_glb.cjs input.glb output.glb");
  }

  const source = fs.readFileSync(inputPath);
  const inputWithoutRtc = removeJsonExtensions(source, ["CESIUM_RTC"]);
  const temporaryInput = `${outputPath}.without-cesium-rtc.tmp.glb`;
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(temporaryInput, inputWithoutRtc);

  try {
    const io = new NodeIO()
      .registerExtensions([KHRDracoMeshCompression, EXTTextureWebP])
      .registerDependencies({
        "draco3d.decoder": await draco3d.createDecoderModule(),
      });
    const document = await io.read(temporaryInput);

    // 読み込み時に展開された頂点属性を残し、Draco拡張だけを無効化して出力する。
    // WebP画像本体は残し、Assimpが未知の必須拡張で失敗しないよう宣言だけ外す。
    document.disposeExtension("KHR_draco_mesh_compression");
    await io.write(outputPath, document);
    const decoded = fs.readFileSync(outputPath);
    fs.writeFileSync(
      outputPath,
      removeJsonExtensions(decoded, ["EXT_texture_webp"]),
    );
  } finally {
    fs.rmSync(temporaryInput, { force: true });
  }
}

main().catch((error) => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
