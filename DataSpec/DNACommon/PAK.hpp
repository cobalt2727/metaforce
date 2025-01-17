#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "DataSpec/DNACommon/DNACommon.hpp"

#include <boo/ThreadLocalPtr.hpp>
#include <zeus/CMatrix4f.hpp>
#include <zeus/Global.hpp>

namespace DataSpec {

/** PAK entry stream reader */
class PAKEntryReadStream : public athena::io::IStreamReader {
  std::unique_ptr<atUint8[]> m_buf;
  atUint64 m_sz = 0;
  atUint64 m_pos = 0;

public:
  PAKEntryReadStream() = default;
  explicit operator bool() const { return m_buf.operator bool(); }
  PAKEntryReadStream(const PAKEntryReadStream& other) = delete;
  PAKEntryReadStream(PAKEntryReadStream&& other) = default;
  PAKEntryReadStream& operator=(const PAKEntryReadStream& other) = delete;
  PAKEntryReadStream& operator=(PAKEntryReadStream&& other) = default;
  PAKEntryReadStream(std::unique_ptr<atUint8[]>&& buf, atUint64 sz, atUint64 pos)
  : m_buf(std::move(buf)), m_sz(sz), m_pos(pos) {
    if (m_pos >= m_sz)
      LogDNACommon.report(logvisor::Fatal, FMT_STRING("PAK stream cursor overrun"));
  }
  void seek(atInt64 pos, athena::SeekOrigin origin) override {
    if (origin == athena::SeekOrigin::Begin) {
      m_pos = pos;
    } else if (origin == athena::SeekOrigin::Current) {
      m_pos += pos;
    } else if (origin == athena::SeekOrigin::End) {
      m_pos = m_sz + pos;
    }
    if (m_pos > m_sz) {
      LogDNACommon.report(logvisor::Fatal, FMT_STRING("PAK stream cursor overrun"));
    }
  }
  atUint64 position() const override { return m_pos; }
  atUint64 length() const override { return m_sz; }
  const atUint8* data() const { return m_buf.get(); }
  atUint64 readUBytesToBuf(void* buf, atUint64 len) override {
    atUint64 bufEnd = m_pos + len;
    if (bufEnd > m_sz)
      len -= bufEnd - m_sz;
    memmove(buf, m_buf.get() + m_pos, len);
    m_pos += len;
    return len;
  }
};

struct UniqueResult {
  enum class Type { NotFound, Pak, Level, Area, Layer } m_type = Type::NotFound;
  const std::string* m_levelName = nullptr;
  const std::string* m_areaName = nullptr;
  const std::string* m_layerName = nullptr;
  UniqueResult() = default;
  UniqueResult(Type tp) : m_type(tp) {}

  template <class PAKBRIDGE>
  void checkEntry(const PAKBRIDGE& pakBridge, const typename PAKBRIDGE::PAKType::Entry& entry);

  hecl::ProjectPath uniquePath(const hecl::ProjectPath& pakPath) const;
};

template <class BRIDGETYPE>
class PAKRouter;

/** Resource extractor type */
template <class PAKBRIDGE>
struct ResExtractor {
  std::function<bool(PAKEntryReadStream&, const hecl::ProjectPath&)> func_a;
  std::function<bool(const SpecBase&, PAKEntryReadStream&, const hecl::ProjectPath&, PAKRouter<PAKBRIDGE>&,
                     const typename PAKBRIDGE::PAKType::Entry&, bool, hecl::blender::Token&,
                     std::function<void(const char*)>)>
      func_b;
  std::array<const char*, 6> fileExts = {};
  unsigned weight = 0;
  std::function<void(const SpecBase&, PAKEntryReadStream&, PAKRouter<PAKBRIDGE>&, typename PAKBRIDGE::PAKType::Entry&)>
      func_name;

  ResExtractor() = default;

  ResExtractor(std::function<bool(PAKEntryReadStream&, const hecl::ProjectPath&)> func,
               std::array<const char*, 6>&& fileExtsIn, unsigned weightin = 0,
               std::function<void(const SpecBase&, PAKEntryReadStream&, PAKRouter<PAKBRIDGE>&,
                                  typename PAKBRIDGE::PAKType::Entry&)>
                   nfunc = {})
  : func_a(std::move(func)), fileExts(std::move(fileExtsIn)), weight(weightin), func_name(std::move(nfunc)) {}

  ResExtractor(std::function<bool(const SpecBase&, PAKEntryReadStream&, const hecl::ProjectPath&, PAKRouter<PAKBRIDGE>&,
                                  const typename PAKBRIDGE::PAKType::Entry&, bool, hecl::blender::Token&,
                                  std::function<void(const char*)>)>
                   func,
               std::array<const char*, 6>&& fileExtsIn, unsigned weightin = 0,
               std::function<void(const SpecBase&, PAKEntryReadStream&, PAKRouter<PAKBRIDGE>&,
                                  typename PAKBRIDGE::PAKType::Entry&)>
                   nfunc = {})
  : func_b(std::move(func)), fileExts(std::move(fileExtsIn)), weight(weightin), func_name(std::move(nfunc)) {}

