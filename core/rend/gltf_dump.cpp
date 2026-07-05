/*
	Copyright 2026 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */

// 3D screenshot: dumps the geometry of the current frame to a glTF 2.0 file,
// along with the textures in use, saved as PNG files.
//
// The PVR only ever sees post-transform vertices: x,y in framebuffer pixels
// and z = 1/w. 3D positions are recovered by un-projecting with an assumed
// vertical FOV of 60 degrees. This restores the correct 3D shape; only the
// global depth scale is an estimate since the game's actual projection is
// unknown. Naomi 2 geometry is pre-transform and is instead transformed to
// view space with its model-view matrix.

#include "gltf_dump.h"
#include "TexCache.h"
#include "hw/pvr/ta_ctx.h"
#include "oslib/i18n.h"
#include "stdclass.h"
#include <stb_image_write.h>
#include <glm/gtc/type_ptr.hpp>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace gltfdump {

static std::atomic<bool> captureRequested { false };

void requestCapture() {
	captureRequested.store(true, std::memory_order_release);
}

namespace {

struct ExportVertex {
	float x, y, z;
	u8 col[4];	// RGBA
	float u, v;
};

struct MaterialKey
{
	BaseTextureCacheData *texture;
	int listType;	// 0: opaque, 1: punch-through, 2: translucent
	u8 wrap;		// ClampU/V, FlipU/V bits

	bool operator<(const MaterialKey& o) const {
		if (texture != o.texture)
			return texture < o.texture;
		if (listType != o.listType)
			return listType < o.listType;
		return wrap < o.wrap;
	}
};

struct ExportMesh
{
	MaterialKey key;
	std::vector<ExportVertex> verts;
	std::vector<u32> indices;
	// (matrix index + 1) << 32 | global vertex index -> local vertex index
	std::unordered_map<u64, u32> vtxMap;
};

class FrameExporter
{
public:
	FrameExporter(const rend_context& ctx) : ctx(ctx)
	{
		float width = 640.f;
		float height = 480.f;
		if (ctx.fbClip.size.x > 0 && ctx.fbClip.size.y > 0)
		{
			width = (float)ctx.fbClip.size.x;
			height = (float)ctx.fbClip.size.y;
			cx = ctx.fbClip.origin.x + width / 2;
			cy = ctx.fbClip.origin.y + height / 2;
		}
		else
		{
			cx = width / 2;
			cy = height / 2;
		}
		// assumed vertical FOV of 60 deg: focal = (h/2) / tan(30 deg)
		focal = height * 0.866f;
		bgraColors = isDirectX(config::RendererType);
	}

	void extract()
	{
		const bool perPixel = config::RendererType == RenderType::OpenGL_OIT
				|| config::RendererType == RenderType::DirectX11_OIT
				|| config::RendererType == RenderType::Vulkan_OIT;
		RenderPass previous{};
		for (const RenderPass& pass : ctx.render_passes)
		{
			processList(ctx.global_param_op, previous.op_count, pass.op_count, true, 0);
			processList(ctx.global_param_pt, previous.pt_count, pass.pt_count, true, 1);
			// Autosorted translucent polys (except with per-pixel renderers or per-strip sorting)
			// aren't indexed: their first/count refer to the vertex array directly.
			const bool trIndexed = !pass.autosort || perPixel || config::PerStripSorting;
			processList(ctx.global_param_tr, previous.tr_count, pass.tr_count, trIndexed, 2);
			previous = pass;
		}
		// Drop meshes that ended up with no valid triangle
		meshes.erase(std::remove_if(meshes.begin(), meshes.end(),
				[](const ExportMesh& mesh) { return mesh.indices.empty(); }),
				meshes.end());
	}

	bool empty() const {
		return meshes.empty();
	}

