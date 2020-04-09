#pragma once

#include "DataSpec/DNACommon/DNACommon.hpp"
#include "DataSpec/DNACommon/PAK.hpp"

namespace DataSpec::DNAPATH {
template <atUint32 Ver>
struct RegionPointers {};
template <>
struct RegionPointers<4> : BigDNA {
  AT_DECL_DNA
  Value<atUint32> regionIdxPtr;
};
template <>
struct RegionPointers<6> : BigDNA {
  AT_DECL_DNA
  Value<atUint32> unk0;
  Value<atUint32> unk1;
  Value<atUint32> unk2;
  Value<atUint32> regionIdxPtr;
};
template <>
struct RegionPointers<7> : BigDNA {
  AT_DECL_DNA
  Value<atUint32> unk0;
  Value<atUint32> unk1;
  Value<atUint32> unk2;
  Value<atUint32> regionIdxPtr;
};

template <atUint32 Ver>
struct AT_SPECIALIZE_PARMS(4, 6, 7) PATH : BigDNA {
  using PathMesh = hecl::blender::PathMesh;

  AT_DECL_DNA
  Value<atUint32> version;

  struct Node : BigDNA {
    AT_DECL_DNA
    Value<atVec3f> position;
    Value<atVec3f> normal;
  };
  Value<atUint32> nodeCount;
  Vector<Node, AT_DNA_COUNT(nodeCount)> nodes;

  struct Link : BigDNA {
    AT_DECL_DNA
    Value<atUint32> nodeIdx;
    Value<atUint32> regionIdx;
    Value<float> width2d;
    Value<float> oneOverWidth2d;
  };
  Value<atUint32> linkCount;
  Vector<Link, AT_DNA_COUNT(linkCount)> links;

  struct Region : BigDNA {
    AT_DECL_DNA
    Value<atUint32> nodeCount;
    Value<atUint32> nodeStart;
    Value<atUint32> linkCount;
    Value<atUint32> linkStart;
    Value<atUint16> meshIndexMask;
    Value<atUint16> meshTypeMask;
    Value<float> height;
    Value<atVec3f> normal;
    Value<atUint32> regionIdx;
    Value<atVec3f> centroid;
    Value<atVec3f> aabb[2];
    Value<RegionPointers<Ver>> pointers;
  };
  Value<atUint32> regionCount;
  Vector<Region, AT_DNA_COUNT(regionCount)> regions;

  Vector<atUint32, AT_DNA_COUNT((((regionCount * (regionCount - 1)) / 2) + 31) / 32)> bitmap1;
  Vector<atUint32, AT_DNA_COUNT(bitmap1.size())> bitmap2;
  Vector<atUint32, AT_DNA_COUNT(((((regionCount * regionCount) + 31) / 32) - bitmap1.size()) * 2)> bitmap3;

  Value<atUint32> octreeRegionLookupCount;
  Vector<atUint32, AT_DNA_COUNT(octreeRegionLookupCount)> octreeRegionLookup;

  struct OctreeNode : BigDNA {
    AT_DECL_DNA
    Value<atUint32> isLeaf;
    Value<atVec3f> aabb[2];
    Value<atVec3f> centroid;
    Value<atUint32> children[8];
    Value<atUint32> regionCount;
    Value<atUint32> regionStart;
  };
  Value<atUint32> octreeNodeCount;
  Vector<OctreeNode, AT_DNA_COUNT(octreeNodeCount)> octree;

  void sendToBlender(hecl::blender::Connection& conn, std::string_view entryName, const zeus::CMatrix4f* xf,
                     const std::string& areaPath);

  static bool Cook(const hecl::ProjectPath& outPath, const hecl::ProjectPath& inPath,
                   const PathMesh& mesh, hecl::blender::Token& btok);
};
} // namespace DataSpec::DNAMP1
