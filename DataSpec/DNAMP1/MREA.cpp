#include "hecl/ClientProcess.hpp"
#include "athena/MemoryReader.hpp"
#include "MREA.hpp"
#include "SCLY.hpp"
#include "PATH.hpp"
#include "DeafBabe.hpp"
#include "DataSpec/DNACommon/BabeDead.hpp"
#include "zeus/Math.hpp"
#include "zeus/CAABox.hpp"
#include "DataSpec/DNACommon/AROTBuilder.hpp"
#include "ScriptObjects/ScriptTypes.hpp"
#include "hecl/Blender/Connection.hpp"

#define DUMP_OCTREE 0

extern std::string ExeDir;

namespace DataSpec::DNAMP1 {

void MREA::ReadBabeDeadToBlender_1_2(hecl::blender::PyOutStream& os, athena::io::IStreamReader& rs) {
  atUint32 bdMagic = rs.readUint32Big();
  if (bdMagic != 0xBABEDEAD)
    Log.report(logvisor::Fatal, FMT_STRING("invalid BABEDEAD magic"));
  os << "bpy.context.scene.world.use_nodes = True\n"
        "bg_node = bpy.context.scene.world.node_tree.nodes['Background']\n"
        "bg_node.inputs[1].default_value = 0.0\n";
  for (atUint32 s = 0; s < 2; ++s) {
    atUint32 lightCount = rs.readUint32Big();
    for (atUint32 l = 0; l < lightCount; ++l) {
      BabeDeadLight light;
      light.read(rs);
      ReadBabeDeadLightToBlender(os, light, s, l);
    }
  }
}

void MREA::AddCMDLRigPairs(PAKEntryReadStream& rs, PAKRouter<PAKBridge>& pakRouter,
                           CharacterAssociations<UniqueID32>& charAssoc) {
  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  /* Skip to SCLY */
  atUint32 curSec = 0;
  atUint64 secStart = rs.position();
  while (curSec != head.sclySecIdx)
    secStart += head.secSizes[curSec++];
  rs.seek(secStart, athena::SeekOrigin::Begin);
  SCLY scly;
  scly.read(rs);
  scly.addCMDLRigPairs(pakRouter, charAssoc);
}

UniqueID32 MREA::GetPATHId(PAKEntryReadStream& rs) {
  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  /* Skip to PATH */
  atUint32 curSec = 0;
  atUint64 secStart = rs.position();
  while (curSec != head.pathSecIdx)
    secStart += head.secSizes[curSec++];
  if (!head.secSizes[curSec])
    return {};
  rs.seek(secStart, athena::SeekOrigin::Begin);
  return {rs};
}

#if DUMP_OCTREE
/* Collision octree dumper */
static void OutputOctreeNode(hecl::blender::PyOutStream& os, athena::io::MemoryReader& r, BspNodeType type,
                             const zeus::CAABox& aabb) {
  if (type == BspNodeType::Branch) {
    u16 flags = r.readUint16Big();
    r.readUint16Big();
    u32 offsets[8];
    for (int i = 0; i < 8; ++i)
      offsets[i] = r.readUint32Big();
    u32 dataStart = r.position();
    for (int i = 0; i < 8; ++i) {
      r.seek(dataStart + offsets[i], athena::SeekOrigin::Begin);
      int chFlags = (flags >> (i * 2)) & 0x3;

      zeus::CAABox pos, neg, res;
      aabb.splitZ(neg, pos);
      if (i & 4) {
        zeus::CAABox(pos).splitY(neg, pos);
        if (i & 2) {
          zeus::CAABox(pos).splitX(neg, pos);
          if (i & 1)
            res = pos;
          else
            res = neg;
        } else {
          zeus::CAABox(neg).splitX(neg, pos);
          if (i & 1)
            res = pos;
          else
            res = neg;
        }
      } else {
        zeus::CAABox(neg).splitY(neg, pos);
        if (i & 2) {
          zeus::CAABox(pos).splitX(neg, pos);
          if (i & 1)
            res = pos;
          else
            res = neg;
        } else {
          zeus::CAABox(neg).splitX(neg, pos);
          if (i & 1)
            res = pos;
          else
            res = neg;
        }
      }

      OutputOctreeNode(os, r, BspNodeType(chFlags), res);
    }
  } else if (type == BspNodeType::Leaf) {
    zeus::CVector3f pos = aabb.center();
    zeus::CVector3f extent = aabb.extents();
    os.format(
        "obj = bpy.data.objects.new('Leaf', None)\n"
        "bpy.context.scene.collection.objects.link(obj)\n"
        "obj.location = (%f,%f,%f)\n"
        "obj.scale = (%f,%f,%f)\n"
        "obj.empty_display_type = 'CUBE'\n"
        "obj.layers[1] = True\n"
        "obj.layers[0] = False\n",
        pos.x, pos.y, pos.z, extent.x, extent.y, extent.z);
  }
}

static const uint32_t AROTChildCounts[] = {0, 2, 2, 4, 2, 4, 4, 8};

/* AROT octree dumper */
static void OutputOctreeNode(hecl::blender::PyOutStream& os, athena::io::IStreamReader& r, const zeus::CAABox& aabb) {
  r.readUint16Big();
  u16 flags = r.readUint16Big();
  if (flags) {
    u32 childCount = AROTChildCounts[flags];
    r.seek(2 * childCount);

    zeus::CAABox Z[2] = {aabb};
    if ((flags & 0x4) != 0)
      aabb.splitZ(Z[0], Z[1]);
    for (int k = 0; k < 1 + ((flags & 0x4) != 0); ++k) {
      zeus::CAABox Y[2] = {Z[k]};
      if ((flags & 0x2) != 0)
        Z[k].splitY(Y[0], Y[1]);
      for (int j = 0; j < 1 + ((flags & 0x2) != 0); ++j) {
        zeus::CAABox X[2] = {Y[j]};
        if ((flags & 0x1) != 0)
          Y[j].splitX(X[0], X[1]);
        for (int i = 0; i < 1 + ((flags & 0x1) != 0); ++i) {
          OutputOctreeNode(os, r, X[i]);
        }
      }
    }
  } else {
    zeus::CVector3f pos = aabb.center();
    zeus::CVector3f extent = aabb.extents();
    os.format(
        "obj = bpy.data.objects.new('Leaf', None)\n"
        "bpy.context.scene.collection.objects.link(obj)\n"
        "obj.location = (%f,%f,%f)\n"
        "obj.scale = (%f,%f,%f)\n"
        "obj.empty_display_type = 'CUBE'\n"
        "obj.layers[1] = True\n"
        "obj.layers[0] = False\n",
        pos.x, pos.y, pos.z, extent.x, extent.y, extent.z);
  }
}
#endif

bool MREA::Extract(const SpecBase& dataSpec, PAKEntryReadStream& rs, const hecl::ProjectPath& outPath,
                   PAKRouter<PAKBridge>& pakRouter, const PAK::Entry& entry, bool force, hecl::blender::Token& btok,
                   std::function<void(const char*)>) {
  using RigPair = std::pair<std::pair<UniqueID32, CSKR*>, std::pair<UniqueID32, CINF*>>;
  RigPair dummy = {};

  if (!force && outPath.isFile())
    return true;

  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  hecl::blender::Connection& conn = btok.getBlenderConnection();
  if (!conn.createBlend(outPath, hecl::blender::BlendType::Area))
    return false;

  /* Open Py Stream and read sections */
  hecl::blender::PyOutStream os = conn.beginPythonOut(true);
  os << "import bpy\n"
        "import bmesh\n"
        "from mathutils import Vector\n"
        "bpy.context.scene.render.fps = 60\n"
        "\n";
  os.format(FMT_STRING("bpy.context.scene.name = '{}'\n"), pakRouter.getBestEntryName(entry, false));
  DNACMDL::InitGeomBlenderContext(os, dataSpec.getMasterShaderPath());
  MaterialSet::RegisterMaterialProps(os);
  os << "# Clear Scene\n"
        "if len(bpy.data.collections):\n"
        "    bpy.data.collections.remove(bpy.data.collections[0])\n"
        "\n"
        "bpy.types.Light.retro_layer = bpy.props.IntProperty(name='Retro: Light Layer')\n"
        "bpy.types.Light.retro_origtype = bpy.props.IntProperty(name='Retro: Original Type')\n"
        "bpy.types.Object.retro_disable_enviro_visor = bpy.props.BoolProperty(name='Retro: Disable in Combat/Scan "
        "Visor')\n"
        "bpy.types.Object.retro_disable_thermal_visor = bpy.props.BoolProperty(name='Retro: Disable in Thermal "
        "Visor')\n"
        "bpy.types.Object.retro_disable_xray_visor = bpy.props.BoolProperty(name='Retro: Disable in X-Ray Visor')\n"
        "bpy.types.Object.retro_thermal_level = bpy.props.EnumProperty(items=[('COOL', 'Cool', 'Cool Temperature'),"
        "('HOT', 'Hot', 'Hot Temperature'),"
        "('WARM', 'Warm', 'Warm Temperature')],"
        "name='Retro: Thermal Visor Level')\n"
        "\n";

  /* One shared material set for all meshes */
  os << "# Materials\n"
        "materials = []\n"
        "\n";
  MaterialSet matSet;
  atUint64 secStart = rs.position();
  matSet.read(rs);
  matSet.readToBlender(os, pakRouter, entry, 0);
  rs.seek(secStart + head.secSizes[0], athena::SeekOrigin::Begin);
  std::vector<DNACMDL::VertexAttributes> vertAttribs;
  DNACMDL::GetVertexAttributes(matSet, vertAttribs);

  /* Read meshes */
  atUint32 curSec = 1;
  for (atUint32 m = 0; m < head.meshCount; ++m) {
    MeshHeader mHeader;
    secStart = rs.position();
    mHeader.read(rs);
    rs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);
    curSec += DNACMDL::ReadGeomSectionsToBlender<PAKRouter<PAKBridge>, MaterialSet, RigPair, DNACMDL::SurfaceHeader_1>(
        os, rs, pakRouter, entry, dummy, true, true, vertAttribs, m, head.secCount, 0, &head.secSizes[curSec]);
    os.format(FMT_STRING("obj.retro_disable_enviro_visor = {}\n"
                         "obj.retro_disable_thermal_visor = {}\n"
                         "obj.retro_disable_xray_visor = {}\n"
                         "obj.retro_thermal_level = '{}'\n"),
              mHeader.visorFlags.disableEnviro() ? "True" : "False",
              mHeader.visorFlags.disableThermal() ? "True" : "False",
              mHeader.visorFlags.disableXray() ? "True" : "False", mHeader.visorFlags.thermalLevelStr());
  }