	void save(const std::string& dir);

private:
	void processList(const std::vector<PolyParam>& polys, u32 first, u32 end, bool indexed, int listType)
	{
		end = std::min(end, (u32)polys.size());
		for (u32 pi = first; pi < end; pi++)
		{
			const PolyParam& pp = polys[pi];
			// count == 0: strip was merged into an equivalent previous poly
			if (pp.count < 3)
				continue;
			// Skip the background plane
			if (listType == 0 && pi == 0)
				continue;
			ExportMesh& mesh = getMesh(pp, listType);

			// Decode the triangle strip, skipping degenerate link triangles
			// and handling primitive restart markers.
			u32 v0 = 0, v1 = 0;
			u32 stripPos = 0;
			for (u32 k = 0; k < pp.count; k++)
			{
				u32 vi;
				if (indexed)
				{
					if (pp.first + k >= ctx.idx.size())
						break;
					vi = ctx.idx[pp.first + k];
					if (vi == ~0u) {
						// primitive restart
						stripPos = 0;
						continue;
					}
				}
				else
				{
					vi = pp.first + k;
					if (vi >= ctx.verts.size())
						break;
				}
				if (stripPos >= 2 && v0 != v1 && v1 != vi && v0 != vi)
				{
					if (stripPos & 1)
						addTriangle(mesh, pp, v1, v0, vi);
					else
						addTriangle(mesh, pp, v0, v1, vi);
				}
				v0 = v1;
				v1 = vi;
				stripPos++;
			}
		}
	}

	ExportMesh& getMesh(const PolyParam& pp, int listType)
	{
		MaterialKey key{};
		key.texture = pp.pcw.Texture ? pp.texture : nullptr;
		key.listType = listType;
		key.wrap = (pp.tsp.ClampU << 3) | (pp.tsp.ClampV << 2) | (pp.tsp.FlipU << 1) | pp.tsp.FlipV;
		auto it = meshMap.find(key);
		if (it != meshMap.end())
			return meshes[it->second];
		meshMap[key] = meshes.size();
		meshes.emplace_back();
		meshes.back().key = key;
		return meshes.back();
	}

	bool validVertex(const PolyParam& pp, u32 vtxIdx) const
	{
		if (vtxIdx >= ctx.verts.size())
			return false;
		const Vertex& v = ctx.verts[vtxIdx];
		if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
			return false;
		// depth can't be recovered from vertices at or behind the camera plane
		if (!pp.isNaomi2() && v.z < 1e-6f)
			return false;
		return true;
	}

	void addTriangle(ExportMesh& mesh, const PolyParam& pp, u32 a, u32 b, u32 c)
	{
		if (!validVertex(pp, a) || !validVertex(pp, b) || !validVertex(pp, c))
			return;
		const u32 la = addVertex(mesh, pp, a);
		const u32 lb = addVertex(mesh, pp, b);
		const u32 lc = addVertex(mesh, pp, c);
		mesh.indices.push_back(la);
		mesh.indices.push_back(lb);
		mesh.indices.push_back(lc);
	}

	u32 addVertex(ExportMesh& mesh, const PolyParam& pp, u32 vtxIdx)
	{
		const u64 key = ((u64)(pp.mvMatrix + 1) << 32) | vtxIdx;
		auto it = mesh.vtxMap.find(key);
		if (it != mesh.vtxMap.end())
			return it->second;

		const Vertex& v = ctx.verts[vtxIdx];
		ExportVertex ev{};
		if (pp.isNaomi2() && pp.mvMatrix >= 0 && pp.mvMatrix < (int)ctx.matrices.size())
		{
			// Naomi 2 vertices are in model space: transform to view space
			glm::vec4 p = glm::make_mat4(ctx.matrices[pp.mvMatrix].mat) * glm::vec4(v.x, v.y, v.z, 1.f);
			ev.x = p.x;
			ev.y = -p.y;
			ev.z = -p.z;
		}
		else
		{
			// Un-project screen space (x, y in pixels, z = 1/w) back to view space,
			// converting to glTF's right-handed, Y-up, -Z-forward convention.
			const float w = 1.f / v.z;
			ev.x = (v.x - cx) * w / focal;
			ev.y = -(v.y - cy) * w / focal;
			ev.z = -w;
		}
		ev.col[0] = v.col[bgraColors ? 2 : 0];
		ev.col[1] = v.col[1];
		ev.col[2] = v.col[bgraColors ? 0 : 2];
		ev.col[3] = v.col[3];
		ev.u = v.u;
		ev.v = v.v;

		const u32 localIdx = (u32)mesh.verts.size();
		mesh.verts.push_back(ev);
		mesh.vtxMap[key] = localIdx;
		return localIdx;
	}

