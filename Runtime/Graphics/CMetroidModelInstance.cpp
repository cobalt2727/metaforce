#include "CMetroidModelInstance.hpp"

#include "Graphics/CCubeSurface.hpp"
#include "Streams/IOStreams.hpp"

#include <zeus/CColor.hpp>

namespace metaforce {
CMetroidModelInstance::CMetroidModelInstance(std::pair<const u8*, u32> modelHeader, const u8* materialData,
                                             std::pair<const u8*, u32> positions, std::pair<const u8*, u32> normals,
                                             std::pair<const u8*, u32> colors, std::pair<const u8*, u32> texCoords,
                                             std::pair<const u8*, u32> packedTexCoords,
                                             std::vector<CCubeSurface>&& surfaces)
: x4c_materialData(materialData), x50_surfaces(std::move(surfaces)) {
  {
    CMemoryInStream stream{modelHeader.first, modelHeader.second};
    x0_visorFlags = stream.Get<u32>();
    x4_worldXf = stream.Get<zeus::CTransform>();
    x34_worldAABB = stream.Get<zeus::CAABox>();
  }
  {
    u32 numVertices = positions.second / 12;
    CMemoryInStream stream{positions.first, positions.second};
    for (u32 i = 0; i < numVertices; ++i) {
      x60_positions.emplace_back(stream.Get<zeus::CVector3f>());
    }
  }
  {
    // Always short normals in MREA
    u32 numNormals = normals.second / 6;
    CMemoryInStream stream{normals.first, normals.second};
    for (u32 i = 0; i < numNormals; ++i) {
      x64_normals.emplace_back(static_cast<float>(stream.ReadShort()) / 16384.f,
                               static_cast<float>(stream.ReadShort()) / 16384.f,
                               static_cast<float>(stream.ReadShort()) / 16384.f);
    }
  }
  {
    u32 numColors = colors.second / 4;
    CMemoryInStream stream{colors.first, colors.second};
    for (u32 i = 0; i < numColors; ++i) {
      x68_colors.emplace_back(zeus::CColor(stream.ReadUint32()));
    }
  }
  {
    u32 numTexCoords = texCoords.second / 8;
    CMemoryInStream stream{texCoords.first, texCoords.second};
    for (u32 i = 0; i < numTexCoords; ++i) {
      x6c_texCoords.emplace_back(stream.Get<zeus::CVector2f>());
    }
  }
  {
    u32 numPackedTexCoords = packedTexCoords.second / 4;
    CMemoryInStream stream{packedTexCoords.first, packedTexCoords.second};
    for (u32 i = 0; i < numPackedTexCoords; ++i) {
      x70_packedTexCoords.emplace_back(static_cast<float>(stream.ReadShort()) / 32768.f,
                                       static_cast<float>(stream.ReadShort()) / 32768.f);
    }
  }
}
} // namespace metaforce