  /* Skip AROT */
  secStart = rs.position();
  rs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Read SCLY layers */
  secStart = rs.position();
  SCLY scly;
  scly.read(rs);
  scly.exportToLayerDirectories(entry, pakRouter, force);
  rs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Read collision meshes */
  DeafBabe collision;
  secStart = rs.position();
  collision.read(rs);
  DeafBabe::BlenderInit(os);
  collision.sendToBlender(os);
  rs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Skip unknown section */
  rs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Read BABEDEAD Lights as Cycles emissives */
  secStart = rs.position();
  ReadBabeDeadToBlender_1_2(os, rs);
  rs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Dump VISI entities */
  secStart = rs.position();
  if (head.secSizes[curSec] && rs.readUint32Big() == 'VISI') {
    {
      rs.seek(secStart, athena::SeekOrigin::Begin);
      auto visiData = rs.readUBytes(head.secSizes[curSec]);
      athena::io::FileWriter visiOut(outPath.getWithExtension(".visi", true).getAbsolutePath());
      visiOut.writeUBytes(visiData.get(), head.secSizes[curSec]);
      rs.seek(secStart + 4, athena::SeekOrigin::Begin);
    }

    athena::io::YAMLDocWriter visiWriter("VISI");
    if (auto __vec = visiWriter.enterSubVector("entities")) {
      rs.seek(18, athena::SeekOrigin::Current);
      uint32_t entityCount = rs.readUint32Big();
      rs.seek(8, athena::SeekOrigin::Current);
      for (uint32_t i = 0; i < entityCount; ++i) {
        uint32_t entityId = rs.readUint32Big();
        visiWriter.writeUint32(entityId);
      }
    }
    hecl::ProjectPath visiMetadataPath(outPath.getParentPath(), "!visi.yaml");
    athena::io::FileWriter visiMetadata(visiMetadataPath.getAbsolutePath());
    visiWriter.finish(&visiMetadata);
  }