	const rend_context& ctx;
	std::map<MaterialKey, size_t> meshMap;
	std::vector<ExportMesh> meshes;
	float cx, cy;
	float focal;
	bool bgraColors;
};

static bool writeFile(const std::string& path, const void *data, size_t size)
{
	FILE *f = nowide::fopen(path.c_str(), "wb");
	if (f == nullptr)
		return false;
	const bool ok = size == 0 || std::fwrite(data, size, 1, f) == 1;
	std::fclose(f);
	return ok;
}

static bool writePng(const std::string& path, const u8 *rgba, int width, int height)
{
	FILE *f = nowide::fopen(path.c_str(), "wb");
	if (f == nullptr)
		return false;
	stbi_flip_vertically_on_write(0);
	const auto writeFunc = [](void *context, void *data, int size) {
		std::fwrite(data, 1, size, (FILE *)context);
	};
	const int rc = stbi_write_png_to_func(writeFunc, f, width, height, 4, rgba, width * 4);
	std::fclose(f);
	return rc != 0;
}

// GL constants used by glTF
constexpr int GLTF_FLOAT = 5126;
constexpr int GLTF_UNSIGNED_BYTE = 5121;
constexpr int GLTF_UNSIGNED_INT = 5125;
constexpr int GLTF_ARRAY_BUFFER = 34962;
constexpr int GLTF_ELEMENT_ARRAY_BUFFER = 34963;
constexpr int GLTF_REPEAT = 10497;
constexpr int GLTF_MIRRORED_REPEAT = 33648;
constexpr int GLTF_CLAMP_TO_EDGE = 33071;

static int wrapMode(bool clamp, bool flip)
{
	if (clamp)
		return GLTF_CLAMP_TO_EDGE;
	if (flip)
		return GLTF_MIRRORED_REPEAT;
	return GLTF_REPEAT;
}

void FrameExporter::save(const std::string& dir)
{
	// Save the textures
	std::map<BaseTextureCacheData *, int> imageIndex;
	std::vector<std::string> imageFiles;
	for (const ExportMesh& mesh : meshes)
	{
		BaseTextureCacheData *texture = mesh.key.texture;
		if (texture == nullptr || imageIndex.count(texture) != 0)
			continue;
		std::vector<u8> rgba;
		if (!texture->decodeToRGBA(rgba)) {
			WARN_LOG(RENDERER, "3D screenshot: can't decode texture @ %x", texture->startAddress);
			continue;
		}
		std::ostringstream name;
		name.imbue(std::locale::classic());
		name << "tex_" << std::hex << texture->texture_hash << "_" << imageFiles.size() << ".png";
		if (!writePng(dir + "/" + name.str(), rgba.data(), texture->width, texture->height)) {
			WARN_LOG(RENDERER, "3D screenshot: can't save %s", name.str().c_str());
			continue;
		}
		imageIndex[texture] = (int)imageFiles.size();
		imageFiles.push_back(name.str());
	}

	// Samplers: one per wrap-mode combination in use
	std::map<u8, int> samplerIndex;
	// Textures: (image, sampler) pairs
	std::map<std::pair<int, int>, int> textureIndex;
	for (const ExportMesh& mesh : meshes)
	{
		auto it = imageIndex.find(mesh.key.texture);
		if (mesh.key.texture == nullptr || it == imageIndex.end())
			continue;
		if (samplerIndex.count(mesh.key.wrap) == 0)
			samplerIndex[mesh.key.wrap] = (int)samplerIndex.size();
		const std::pair<int, int> texKey(it->second, samplerIndex[mesh.key.wrap]);
		if (textureIndex.count(texKey) == 0)
			textureIndex[texKey] = (int)textureIndex.size();
	}

	// Pack the binary buffer: per mesh: positions, colors, UVs, indices
	struct MeshBufferInfo {
		u32 posOffset, posLength;
		u32 colOffset, colLength;
		u32 uvOffset, uvLength;
		u32 idxOffset, idxLength;
		float posMin[3], posMax[3];
		bool textured;
	};
	std::vector<MeshBufferInfo> bufInfos;
	std::vector<u8> bin;
	const auto append = [&bin](const void *data, size_t size) -> u32 {
		const u32 offset = (u32)bin.size();
		bin.insert(bin.end(), (const u8 *)data, (const u8 *)data + size);
		while (bin.size() % 4 != 0)
			bin.push_back(0);
		return offset;
	};

	for (const ExportMesh& mesh : meshes)
	{
		MeshBufferInfo info{};
		info.textured = mesh.key.texture != nullptr && imageIndex.count(mesh.key.texture) != 0;
		std::fill(std::begin(info.posMin), std::end(info.posMin), std::numeric_limits<float>::max());
		std::fill(std::begin(info.posMax), std::end(info.posMax), std::numeric_limits<float>::lowest());

		std::vector<float> floats;
		floats.reserve(mesh.verts.size() * 3);
		for (const ExportVertex& v : mesh.verts)
		{
			floats.push_back(v.x);
			floats.push_back(v.y);
			floats.push_back(v.z);
			info.posMin[0] = std::min(info.posMin[0], v.x);
			info.posMin[1] = std::min(info.posMin[1], v.y);
			info.posMin[2] = std::min(info.posMin[2], v.z);
			info.posMax[0] = std::max(info.posMax[0], v.x);
			info.posMax[1] = std::max(info.posMax[1], v.y);
			info.posMax[2] = std::max(info.posMax[2], v.z);
		}
		info.posLength = (u32)(floats.size() * sizeof(float));
		info.posOffset = append(floats.data(), info.posLength);

		std::vector<u8> colors;
		colors.reserve(mesh.verts.size() * 4);
		for (const ExportVertex& v : mesh.verts)
			colors.insert(colors.end(), std::begin(v.col), std::end(v.col));
		info.colLength = (u32)colors.size();
		info.colOffset = append(colors.data(), info.colLength);

		if (info.textured)
		{
			floats.clear();
			for (const ExportVertex& v : mesh.verts)
			{
				floats.push_back(v.u);
				floats.push_back(v.v);
			}
			info.uvLength = (u32)(floats.size() * sizeof(float));
			info.uvOffset = append(floats.data(), info.uvLength);
		}

		info.idxLength = (u32)(mesh.indices.size() * sizeof(u32));
		info.idxOffset = append(mesh.indices.data(), info.idxLength);

		bufInfos.push_back(info);
	}

	if (!writeFile(dir + "/scene.bin", bin.data(), bin.size()))
		throw FlycastException(dir + "/scene.bin");

	// Assemble the glTF JSON
	std::ostringstream j;
	j.imbue(std::locale::classic());
	j << std::setprecision(9);

	j << "{\n";
	j << "  \"asset\": {\"version\": \"2.0\", \"generator\": \"Flycast 3D Screenshot\"},\n";
	j << "  \"scene\": 0,\n";
	j << "  \"scenes\": [{\"nodes\": [";
	for (size_t i = 0; i < meshes.size(); i++)
		j << (i > 0 ? ", " : "") << i;
	j << "]}],\n";

	j << "  \"nodes\": [\n";
	for (size_t i = 0; i < meshes.size(); i++)
		j << "    {\"mesh\": " << i << "}" << (i + 1 < meshes.size() ? "," : "") << "\n";
	j << "  ],\n";

	static const char * const listNames[] = { "op", "pt", "tr" };

	j << "  \"materials\": [\n";
	for (size_t i = 0; i < meshes.size(); i++)
	{
		const MaterialKey& key = meshes[i].key;
		j << "    {\"name\": \"mat_" << i << "_" << listNames[key.listType] << "\",\n";
		j << "     \"pbrMetallicRoughness\": {\"metallicFactor\": 0, \"roughnessFactor\": 1";
		auto imgIt = imageIndex.find(key.texture);
		if (key.texture != nullptr && imgIt != imageIndex.end())
		{
			const std::pair<int, int> texKey(imgIt->second, samplerIndex[key.wrap]);
			j << ", \"baseColorTexture\": {\"index\": " << textureIndex[texKey] << "}";
		}
		j << "},\n";
		switch (key.listType)
		{
		case 1:
			j << "     \"alphaMode\": \"MASK\", \"alphaCutoff\": " << ((PT_ALPHA_REF & 0xff) / 255.f) << ",\n";
			break;
		case 2:
			j << "     \"alphaMode\": \"BLEND\",\n";
			break;
		default:
			break;
		}
		j << "     \"doubleSided\": true}" << (i + 1 < meshes.size() ? "," : "") << "\n";
	}
	j << "  ],\n";

	// Accessors: per mesh: position, color, [uv], indices
	int accessorIdx = 0;
	j << "  \"meshes\": [\n";
	for (size_t i = 0; i < meshes.size(); i++)
	{
		j << "    {\"name\": \"mesh_" << i << "_" << listNames[meshes[i].key.listType] << "\",\n";
		j << "     \"primitives\": [{\"attributes\": {\"POSITION\": " << accessorIdx
		  << ", \"COLOR_0\": " << accessorIdx + 1;
		int nextIdx = accessorIdx + 2;
		if (bufInfos[i].textured)
			j << ", \"TEXCOORD_0\": " << nextIdx++;
		j << "}, \"indices\": " << nextIdx++ << ", \"material\": " << i << "}]}"
		  << (i + 1 < meshes.size() ? "," : "") << "\n";
		accessorIdx = nextIdx;
	}
	j << "  ],\n";

	j << "  \"accessors\": [\n";
	int bufferViewIdx = 0;
	for (size_t i = 0; i < meshes.size(); i++)
	{
		const MeshBufferInfo& info = bufInfos[i];
		const size_t vtxCount = meshes[i].verts.size();
		j << "    {\"bufferView\": " << bufferViewIdx++ << ", \"componentType\": " << GLTF_FLOAT
		  << ", \"count\": " << vtxCount << ", \"type\": \"VEC3\""
		  << ", \"min\": [" << info.posMin[0] << ", " << info.posMin[1] << ", " << info.posMin[2] << "]"
		  << ", \"max\": [" << info.posMax[0] << ", " << info.posMax[1] << ", " << info.posMax[2] << "]},\n";
		j << "    {\"bufferView\": " << bufferViewIdx++ << ", \"componentType\": " << GLTF_UNSIGNED_BYTE
		  << ", \"normalized\": true, \"count\": " << vtxCount << ", \"type\": \"VEC4\"},\n";
		if (info.textured)
			j << "    {\"bufferView\": " << bufferViewIdx++ << ", \"componentType\": " << GLTF_FLOAT
			  << ", \"count\": " << vtxCount << ", \"type\": \"VEC2\"},\n";
		j << "    {\"bufferView\": " << bufferViewIdx++ << ", \"componentType\": " << GLTF_UNSIGNED_INT
		  << ", \"count\": " << meshes[i].indices.size() << ", \"type\": \"SCALAR\"}"
		  << (i + 1 < meshes.size() ? "," : "") << "\n";
	}
	j << "  ],\n";

	j << "  \"bufferViews\": [\n";
	for (size_t i = 0; i < meshes.size(); i++)
	{
		const MeshBufferInfo& info = bufInfos[i];
		j << "    {\"buffer\": 0, \"byteOffset\": " << info.posOffset << ", \"byteLength\": " << info.posLength
		  << ", \"target\": " << GLTF_ARRAY_BUFFER << "},\n";
		j << "    {\"buffer\": 0, \"byteOffset\": " << info.colOffset << ", \"byteLength\": " << info.colLength
		  << ", \"target\": " << GLTF_ARRAY_BUFFER << "},\n";
		if (info.textured)
			j << "    {\"buffer\": 0, \"byteOffset\": " << info.uvOffset << ", \"byteLength\": " << info.uvLength
			  << ", \"target\": " << GLTF_ARRAY_BUFFER << "},\n";
		j << "    {\"buffer\": 0, \"byteOffset\": " << info.idxOffset << ", \"byteLength\": " << info.idxLength
		  << ", \"target\": " << GLTF_ELEMENT_ARRAY_BUFFER << "}"
		  << (i + 1 < meshes.size() ? "," : "") << "\n";
	}
	j << "  ],\n";

	j << "  \"buffers\": [{\"uri\": \"scene.bin\", \"byteLength\": " << bin.size() << "}]";

	if (!imageFiles.empty())
	{
		j << ",\n  \"images\": [\n";
		for (size_t i = 0; i < imageFiles.size(); i++)
			j << "    {\"uri\": \"" << imageFiles[i] << "\"}" << (i + 1 < imageFiles.size() ? "," : "") << "\n";
		j << "  ],\n";

		j << "  \"samplers\": [\n";
		{
			// samplerIndex values are assigned in insertion order: emit sorted by index
			std::vector<u8> wraps(samplerIndex.size());
			for (const auto& [wrap, idx] : samplerIndex)
				wraps[idx] = wrap;
			for (size_t i = 0; i < wraps.size(); i++)
				j << "    {\"wrapS\": " << wrapMode(wraps[i] & 8, wraps[i] & 2)
				  << ", \"wrapT\": " << wrapMode(wraps[i] & 4, wraps[i] & 1) << "}"
				  << (i + 1 < wraps.size() ? "," : "") << "\n";
		}
		j << "  ],\n";

		j << "  \"textures\": [\n";
		{
			std::vector<std::pair<int, int>> texKeys(textureIndex.size());
			for (const auto& [texKey, idx] : textureIndex)
				texKeys[idx] = texKey;
			for (size_t i = 0; i < texKeys.size(); i++)
				j << "    {\"source\": " << texKeys[i].first << ", \"sampler\": " << texKeys[i].second << "}"
				  << (i + 1 < texKeys.size() ? "," : "") << "\n";
		}
		j << "  ]";
	}
	j << "\n}\n";

	const std::string json = j.str();
	if (!writeFile(dir + "/scene.gltf", json.data(), json.size()))
		throw FlycastException(dir + "/scene.gltf");
}

} // anonymous namespace

void checkCapture(TA_context *ctx)
{
	if (!captureRequested.load(std::memory_order_acquire))
		return;
	// Wait for a frame rendered to the screen
	if (ctx == nullptr || ctx->rend.isRTT)
		return;
	captureRequested.store(false, std::memory_order_relaxed);

	try {
		FrameExporter exporter(ctx->rend);
		exporter.extract();
		if (exporter.empty()) {
			os_notify(i18n::T("No 3D geometry to capture"), 2000);
			return;
		}
		std::string date = timeToISO8601(time(nullptr));
		std::replace(date.begin(), date.end(), '/', '-');
		std::replace(date.begin(), date.end(), ':', '-');
		const std::string name = "Flycast-3D-" + date;
		const std::string dir = hostfs::getScreenshots3DPath() + "/" + name;
		if (!make_directory(dir) && !file_exists(dir))
			throw FlycastException(dir);
		exporter.save(dir);
		os_notify(i18n::T("3D screenshot saved"), 5000, name.c_str());
	} catch (const std::exception& e) {
		WARN_LOG(RENDERER, "3D screenshot failed: %s", e.what());
		os_notify(i18n::T("Error saving 3D screenshot"), 5000, e.what());
	}
}

} // namespace gltfdump
