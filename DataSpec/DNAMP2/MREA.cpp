#include <athena/FileWriter.hpp>
#include <lzokay.hpp>
#include "MREA.hpp"
#include "../DNAMP1/MREA.hpp"
#include "DataSpec/DNACommon/EGMC.hpp"
#include "DeafBabe.hpp"
#include "hecl/Blender/Connection.hpp"

namespace DataSpec {
extern hecl::Database::DataSpecEntry SpecEntMP2ORIG;

namespace DNAMP2 {

void MREA::StreamReader::nextBlock() {
  if (m_nextBlk >= m_blkCount)
    Log.report(logvisor::Fatal, FMT_STRING("MREA stream overrun"));

  BlockInfo& info = m_blockInfos[m_nextBlk++];

  /* Reallocate read buffer if needed */
  if (info.bufSize > m_compBufSz) {
    m_compBufSz = info.bufSize;
    m_compBuf.reset(new atUint8[m_compBufSz]);
  }

  /* Reallocate decompress buffer if needed */
  if (info.decompSize > m_decompBufSz) {
    m_decompBufSz = info.decompSize;
    m_decompBuf.reset(new atUint8[m_decompBufSz]);
  }

  if (info.compSize == 0) {
    /* Read uncompressed block */
    m_source.readUBytesToBuf(m_decompBuf.get(), info.decompSize);
  } else {
    /* Read compressed segments */
    atUint32 blockStart = ROUND_UP_32(info.compSize) - info.compSize;
    m_source.seek(blockStart);
    atUint32 rem = info.decompSize;
    atUint8* bufCur = m_decompBuf.get();
    while (rem) {
      atInt16 chunkSz = m_source.readInt16Big();
      if (chunkSz < 0) {
        chunkSz = -chunkSz;
        m_source.readUBytesToBuf(bufCur, chunkSz);
        bufCur += chunkSz;
        rem -= chunkSz;
      } else {
        m_source.readUBytesToBuf(m_compBuf.get(), chunkSz);
        size_t dsz;
        lzokay::decompress(m_compBuf.get(), chunkSz, bufCur, rem, dsz);
        bufCur += dsz;
        rem -= dsz;
      }
    }
  }

  m_posInBlk = 0;
  m_blkSz = info.decompSize;
}

MREA::StreamReader::StreamReader(athena::io::IStreamReader& source, atUint32 blkCount)
: m_compBufSz(0x4120)
, m_compBuf(new atUint8[0x4120])
, m_decompBufSz(0x4120)
, m_decompBuf(new atUint8[0x4120])
, m_source(source)
, m_blkCount(blkCount) {
  m_blockInfos.reserve(blkCount);
  for (atUint32 i = 0; i < blkCount; ++i) {
    BlockInfo& info = m_blockInfos.emplace_back();
    info.read(source);
    m_totalDecompLen += info.decompSize;
  }
  source.seekAlign32();
  m_blkBase = source.position();
  nextBlock();
}

void MREA::StreamReader::seek(atInt64 diff, athena::SeekOrigin whence) {
  atUint64 target = diff;
  if (whence == athena::SeekOrigin::Current) {
    target = m_pos + diff;
  } else if (whence == athena::SeekOrigin::End) {
    target = m_totalDecompLen - diff;
  }

  if (target >= m_totalDecompLen)
    Log.report(logvisor::Fatal, FMT_STRING("MREA stream seek overrun"));

  /* Determine which block contains position */
  atUint32 dAccum = 0;
  atUint32 cAccum = 0;
  atUint32 bIdx = 0;
  for (BlockInfo& info : m_blockInfos) {
    atUint32 newAccum = dAccum + info.decompSize;
    if (newAccum > target)
      break;
    dAccum = newAccum;
    if (info.compSize)
      cAccum += ROUND_UP_32(info.compSize);
    else
      cAccum += info.decompSize;
    ++bIdx;
  }

  /* Seek source if needed */
  if (bIdx != m_nextBlk - 1) {
    m_source.seek(m_blkBase + cAccum, athena::SeekOrigin::Begin);
    m_nextBlk = bIdx;
    nextBlock();
  }

  m_pos = target;
  m_posInBlk = target - dAccum;
}

void MREA::StreamReader::seekToSection(atUint32 sec, const std::vector<atUint32>& secSizes) {
  /* Determine which block contains section */
  atUint32 sAccum = 0;
  atUint32 dAccum = 0;
  atUint32 cAccum = 0;
  atUint32 bIdx = 0;
  for (BlockInfo& info : m_blockInfos) {
    atUint32 newSAccum = sAccum + info.secCount;
    if (newSAccum > sec)
      break;
    sAccum = newSAccum;
    dAccum += info.decompSize;
    if (info.compSize)
      cAccum += ROUND_UP_32(info.compSize);
    else
      cAccum += info.decompSize;
    ++bIdx;
  }

  /* Seek source if needed */
  if (bIdx != m_nextBlk - 1) {
    m_source.seek(m_blkBase + cAccum, athena::SeekOrigin::Begin);
    m_nextBlk = bIdx;
    nextBlock();
  }

  /* Seek within block */
  atUint32 target = dAccum;
  while (sAccum != sec)
    target += secSizes[sAccum++];

  m_pos = target;
  m_posInBlk = target - dAccum;
}

atUint64 MREA::StreamReader::readUBytesToBuf(void* buf, atUint64 len) {
  atUint8* bufCur = reinterpret_cast<atUint8*>(buf);
  atUint64 rem = len;
  while (rem) {
    atUint64 lRem = rem;
    atUint64 blkRem = m_blkSz - m_posInBlk;
    if (lRem > blkRem)
      lRem = blkRem;
    memcpy(bufCur, &m_decompBuf[m_posInBlk], lRem);
    bufCur += lRem;
    rem -= lRem;
    m_posInBlk += lRem;
    m_pos += lRem;
    if (rem)
      nextBlock();
  }
  return len;
}

void MREA::StreamReader::writeDecompInfos(athena::io::IStreamWriter& writer) const {
  for (const BlockInfo& info : m_blockInfos) {
    BlockInfo modInfo = info;
    modInfo.compSize = 0;
    modInfo.write(writer);
  }
}

bool MREA::Extract(const SpecBase& dataSpec, PAKEntryReadStream& rs, const hecl::ProjectPath& outPath,
                   PAKRouter<PAKBridge>& pakRouter, const DNAMP2::PAK::Entry& entry, bool force,
                   hecl::blender::Token& btok, std::function<void(const char*)>) {
  using RigPair = std::pair<std::pair<UniqueID32, CSKR*>, std::pair<UniqueID32, CINF*>>;
  RigPair dummy = {};

  if (!force && outPath.isFile())
    return true;

  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  /* MREA decompression stream */
  StreamReader drs(rs, head.compressedBlockCount);
  hecl::ProjectPath decompPath = outPath.getCookedPath(SpecEntMP2ORIG).getWithExtension(".decomp");
  decompPath.makeDirChain(false);
  athena::io::FileWriter mreaDecompOut(decompPath.getAbsolutePath());
  head.write(mreaDecompOut);
  mreaDecompOut.seekAlign32();
  drs.writeDecompInfos(mreaDecompOut);
  mreaDecompOut.seekAlign32();
  atUint64 decompLen = drs.length();
  mreaDecompOut.writeBytes(drs.readBytes(decompLen).get(), decompLen);
  mreaDecompOut.close();
  drs.seek(0, athena::SeekOrigin::Begin);

  /* Start up blender connection */
  hecl::blender::Connection& conn = btok.getBlenderConnection();
  if (!conn.createBlend(outPath, hecl::blender::BlendType::Area))
    return false;

  /* Calculate offset to EGMC section */
  atUint64 egmcOffset = 0;
  for (unsigned i = 0; i < head.egmcSecIdx; i++)
    egmcOffset += head.secSizes[i];

  /* Load EGMC if possible so we can assign meshes to scanIds */
  drs.seek(egmcOffset, athena::SeekOrigin::Begin);
  UniqueID32 egmcId(drs);
  DNACommon::EGMC egmc;
  pakRouter.lookupAndReadDNA(egmcId, egmc);

  drs.seek(0, athena::SeekOrigin::Begin);

  /* Open Py Stream and read sections */
  hecl::blender::PyOutStream os = conn.beginPythonOut(true);
  os.format(FMT_STRING("import bpy\n"
                       "import bmesh\n"
                       "from mathutils import Vector\n"
                       "\n"
                       "bpy.context.scene.name = '{}'\n"),
            pakRouter.getBestEntryName(entry, false));
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
  atUint64 secStart = drs.position();
  matSet.read(drs);
  matSet.readToBlender(os, pakRouter, entry, 0);
  drs.seek(secStart + head.secSizes[0], athena::SeekOrigin::Begin);
  std::vector<DNACMDL::VertexAttributes> vertAttribs;
  DNACMDL::GetVertexAttributes(matSet, vertAttribs);

  /* Read meshes */
  atUint32 curSec = 1;
  for (atUint32 m = 0; m < head.meshCount; ++m) {
    MeshHeader mHeader;
    secStart = drs.position();
    mHeader.read(drs);
    drs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);
    curSec += DNACMDL::ReadGeomSectionsToBlender<PAKRouter<PAKBridge>, MaterialSet, RigPair, DNACMDL::SurfaceHeader_2>(
        os, drs, pakRouter, entry, dummy, true, true, vertAttribs, m, head.secCount, 0, &head.secSizes[curSec]);
    os.format(FMT_STRING("obj.retro_disable_enviro_visor = {}\n"
                         "obj.retro_disable_thermal_visor = {}\n"
                         "obj.retro_disable_xray_visor = {}\n"
                         "obj.retro_thermal_level = '{}'\n"),
              mHeader.visorFlags.disableEnviro() ? "True" : "False",
              mHeader.visorFlags.disableThermal() ? "True" : "False",
              mHeader.visorFlags.disableXray() ? "True" : "False", mHeader.visorFlags.thermalLevelStr());

    /* Seek through AROT-relation sections */
    drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);
    drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);
  }

  /* Skip AROT */
  drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Skip BVH */
  drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Skip Bitmap */
  drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Skip SCLY (for now) */
  for (atUint32 l = 0; l < head.sclyLayerCount; ++l) {
    drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);
  }

  /* Skip SCGN (for now) */
  drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Read collision meshes */
  DeafBabe collision;
  secStart = drs.position();
  collision.read(drs);
  DeafBabe::BlenderInit(os);
  collision.sendToBlender(os);
  drs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Skip unknown section */
  drs.seek(head.secSizes[curSec++], athena::SeekOrigin::Current);

  /* Read BABEDEAD Lights as Cycles emissives */
  secStart = drs.position();
  DNAMP1::MREA::ReadBabeDeadToBlender_1_2(os, drs);
  drs.seek(secStart + head.secSizes[curSec++], athena::SeekOrigin::Begin);

  /* Origins to center of mass */
  os << "bpy.context.view_layer.layer_collection.children['Collision'].hide_viewport = False\n"
        "bpy.ops.object.select_by_type(type='MESH')\n"
        "bpy.ops.object.origin_set(type='ORIGIN_CENTER_OF_MASS')\n"
        "bpy.ops.object.select_all(action='DESELECT')\n"
        "bpy.context.view_layer.layer_collection.children['Collision'].hide_viewport = True\n";

  os.centerView();
  os.close();
  return conn.saveBlend();
}

UniqueID32 MREA::GetPATHId(PAKEntryReadStream& rs) {
  /* Do extract */
  Header head;
  head.read(rs);
  rs.seekAlign32();

  /* MREA decompression stream */
  StreamReader drs(rs, head.compressedBlockCount);

  /* Skip to PATH */
  drs.seekToSection(head.pathSecIdx, head.secSizes);
  return {drs};
}

} // namespace DNAMP2
} // namespace DataSpec