  /* Origins to center of mass */
  os << "bpy.context.view_layer.layer_collection.children['Collision'].hide_viewport = False\n"
        "bpy.ops.object.select_by_type(type='MESH')\n"
        "bpy.ops.object.origin_set(type='ORIGIN_CENTER_OF_MASS')\n"
        "bpy.ops.object.select_all(action='DESELECT')\n"
        "bpy.context.view_layer.layer_collection.children['Collision'].hide_viewport = True\n";

  /* Link MLVL scene as background */
  os.linkBackground(fmt::format(FMT_STRING("//../!world_{}.blend"), pakRouter.getCurrentBridge().getLevelId()),
                    "World"sv);

  os.centerView();
  os.close();
  return conn.saveBlend();
}

void MREA::Name(const SpecBase& dataSpec, PAKEntryReadStream& rs, PAKRouter<PAKBridge>& pakRouter, PAK::Entry& entry) {
  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  /* One shared material set for all meshes */
  atUint64 secStart = rs.position();
  MaterialSet matSet;
  matSet.read(rs);
  matSet.nameTextures(pakRouter, fmt::format(FMT_STRING("MREA_{}"), entry.id).c_str(), -1);
  rs.seek(secStart + head.secSizes[0], athena::SeekOrigin::Begin);

  /* Skip to SCLY */
  atUint32 curSec = 1;
  secStart = rs.position();
  while (curSec != head.sclySecIdx)
    secStart += head.secSizes[curSec++];
  rs.seek(secStart, athena::SeekOrigin::Begin);
  SCLY scly;
  scly.read(rs);
  scly.nameIDs(pakRouter);

  /* Skip to PATH */
  while (curSec != head.pathSecIdx)
    secStart += head.secSizes[curSec++];
  rs.seek(secStart, athena::SeekOrigin::Begin);

  UniqueID32 pathID(rs);
  const nod::Node* node;
  PAK::Entry* pathEnt = (PAK::Entry*)pakRouter.lookupEntry(pathID, &node);
  pathEnt->name = entry.name + "_path";
}