  bool IsFullyExtracted(const hecl::ProjectPath& path) const {
    hecl::ProjectPath::Type tp = path.getPathType();
    if (tp == hecl::ProjectPath::Type::None)
      return false;
    else if (tp == hecl::ProjectPath::Type::Glob) {
      for (int i = 0; i < 6; ++i) {
        if (!fileExts[i])
          break;
        hecl::ProjectPath withExt = path.getWithExtension(fileExts[i], true);
        if (withExt.isNone())
          return false;
      }
    }
    return true;
  }
};

/** Level hierarchy representation */
template <class IDType>
struct Level {
  std::string name;
  struct Area {
    std::string name;
    struct Layer {
      std::string name;
      bool active;
      std::unordered_set<IDType> resources;
    };
    std::vector<Layer> layers;
    std::unordered_set<IDType> resources;
  };
  std::unordered_map<IDType, Area> areas;
  std::unordered_set<IDType> resources;
};

/** PAKRouter (for detecting shared entry locations) */
template <class BRIDGETYPE>
class PAKRouter : public PAKRouterBase {
public:
  using PAKType = typename BRIDGETYPE::PAKType;
  using IDType = typename PAKType::IDType;
  using EntryType = typename PAKType::Entry;

private:
  const std::vector<BRIDGETYPE>* m_bridges = nullptr;
  std::vector<std::pair<hecl::ProjectPath, hecl::ProjectPath>> m_bridgePaths;
  ThreadLocalPtr<void> m_curBridgeIdx;
  const hecl::ProjectPath& m_gameWorking;
  const hecl::ProjectPath& m_gameCooked;
  hecl::ProjectPath m_sharedWorking;
  hecl::ProjectPath m_sharedCooked;
  ThreadLocalPtr<const PAKType> m_pak;
  ThreadLocalPtr<const nod::Node> m_node;
  std::unordered_map<IDType, std::pair<size_t, const EntryType*>> m_uniqueEntries;
  std::unordered_map<IDType, std::pair<size_t, const EntryType*>> m_sharedEntries;
  std::unordered_map<IDType, hecl::ProjectPath> m_overrideEntries;
  CharacterAssociations<IDType> m_charAssoc;
  std::unordered_map<IDType, zeus::CMatrix4f> m_mapaTransforms;

  hecl::ProjectPath getCharacterWorking(const EntryType* entry) const;

public:
  PAKRouter(const SpecBase& dataSpec, const hecl::ProjectPath& working, const hecl::ProjectPath& cooked)
  : PAKRouterBase(dataSpec)
  , m_gameWorking(working)
  , m_gameCooked(cooked)
  , m_sharedWorking(working, "Shared")
  , m_sharedCooked(cooked, "Shared") {}

  void build(std::vector<BRIDGETYPE>& bridges, std::function<void(float)> progress);

  void enterPAKBridge(const BRIDGETYPE& pakBridge);
  const BRIDGETYPE& getCurrentBridge() const { return (*m_bridges)[reinterpret_cast<intptr_t>(m_curBridgeIdx.get())]; }

  using PAKRouterBase::getWorking;
  hecl::ProjectPath getWorking(const EntryType* entry, const ResExtractor<BRIDGETYPE>& extractor) const;
  hecl::ProjectPath getWorking(const EntryType* entry) const;
  hecl::ProjectPath getWorking(const IDType& id, bool silenceWarnings = false) const override;
  hecl::ProjectPath getCooked(const EntryType* entry) const;
  hecl::ProjectPath getCooked(const IDType& id, bool silenceWarnings = false) const;

  std::string getResourceRelativePath(const EntryType& a, const IDType& b) const;

  std::string getBestEntryName(const EntryType& entry, bool stdOverride = true) const;
  std::string getBestEntryName(const IDType& entry, bool stdOverride = true) const;

  bool extractResources(const BRIDGETYPE& pakBridge, bool force, hecl::blender::Token& btok,
                        std::function<void(const char*, float)> progress);

  const typename BRIDGETYPE::PAKType::Entry* lookupEntry(const IDType& entry, const nod::Node** nodeOut = nullptr,
                                                         bool silenceWarnings = false, bool currentPAK = false) const;

  template <typename DNA>
  bool lookupAndReadDNA(const IDType& id, DNA& out, bool silenceWarnings = false) {
    const nod::Node* node;
    const EntryType* entry = lookupEntry(id, &node, silenceWarnings);
    if (!entry)
      return false;
    PAKEntryReadStream rs = entry->beginReadStream(*node);
    out.read(rs);
    return true;
  }

  PAKEntryReadStream beginReadStreamForId(const IDType& id, bool silenceWarnings = false) {
    const nod::Node* node;
    const EntryType* entry = lookupEntry(id, &node, silenceWarnings);
    return entry->beginReadStream(*node);
  }

  const typename CharacterAssociations<IDType>::RigPair* lookupCMDLRigPair(const IDType& id) const;
  const typename CharacterAssociations<IDType>::MultimapIteratorPair
  lookupCharacterAttachmentRigs(const IDType& id) const;
  const zeus::CMatrix4f* lookupMAPATransform(const IDType& mapaId) const;

  hecl::ProjectPath getAreaLayerWorking(const IDType& areaId, int layerIdx) const;
  hecl::ProjectPath getAreaLayerWorking(const IDType& areaId, int layerIdx, bool& activeOut) const;
  hecl::ProjectPath getAreaLayerCooked(const IDType& areaId, int layerIdx) const;
  hecl::ProjectPath getAreaLayerCooked(const IDType& areaId, int layerIdx, bool& activeOut) const;

  void enumerateResources(const std::function<bool(const EntryType*)>& func);

  bool mreaHasDupeResources(const IDType& id) const;
};

} // namespace DataSpec
