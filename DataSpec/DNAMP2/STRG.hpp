#pragma once

#include <unordered_map>
#include "DataSpec/DNACommon/PAK.hpp"
#include "DataSpec/DNACommon/STRG.hpp"

namespace DataSpec::DNAMP2 {

struct STRG : ISTRG {
  AT_DECL_EXPLICIT_DNA_YAMLV
  void _read(athena::io::IStreamReader& reader);
  std::vector<std::pair<FourCC, std::vector<std::u16string>>> langs;
  std::unordered_map<FourCC, std::vector<std::u16string>*> langMap;
  std::map<std::string, int32_t> names;

  int32_t lookupIdx(std::string_view name) const override {
    // TODO: Heterogeneous lookup when C++20 available
    auto search = names.find(name.data());
    if (search == names.end())
      return -1;
    return search->second;
  }

  size_t count() const override {
    size_t retval = 0;
    for (const auto& item : langs) {
      size_t sz = item.second.size();
      if (sz > retval)
        retval = sz;
    }
    return retval;
  }
  std::string getUTF8(const FourCC& lang, size_t idx) const override {
    auto search = langMap.find(lang);
    if (search != langMap.end())
      return hecl::Char16ToUTF8(search->second->at(idx));
    return std::string();
  }
  std::u16string getUTF16(const FourCC& lang, size_t idx) const override {
    auto search = langMap.find(lang);
    if (search != langMap.end())
      return search->second->at(idx);
    return std::u16string();
  }

  static bool Extract(PAKEntryReadStream& rs, const hecl::ProjectPath& outPath) {
    STRG strg;
    strg.read(rs);
    athena::io::TransactionalFileWriter writer(outPath.getAbsolutePath());
    athena::io::ToYAMLStream(strg, writer);
    return true;
  }

  static bool Cook(const hecl::ProjectPath& inPath, const hecl::ProjectPath& outPath) {
    STRG strg;
    athena::io::FileReader reader(inPath.getAbsolutePath());
    athena::io::FromYAMLStream(strg, reader);
    athena::io::TransactionalFileWriter ws(outPath.getAbsolutePath());
    strg.write(ws);
    return true;
  }
};

} // namespace DataSpec::DNAMP2