void MREA::MeshHeader::VisorFlags::setFromBlenderProps(const std::unordered_map<std::string, std::string>& props) {
  auto search = props.find("retro_disable_enviro_visor");
  if (search != props.cend() && search->second == "1")
    setDisableEnviro(true);
  search = props.find("retro_disable_thermal_visor");
  if (search != props.cend() && search->second == "1")
    setDisableThermal(true);
  search = props.find("retro_disable_xray_visor");
  if (search != props.cend() && search->second == "1")
    setDisableXray(true);
  search = props.find("retro_thermal_level");
  if (search != props.cend()) {
    if (search->second == "0")
      setThermalLevel(ThermalLevel::Cool);
    else if (search->second == "1")
      setThermalLevel(ThermalLevel::Hot);
    else if (search->second == "2")
      setThermalLevel(ThermalLevel::Warm);
  }
}

bool MREA::Cook(const hecl::ProjectPath& outPath, const hecl::ProjectPath& inPath,
                const std::vector<DNACMDL::Mesh>& meshes, const ColMesh& cMesh, const std::vector<Light>& lights,
                hecl::blender::Token& btok, const hecl::blender::Matrix4f* xf, bool pc) {
  /* Discover area layers */
  hecl::ProjectPath areaDirPath = inPath.getParentPath();
  std::vector<hecl::ProjectPath> layerScriptPaths;
  {
    hecl::DirectoryEnumerator dEnum(inPath.getParentPath().getAbsolutePath(),
                                    hecl::DirectoryEnumerator::Mode::DirsSorted, false, false, true);
    for (const hecl::DirectoryEnumerator::Entry& ent : dEnum) {
      hecl::ProjectPath layerScriptPath(areaDirPath, ent.m_name + "/!objects.yaml");
      if (layerScriptPath.isFile())
        layerScriptPaths.push_back(std::move(layerScriptPath));
    }
  }

  size_t secCount = 1 + meshes.size() * (pc ? 5 : 7); /* (materials, 5/7 fixed model secs) */

  /* tally up surfaces */
  for (const DNACMDL::Mesh& mesh : meshes)
    secCount += mesh.surfaces.size();

  /* Header */
  Header head = {};
  head.magic = 0xDEADBEEF;
  head.version = pc ? 0x1000F : 0xF;
  if (xf) {
    head.localToWorldMtx[0] = xf->val[0];
    head.localToWorldMtx[1] = xf->val[1];
    head.localToWorldMtx[2] = xf->val[2];
  } else {
    head.localToWorldMtx[0].simd[0] = 1.f;
    head.localToWorldMtx[1].simd[1] = 1.f;
    head.localToWorldMtx[2].simd[2] = 1.f;
  }
  head.meshCount = meshes.size();
  head.geomSecIdx = 0;
  head.arotSecIdx = secCount++;
  head.sclySecIdx = secCount++;
  head.collisionSecIdx = secCount++;
  head.unkSecIdx = secCount++;
  head.lightSecIdx = secCount++;
  head.visiSecIdx = secCount++;
  head.pathSecIdx = secCount++;
  head.secCount = secCount;

  std::vector<std::vector<uint8_t>> secs;
  secs.reserve(secCount + 2);

  /* Header section */
  {
    size_t secSz = 0;
    head.binarySize(secSz);
    secs.emplace_back(secSz, 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    head.write(w);
    int i = w.position();
    int end = ROUND_UP_32(i);
    for (; i < end; ++i)
      w.writeByte(0);
  }

  /* Sizes section */
  secs.emplace_back();

  /* Pre-emptively build full AABB and mesh AABBs in world coords */
  zeus::CAABox fullAabb;
  std::vector<zeus::CAABox> meshAabbs;
  meshAabbs.reserve(meshes.size());

  /* Models */
  if (pc) {
    if (!DNACMDL::WriteHMDLMREASecs<HMDLMaterialSet, DNACMDL::SurfaceHeader_2, MeshHeader>(secs, inPath, meshes,
                                                                                           fullAabb, meshAabbs))
      return false;
  } else {
    if (!DNACMDL::WriteMREASecs<MaterialSet, DNACMDL::SurfaceHeader_1, MeshHeader>(secs, inPath, meshes, fullAabb,
                                                                                   meshAabbs))
      return false;
  }

  /* AROT */
  {
    AROTBuilder arotBuilder;
    arotBuilder.build(secs, fullAabb, meshAabbs, meshes);

#if DUMP_OCTREE
    hecl::blender::Connection& conn = btok.getBlenderConnection();
    if (!conn.createBlend(inPath.getWithExtension(".octree.blend", true), hecl::blender::BlendType::Area))
      return false;

    /* Open Py Stream and read sections */
    hecl::blender::PyOutStream os = conn.beginPythonOut(true);
    os.format(
        "import bpy\n"
        "import bmesh\n"
        "from mathutils import Vector\n"
        "\n"
        "bpy.context.scene.name = '%s'\n",
        inPath.getLastComponent().data());

    athena::io::MemoryReader reader(secs.back().data(), secs.back().size());
    reader.readUint32Big();
    reader.readUint32Big();
    u32 numMeshBitmaps = reader.readUint32Big();
    u32 meshBitCount = reader.readUint32Big();
    u32 numNodes = reader.readUint32Big();
    auto aabbMin = reader.readVec3fBig();
    auto aabbMax = reader.readVec3fBig();
    reader.seekAlign32();
    reader.seek(ROUND_UP_32(meshBitCount) / 8 * numMeshBitmaps + numNodes * 4);
    zeus::CAABox arotAABB(aabbMin, aabbMax);
    OutputOctreeNode(os, reader, arotAABB);

    os.centerView();
    os.close();
    conn.saveBlend();
#endif
  }

  /* SCLY */
  DNAMP1::SCLY sclyData = {};
  {
    sclyData.fourCC = FOURCC('SCLY');
    sclyData.version = 1;
    for (const hecl::ProjectPath& layer : layerScriptPaths) {
      athena::io::FileReader freader(layer.getAbsolutePath());
      if (!freader.isOpen())
        continue;
      if (!athena::io::ValidateFromYAMLStream<DNAMP1::SCLY::ScriptLayer>(freader))
        continue;

      athena::io::YAMLDocReader reader;
      if (!reader.parse(&freader))
        continue;

      sclyData.layers.emplace_back();
      sclyData.layers.back().read(reader);
      size_t layerSize = 0;
      sclyData.layers.back().binarySize(layerSize);
      sclyData.layerSizes.push_back(layerSize);
    }
    sclyData.layerCount = sclyData.layers.size();

    size_t secSz = 0;
    sclyData.binarySize(secSz);
    secs.emplace_back(secSz, 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    sclyData.write(w);
  }

  /* Collision */
  {
    DeafBabe collision = {};
    DeafBabeBuildFromBlender(collision, cMesh);

#if DUMP_OCTREE
    hecl::blender::Connection& conn = btok.getBlenderConnection();
    if (!conn.createBlend(inPath.getWithExtension(".coctree.blend", true), hecl::blender::BlendType::Area))
      return false;

    /* Open Py Stream and read sections */
    hecl::blender::PyOutStream os = conn.beginPythonOut(true);
    os.format(
        "import bpy\n"
        "import bmesh\n"
        "from mathutils import Vector\n"
        "\n"
        "bpy.context.scene.name = '%s'\n",
        inPath.getLastComponent().data());

    athena::io::MemoryReader reader(collision.bspTree.get(), collision.bspSize);
    zeus::CAABox colAABB(collision.aabb[0], collision.aabb[1]);
    OutputOctreeNode(os, reader, collision.rootNodeType, colAABB);

    os.centerView();
    os.close();
    conn.saveBlend();
#endif

    size_t secSz = 0;
    collision.binarySize(secSz);
    secs.emplace_back(secSz, 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    collision.write(w);
  }

  /* Unk */
  {
    secs.emplace_back(8, 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    w.writeUint32Big(1);
  }

  /* Lights */
  std::vector<atVec3f> lightsVisi[2];
  {
    int actualCounts[2] = {};
    for (const Light& l : lights)
      if (l.layer == 0 || l.layer == 1)
        ++actualCounts[l.layer];
    lightsVisi[0].reserve(actualCounts[0]);
    lightsVisi[1].reserve(actualCounts[1]);

    secs.emplace_back(12 + 65 * (actualCounts[0] + actualCounts[1]), 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    w.writeUint32Big(0xBABEDEAD);

    for (uint32_t lay = 0; lay < 2; ++lay) {
      int lightCount = 0;
      for (const Light& l : lights) {
        if (l.layer == lay)
          ++lightCount;
      }
      w.writeUint32Big(lightCount);
      for (const Light& l : lights) {
        if (l.layer == lay) {
          BabeDeadLight light = {};
          WriteBabeDeadLightFromBlender(light, l);
          light.write(w);
          lightsVisi[l.layer].push_back(light.position);
        }
      }
    }
  }

  /* VISI */
  hecl::ProjectPath visiMetadataPath(areaDirPath, "!visi.yaml");
  bool visiGood = false;
  if (visiMetadataPath.isFile()) {
    athena::io::FileReader visiReader(visiMetadataPath.getAbsolutePath());
    athena::io::YAMLDocReader r;
    if (r.parse(&visiReader)) {
      size_t entityCount;
      std::vector<std::pair<uint16_t, zeus::CAABox>> entities;
      if (auto __vec = r.enterSubVector("entities", entityCount)) {
        entities.reserve(entityCount);
        for (size_t i = 0; i < entityCount; ++i) {
          uint32_t entityId = r.readUint32();
          for (const SCLY::ScriptLayer& layer : sclyData.layers) {
            for (const std::unique_ptr<IScriptObject>& obj : layer.objects) {
              if ((obj->id & ~0x03FF0000) == entityId) {
                zeus::CAABox entAABB = obj->getVISIAABB(btok);
                if (!entAABB.invalid())
                  entities.emplace_back(entityId, entAABB);
              }
            }
          }
        }
      }

      // Check if pre-generated visi exists, recycle if able
      hecl::ProjectPath preVisiPath = inPath.getWithExtension(".visi", true);
      if (preVisiPath.getPathType() == hecl::ProjectPath::Type::File) {
        athena::io::FileReader preVisiReader(preVisiPath.getAbsolutePath());
        atUint64 preVisiLen = preVisiReader.length();
        if (preVisiLen > 26) {
          auto preVisiData = preVisiReader.readUBytes(preVisiLen);
          athena::io::MemoryReader preVisiDataReader(preVisiData.get(), preVisiLen);

          atUint32 preVisiFourCC = preVisiDataReader.readUint32Big();
          atUint32 preVisiVersion = preVisiDataReader.readUint32Big();
          preVisiDataReader.readBool();
          preVisiDataReader.readBool();
          atUint32 preFeatureCount = preVisiDataReader.readUint32Big();
          atUint32 preLightsCount = preVisiDataReader.readUint32Big();
          atUint32 preLayer2LightCount = preVisiDataReader.readUint32Big();
          atUint32 preEntityCount = preVisiDataReader.readUint32Big();

          if (preVisiFourCC == 'VISI' && preVisiVersion == 2 && preFeatureCount == meshes.size() + entities.size() &&
              preLightsCount == lightsVisi[0].size() + lightsVisi[1].size() &&
              preLayer2LightCount == lightsVisi[1].size() && preEntityCount == entities.size()) {
            secs.emplace_back(preVisiLen, 0);
            memcpy(secs.back().data(), preVisiData.get(), preVisiLen);
            visiGood = true;
          } else {
            // TODO: Fix visigen and remove this hack
            secs.emplace_back(preVisiLen, 0);
            memcpy(secs.back().data(), preVisiData.get(), preVisiLen);
            visiGood = true;
          }
        }
      }
// TODO: fix visigen so this can be re-enabled
#if 0
#if !WINDOWS_STORE
      if (!visiGood) {
        hecl::ProjectPath visiIntOut = outPath.getWithExtension(".visiint");
        athena::io::FileWriter w(visiIntOut.getAbsolutePath());
        w.writeUint32Big(meshes.size());
        for (const DNACMDL::Mesh& mesh : meshes) {
          w.writeUint32Big(uint32_t(mesh.topology));

          w.writeUint32Big(mesh.pos.size());
          for (const auto& v : mesh.pos) {
            atVec3f xfPos = hecl::blender::MtxVecMul4RM(mesh.sceneXf, v);
            w.writeVec3fBig(xfPos);
          }

          w.writeUint32Big(mesh.surfaces.size());
          for (const DNACMDL::Mesh::Surface& surf : mesh.surfaces) {
            w.writeUint32Big(surf.verts.size());
            for (const DNACMDL::Mesh::Surface::Vert& vert : surf.verts)
              w.writeUint32Big(vert.iPos);
            const DNACMDL::Material& mat = mesh.materialSets[0][surf.materialIdx];
            w.writeBool(mat.blendMode != DNACMDL::Material::BlendMode::Opaque);
          }
        }

        w.writeUint32Big(entities.size());
        for (const auto& ent : entities) {
          w.writeUint32Big(ent.first);
          w.writeVec3fBig(ent.second.min);
          w.writeVec3fBig(ent.second.max);
        }

        w.writeUint32Big(lightsVisi[0].size() + lightsVisi[1].size());
        w.writeUint32Big(lightsVisi[1].size());
        for (const auto& light : lightsVisi[1])
          w.writeVec3fBig(light);
        for (const auto& light : lightsVisi[0])
          w.writeVec3fBig(light);

        w.close();

        std::string VisiGenPath = ExeDir + "/visigen";
#if _WIN32
        VisiGenPath += ".exe";
#endif
        std::string thrIdx = fmt::format(FMT_STRING("{}"), hecl::ClientProcess::GetThreadWorkerIdx());
        std::string parPid;
#if _WIN32
        parPid = fmt::format(FMT_STRING("{}"), reinterpret_cast<unsigned long long>(GetCurrentProcess()));
#else
        parPid = fmt::format(FMT_STRING("{}"), (unsigned long long)getpid());
#endif
        const char* args[] = {VisiGenPath.c_str(),
                                          visiIntOut.getAbsolutePath().data(),
                                          preVisiPath.getAbsolutePath().data(),
                                          thrIdx.c_str(),
                                          parPid.c_str(),
                                          nullptr};
        if (0 == hecl::RunProcess(VisiGenPath.c_str(), args)) {
          athena::io::FileReader r(preVisiPath.getAbsolutePath());
          size_t length = r.length();
          secs.emplace_back(length, 0);
          r.readBytesToBuf(secs.back().data(), length);
          visiGood = true;
        } else {
          Log.report(logvisor::Fatal, FMT_STRING("Unable to launch {}"), VisiGenPath);
        }
      }
#endif
#endif
    }
  }
  if (!visiGood)
    secs.emplace_back(4, 0);

  /* PATH */
  {
    const hecl::ProjectPath pathPath = GetPathBeginsWith(inPath.getParentPath(), "!path");
    UniqueID32 pathId;
    if (pathPath.isFile())
      pathId = pathPath;
    secs.emplace_back(4, 0);
    athena::io::MemoryWriter w(secs.back().data(), secs.back().size());
    pathId.write(w);
  }

  /* Assemble sizes and add padding */
  {
    std::vector<uint8_t>& sizesSec = secs[1];
    sizesSec.assign((((head.secCount) + 7) & ~7) * 4, 0);
    athena::io::MemoryWriter w(sizesSec.data(), sizesSec.size());
    for (auto it = secs.begin() + 2; it != secs.end(); ++it) {
      std::vector<uint8_t>& sec = *it;
      int i = sec.size();
      int end = ROUND_UP_32(i);
      sec.resize(end);
      w.writeUint32Big(end);
    }
  }

  /* Output all padded sections to file */
  athena::io::FileWriter writer(outPath.getAbsolutePath());
  for (const std::vector<uint8_t>& sec : secs)
    writer.writeUBytes(sec.data(), sec.size());

  return true;
}

} // namespace DataSpec::DNAMP1
