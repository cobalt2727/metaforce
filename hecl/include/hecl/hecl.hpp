#pragma once

#ifndef _WIN32
#include <cstdlib>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>
#if __linux__ || __APPLE__
extern "C" int rep_closefrom(int lower);
#define closefrom rep_closefrom
#endif
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cwchar>
#include <cwctype>
#include <Shlwapi.h>
#include "winsupport.hpp"
#include <nowide/stackstring.hpp>
#endif

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <optional>

#include "FourCC.hpp"
#include "athena/Global.hpp"
#include "logvisor/logvisor.hpp"
#include "xxhash/xxhash.h"
#include "FourCC.hpp"


#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define HECL_NO_SANITIZE_THREAD __attribute__((no_sanitize("thread")))
#endif
#endif
#ifndef HECL_NO_SANITIZE_THREAD
#define HECL_NO_SANITIZE_THREAD
#endif

namespace hecl {
namespace Database {
class Project;
struct DataSpecEntry;
} // namespace Database

namespace blender {
enum class BlendType { None, Mesh, ColMesh, Armature, Actor, Area,
                       World, MapArea, MapUniverse, Frame, PathMesh };

class ANIMOutStream;
class Connection;
class DataStream;
class PyOutStream;
class Token;

struct Action;
struct Actor;
struct Armature;
struct Bone;
struct ColMesh;
struct Light;
struct MapArea;
struct MapUniverse;
struct Material;
struct Matrix3f;
struct Matrix4f;
struct Mesh;
struct PathMesh;
struct PoolSkinIndex;
struct World;

extern class Token SharedBlenderToken;
} // namespace blender

extern unsigned VerbosityLevel;
extern bool GuiMode;
extern logvisor::Module LogModule;

std::string Char16ToUTF8(std::u16string_view src);
std::u16string UTF8ToChar16(std::string_view src);

/* humanize_number port from FreeBSD's libutil */
enum class HNFlags { None = 0, Decimal = 0x01, NoSpace = 0x02, B = 0x04, Divisor1000 = 0x08, IECPrefixes = 0x10 };
ENABLE_BITWISE_ENUM(HNFlags)

enum class HNScale { None = 0, AutoScale = 0x20 };
ENABLE_BITWISE_ENUM(HNScale)

std::string HumanizeNumber(int64_t quotient, size_t len, const char* suffix, int scale, HNFlags flags);

static inline void ToLower(std::string& str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](char c) { return std::tolower(static_cast<unsigned char>(c)); });
}
static inline void ToUpper(std::string& str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](char c) { return std::toupper(static_cast<unsigned char>(c)); });
}

#if _WIN32
using Sstat = struct ::_stat64;
#else
using Sstat = struct stat;
#endif

constexpr size_t StrLen(const char* str) { return std::char_traits<char>::length(str); }

void SanitizePath(std::string& path);

inline void Unlink(const char* file) {
#if _WIN32
  const nowide::wstackstring wfile(file);
  _wunlink(wfile.get());
#else
  unlink(file);
#endif
}

inline void MakeDir(const char* dir) {
#if _WIN32
  HRESULT err;
  const nowide::wstackstring wdir(dir);
  if (!CreateDirectoryW(wdir.get(), NULL))
    if ((err = GetLastError()) != ERROR_ALREADY_EXISTS)
      LogModule.report(logvisor::Fatal, FMT_STRING("MakeDir({})"), dir);
#else
  if (mkdir(dir, 0755))
    if (errno != EEXIST)
      LogModule.report(logvisor::Fatal, FMT_STRING("MakeDir({}): {}"), dir, strerror(errno));
#endif
}

int RecursiveMakeDir(const char* dir);

inline std::optional<std::string> GetEnv(const char* name) {
#if WINDOWS_STORE
  return nullptr;
#else
#if _WIN32
  size_t sz = 0;
  const nowide::wshort_stackstring wname(name);
  _wgetenv_s(&sz, nullptr, 0, wname.get());
  if (sz == 0) {
    return {};
  }
  auto wbuf = std::make_unique<wchar_t[]>(sz);
  _wgetenv_s(&sz, wbuf.get(), sz, wname.get());
  return nowide::narrow(wbuf.get(), sz - 1); // null-terminated
#else
  const auto* env = getenv(name);
  if (env != nullptr) {
    return env;
  }
  return {};
#endif
#endif
}

inline char* Getcwd(char* buf, int maxlen) {
#if _WIN32
  auto wbuf = std::make_unique<wchar_t[]>(maxlen);
  wchar_t* result = _wgetcwd(wbuf.get(), maxlen);
  if (result == nullptr) {
    return nullptr;
  }
  return nowide::narrow(buf, maxlen, wbuf.get());
#else
  return getcwd(buf, maxlen);
#endif
}

std::string GetcwdStr();

inline bool IsAbsolute(std::string_view path) {
#if _WIN32
  if (path.size() && (path[0] == '\\' || path[0] == '/'))
    return true;
  if (path.size() >= 2 && iswalpha(path[0]) && path[1] == ':')
    return true;
#else
  if (path.size() && path[0] == '/')
    return true;
#endif
  return false;
}

std::string GetTmpDir();

#if !WINDOWS_STORE
int RunProcess(const char* path, const char* const args[]);
#endif

enum class FileLockType { None = 0, Read, Write };
inline FILE* Fopen(const char* path, const char* mode, FileLockType lock = FileLockType::None) {
#if _WIN32
  const nowide::wstackstring wpath(path);
  const nowide::wshort_stackstring wmode(mode);
  FILE* fp = _wfopen(wpath.get(), wmode.get());
  if (!fp)
    return nullptr;
#else
  FILE* fp = fopen(path, mode);
  if (!fp)
    return nullptr;
#endif

  if (lock != FileLockType::None) {
#if _WIN32
    OVERLAPPED ov = {};
    LockFileEx((HANDLE)(uintptr_t)_fileno(fp), (lock == FileLockType::Write) ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 0, 1,
               &ov);
#else
    if (flock(fileno(fp), ((lock == FileLockType::Write) ? LOCK_EX : LOCK_SH) | LOCK_NB))
      LogModule.report(logvisor::Error, FMT_STRING("flock {}: {}"), path, strerror(errno));
#endif
  }

  return fp;
}

struct UniqueFileDeleter {
  void operator()(FILE* file) const noexcept { std::fclose(file); }
};
using UniqueFilePtr = std::unique_ptr<FILE, UniqueFileDeleter>;

inline UniqueFilePtr FopenUnique(const char* path, const char* mode,
                                 FileLockType lock = FileLockType::None) {
  return UniqueFilePtr{Fopen(path, mode, lock)};
}

inline int FSeek(FILE* fp, int64_t offset, int whence) {
#if _WIN32
  return _fseeki64(fp, offset, whence);
#elif __APPLE__ || __FreeBSD__
  return fseeko(fp, offset, whence);
#else
  return fseeko64(fp, offset, whence);
#endif
}

inline int64_t FTell(FILE* fp) {
#if _WIN32
  return _ftelli64(fp);
#elif __APPLE__ || __FreeBSD__
  return ftello(fp);
#else
  return ftello64(fp);
#endif
}

inline int Rename(const char* oldpath, const char* newpath) {
#if _WIN32
  const nowide::wstackstring woldpath(oldpath);
  const nowide::wstackstring wnewpath(newpath);
  return MoveFileExW(woldpath.get(), wnewpath.get(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0;
#else
  return rename(oldpath, newpath);
#endif
}

inline int Stat(const char* path, Sstat* statOut) {
#if _WIN32
  size_t pos;
  const nowide::wstackstring wpath(path);
  const wchar_t* wpathP = wpath.get();
  for (pos = 0; pos < 3 && wpathP[pos] != L'\0'; ++pos) {}
  if (pos == 2 && wpathP[1] == L':') {
    wchar_t fixPath[4] = {wpathP[0], L':', L'/', L'\0'};
    return _wstat64(fixPath, statOut);
  }
  return _wstat64(wpath.get(), statOut);
#else
  return stat(path, statOut);
#endif
}

inline int StrCmp(const char* str1, const char* str2) {
  if (!str1 || !str2)
    return str1 != str2;
  return strcmp(str1, str2);
}

inline int StrNCmp(const char* str1, const char* str2, size_t count) {
  if (!str1 || !str2)
    return str1 != str2;

  return std::char_traits<char>::compare(str1, str2, count);
}

inline int StrCaseCmp(const char* str1, const char* str2) {
  if (!str1 || !str2)
    return str1 != str2;
#if _WIN32
  return _stricmp(str1, str2);
#else
  return strcasecmp(str1, str2);
#endif
}

inline unsigned long StrToUl(const char* str, char** endPtr, int base) {
  return strtoul(str, endPtr, base);
}

inline bool PathRelative(const char* path) {
  if (!path || !path[0])
    return false;
#if _WIN32 && !WINDOWS_STORE
  const nowide::wstackstring wpath(path);
  return PathIsRelativeW(wpath.get());
#else
  return path[0] != '/';
#endif
}

inline int ConsoleWidth(bool* ok = nullptr) {
  int retval = 80;
#if _WIN32
#if !WINDOWS_STORE
  CONSOLE_SCREEN_BUFFER_INFO info;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
  retval = info.dwSize.X;
  if (ok)
    *ok = true;
#endif
#else
  if (ok)
    *ok = false;
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1) {
    retval = w.ws_col;
    if (ok)
      *ok = true;
  }
#endif
  if (retval < 10)
    return 10;
  return retval;
}

class MultiProgressPrinter;
class ProjectRootPath;

/**
 * @brief Hash representation used for all storable and comparable objects
 *
 * Hashes are used within HECL to avoid redundant storage of objects;
 * providing a rapid mechanism to compare for equality.
 */
class Hash {
protected:
  uint64_t hash = 0;

public:
  constexpr Hash() noexcept = default;
  constexpr Hash(const Hash&) noexcept = default;
  constexpr Hash(Hash&&) noexcept = default;
  constexpr Hash(uint64_t hashin) noexcept : hash(hashin) {}
  explicit Hash(const void* buf, size_t len) noexcept : hash(XXH64(buf, len, 0)) {}
  explicit Hash(std::string_view str) noexcept : hash(XXH64(str.data(), str.size(), 0)) {}

  constexpr uint32_t val32() const noexcept { return uint32_t(hash) ^ uint32_t(hash >> 32); }
  constexpr uint64_t val64() const noexcept { return uint64_t(hash); }
  constexpr size_t valSizeT() const noexcept { return size_t(hash); }
  template <typename T>
  constexpr T valT() const noexcept;

  constexpr Hash& operator=(const Hash& other) noexcept = default;
  constexpr Hash& operator=(Hash&& other) noexcept = default;
  constexpr bool operator==(const Hash& other) const noexcept { return hash == other.hash; }
  constexpr bool operator!=(const Hash& other) const noexcept { return !operator==(other); }
  constexpr bool operator<(const Hash& other) const noexcept { return hash < other.hash; }
  constexpr bool operator>(const Hash& other) const noexcept { return hash > other.hash; }
  constexpr bool operator<=(const Hash& other) const noexcept { return hash <= other.hash; }
  constexpr bool operator>=(const Hash& other) const noexcept { return hash >= other.hash; }
  constexpr explicit operator bool() const noexcept { return hash != 0; }
};
template <>
constexpr uint32_t Hash::valT<uint32_t>() const noexcept {
  return val32();
}
template <>
constexpr uint64_t Hash::valT<uint64_t>() const noexcept {
  return val64();
}

/**
 * @brief Timestamp representation used for comparing modtimes of cooked resources
 */
class Time final {
  time_t ts;

public:
  Time() : ts(std::time(nullptr)) {}
  constexpr Time(time_t ti) noexcept : ts{ti} {}
  constexpr Time(const Time& other) noexcept : ts{other.ts} {}
  [[nodiscard]] constexpr time_t getTs() const { return ts; }
  constexpr Time& operator=(const Time& other) noexcept {
    ts = other.ts;
    return *this;
  }
  [[nodiscard]] constexpr bool operator==(const Time& other) const noexcept { return ts == other.ts; }
  [[nodiscard]] constexpr bool operator!=(const Time& other) const noexcept { return ts != other.ts; }
  [[nodiscard]] constexpr bool operator<(const Time& other) const noexcept { return ts < other.ts; }
  [[nodiscard]] constexpr bool operator>(const Time& other) const noexcept { return ts > other.ts; }
  [[nodiscard]] constexpr bool operator<=(const Time& other) const noexcept { return ts <= other.ts; }
  [[nodiscard]] constexpr bool operator>=(const Time& other) const noexcept { return ts >= other.ts; }
};

/**
 * @brief Case-insensitive comparator for std::map sorting
 */
struct CaseInsensitiveCompare {
  // Allow heterogeneous lookup with maps that use this comparator.
  using is_transparent = void;

  bool operator()(std::string_view lhs, std::string_view rhs) const {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char lhs, char rhs) {
      return std::tolower(static_cast<unsigned char>(lhs)) < std::tolower(static_cast<unsigned char>(rhs));
    });
  }
};

/**
 * @brief Directory traversal tool for accessing sorted directory entries
 */
class DirectoryEnumerator {
public:
  enum class Mode { Native, DirsSorted, FilesSorted, DirsThenFilesSorted };
  struct Entry {
    std::string m_path;
    std::string m_name;
    size_t m_fileSz;
    bool m_isDir;

    Entry(std::string path, std::string name, size_t sz, bool isDir)
    : m_path(std::move(path)), m_name(std::move(name)), m_fileSz(sz), m_isDir(isDir) {}
  };

private:
  std::vector<Entry> m_entries;

public:
  DirectoryEnumerator(std::string_view path, Mode mode = Mode::DirsThenFilesSorted, bool sizeSort = false,
                      bool reverse = false, bool noHidden = false);

  explicit operator bool() const { return !m_entries.empty(); }
  [[nodiscard]] size_t size() const { return m_entries.size(); }
  [[nodiscard]] std::vector<Entry>::const_iterator begin() const { return m_entries.cbegin(); }
  [[nodiscard]] std::vector<Entry>::const_iterator end() const { return m_entries.cend(); }
};

/**
 * @brief Special ProjectRootPath class for opening Database::Project instances
 *
 * Constructing a ProjectPath requires supplying a ProjectRootPath to consistently
 * resolve canonicalized relative paths.
 */
class ProjectRootPath {
  std::string m_projRoot;
  Hash m_hash = 0;

public:
  /**
   * @brief Empty constructor
   *
   * Used to preallocate ProjectPath for later population using assign()
   */
  ProjectRootPath() = default;

  /**
   * @brief Tests for non-empty project root path
   */
  explicit operator bool() const { return m_projRoot.size() != 0; }

  /**
   * @brief Construct a representation of a project root path
   * @param path valid filesystem-path (relative or absolute) to project root
   */
  ProjectRootPath(std::string_view path) : m_projRoot(path) {
    SanitizePath(m_projRoot);
    m_hash = Hash(m_projRoot);
  }

  /**
   * @brief Access fully-canonicalized absolute path
   * @return Absolute path reference
   */
  std::string_view getAbsolutePath() const { return m_projRoot; }

  /**
   * @brief Make absolute path project relative
   * @param absPath Absolute path
   * @return std::string of path relative to project root
   */
  std::string getProjectRelativeFromAbsolute(std::string_view absPath) const {
    if (absPath.size() > m_projRoot.size()) {
      std::string absPathForward(absPath);
      for (char& ch : absPathForward)
        if (ch == '\\')
          ch = '/';
      if (!absPathForward.compare(0, m_projRoot.size(), m_projRoot)) {
        auto beginIt = absPathForward.cbegin() + m_projRoot.size();
        while (*beginIt == '/')
          ++beginIt;
        return std::string(beginIt, absPathForward.cend());
      }
    }
    LogModule.report(logvisor::Fatal, FMT_STRING("unable to resolve '{}' as project relative '{}'"), absPath,
                     m_projRoot);
    return std::string();
  }

  /**
   * @brief Create directory at path
   *
   * Fatal log report is issued if directory is not able to be created or doesn't already exist.
   * If directory already exists, no action taken.
   */
  void makeDir() const { MakeDir(m_projRoot.c_str()); }

  /**
   * @brief HECL-specific xxhash
   * @return unique hash value
   */
  Hash hash() const noexcept { return m_hash; }
  bool operator==(const ProjectRootPath& other) const noexcept { return m_hash == other.m_hash; }
  bool operator!=(const ProjectRootPath& other) const noexcept { return !operator==(other); }

  /**
   * @brief Obtain c-string of final path component
   * @return Final component c-string (may be empty)
   */
  std::string_view getLastComponent() const {
    size_t pos = m_projRoot.rfind('/');
    if (pos == std::string::npos)
      return {};
    return {m_projRoot.c_str() + pos + 1, size_t(m_projRoot.size() - pos - 1)};
  }
};

/**
 * @brief Canonicalized project path representation using POSIX conventions
 *
 * HECL uses POSIX-style paths (with '/' separator) and directory tokens
 * ('.','..') to resolve files within a project. The database internally
 * uses this representation to track working files.
 *
 * This class provides a convenient way to resolve paths relative to the
 * project root. Part of this representation involves resolving symbolic
 * links to regular file/directory paths and determining its type.
 *
 * NOTE THAT PROJECT PATHS ARE TREATED AS CASE SENSITIVE!!
 */
class ProjectPath {
  Database::Project* m_proj = nullptr;
  std::string m_absPath;
  std::string m_relPath;
  std::string m_auxInfo;
  Hash m_hash = 0;
  void ComputeHash() {
    if (m_auxInfo.size())
      m_hash = Hash(m_relPath + '|' + m_auxInfo);
    else
      m_hash = Hash(m_relPath);
  }

public:
  /**
   * @brief Empty constructor
   *
   * Used to preallocate ProjectPath for later population using assign()
   */
  ProjectPath() = default;

  /**
   * @brief Tests for non-empty project path
   */
  explicit operator bool() const { return m_absPath.size() != 0; }

  /**
   * @brief Clears path
   */
  void clear() {
    m_proj = nullptr;
    m_absPath.clear();
    m_relPath.clear();
    m_hash = 0;
  }

  /**
   * @brief Construct a project subpath representation within a project's root path
   * @param project previously constructed Project to use root path of
   * @param path valid filesystem-path (relative or absolute) to subpath
   */
  ProjectPath(Database::Project& project, std::string_view path) { assign(project, path); }
  void assign(Database::Project& project, std::string_view path);

  /**
   * @brief Construct a project subpath representation within another subpath
   * @param parentPath previously constructed ProjectPath which ultimately connects to a ProjectRootPath
   * @param path valid filesystem-path (relative or absolute) to subpath
   */
  ProjectPath(const ProjectPath& parentPath, std::string_view path) { assign(parentPath, path); }
  void assign(const ProjectPath& parentPath, std::string_view path);

  /**
   * @brief Determine if ProjectPath represents project root directory
   * @return true if project root directory
   */
  bool isRoot() const { return m_relPath.empty(); }

  /**
   * @brief Return new ProjectPath with extension added
   * @param ext file extension to add (nullptr may be passed to remove the extension)
   * @param replace remove existing extension (if any) before appending new extension
   * @return new path with extension
   */
  ProjectPath getWithExtension(const char* ext, bool replace = false) const;

  /**
   * @brief Access fully-canonicalized absolute path
   * @return Absolute path reference
   */
  std::string_view getAbsolutePath() const { return m_absPath; }

  /**
   * @brief Access fully-canonicalized project-relative path
   * @return Relative pointer to within absolute-path or "." for project root-directory (use isRoot to detect)
   */
  std::string_view getRelativePath() const {
    if (m_relPath.size())
      return m_relPath;
    static const std::string dot = ".";
    return dot;
  }

  /**
   * @brief Obtain cooked equivalent of this ProjectPath
   * @param spec DataSpec to get path against
   * @return Cooked representation path
   */
  ProjectPath getCookedPath(const Database::DataSpecEntry& spec) const;

  /**
   * @brief Obtain path of parent entity (a directory for file paths)
   * @return Parent Path
   *
   * This will not resolve outside the project root (error in that case)
   */
  ProjectPath getParentPath() const {
    if (m_relPath == ".")
      LogModule.report(logvisor::Fatal, FMT_STRING("attempted to resolve parent of root project path"));
    size_t pos = m_relPath.rfind('/');
    if (pos == std::string::npos)
      return ProjectPath(*m_proj, "");
    return ProjectPath(*m_proj, std::string(m_relPath.begin(), m_relPath.begin() + pos));
  }

  /**
   * @brief Obtain c-string of final path component (stored within relative path)
   * @return Final component c-string (may be empty)
   */
  std::string_view getLastComponent() const {
    size_t pos = m_relPath.rfind('/');
    if (pos == std::string::npos)
      return m_relPath;
    return {m_relPath.c_str() + pos + 1, m_relPath.size() - pos - 1};
  }

  /**
   * @brief Obtain c-string of extension of final path component (stored within relative path)
   * @return Final component extension c-string (may be empty)
   */
  std::string_view getLastComponentExt() const {
    std::string_view lastCompOrig = getLastComponent().data();
    const char* end = lastCompOrig.data() + lastCompOrig.size();
    const char* lastComp = end;
    while (lastComp != lastCompOrig.data()) {
      if (*lastComp == '.')
        return {lastComp + 1, size_t(end - lastComp - 1)};
      --lastComp;
    }
    return {};
  }

  /**
   * @brief Build vector of project-relative directory/file components
   * @return Vector of path components
   */
  std::vector<std::string> getPathComponents() const {
    std::vector<std::string> ret;
    if (m_relPath.empty())
      return ret;
    auto it = m_relPath.cbegin();
    if (*it == '/') {
      ret.push_back("/");
      ++it;
    }
    std::string comp;
    for (; it != m_relPath.cend(); ++it) {
      if (*it == '/') {
        if (comp.empty())
          continue;
        ret.push_back(std::move(comp));
        comp.clear();
        continue;
      }
      comp += *it;
    }
    if (comp.size())
      ret.push_back(std::move(comp));
    return ret;
  }

  std::string_view getAuxInfo() const { return m_auxInfo; }

  /**
   * @brief Construct a path with the aux info overwritten with specified string
   * @param auxStr string to replace existing auxInfo with
   */
  ProjectPath ensureAuxInfo(std::string_view auxStr) const {
    if (auxStr.empty())
      return ProjectPath(getProject(), getRelativePath());
    else
      return ProjectPath(getProject(), std::string(getRelativePath()) + '|' + auxStr.data());
  }

  template<typename StringT>
  class EncodableString {
    friend class ProjectPath;
    using EncStringView = std::basic_string_view<typename StringT::value_type>;
    StringT m_ownedString;
    EncStringView m_stringView;
    EncodableString(StringT s) : m_ownedString(std::move(s)), m_stringView(m_ownedString) {}
    EncodableString(EncStringView sv) : m_stringView(sv) {}
    EncodableString(const EncodableString&) = delete;
    EncodableString& operator=(const EncodableString&) = delete;
    EncodableString(EncodableString&&) = delete;
    EncodableString& operator=(EncodableString&&) = delete;
  public:
    operator EncStringView() const { return m_stringView; }
  };

  EncodableString<std::string> getEncodableString() const {
    if (!getAuxInfo().empty())
      return {std::string(getRelativePath()) + '|' + getAuxInfo().data()};
    else
      return {getRelativePath()};
  }

  /**
   * @brief Type of path
   */
  enum class Type {
    None,      /**< If path doesn't reference a valid filesystem entity, this is returned */
    File,      /**< Singular file path (confirmed with filesystem) */
    Directory, /**< Singular directory path (confirmed with filesystem) */
    Glob,      /**< Glob-path (whenever one or more '*' occurs in syntax) */
  };

  /**
   * @brief Get type of path based on syntax and filesystem queries
   * @return Type of path
   */
  Type getPathType() const;

  /**
   * @brief Test if nothing exists at path
   * @return True if nothing exists at path
   */
  bool isNone() const { return getPathType() == Type::None; }

  /**
   * @brief Test if regular file exists at path
   * @return True if regular file exists at path
   */
  bool isFile() const { return getPathType() == Type::File; }

  /**
   * @brief Test if directory exists at path
   * @return True if directory exists at path
   */
  bool isDirectory() const { return getPathType() == Type::Directory; }

  /**
   * @brief Certain singular resource targets are cooked based on this test
   * @return True if file or glob
   */
  bool isFileOrGlob() const {
    Type type = getPathType();
    return (type == Type::File || type == Type::Glob);
  }

  /**
   * @brief Get time of last modification with special behaviors for directories and glob-paths
   * @return Time object representing entity's time of last modification
   *
   * Regular files simply return their modtime as queried from the OS
   * Directories return the latest modtime of all first-level regular files
   * Glob-paths return the latest modtime of all matched regular files
   */
  Time getModtime() const;

  /**
   * @brief Insert directory children into list
   * @param outPaths list to append children to
   */
  void getDirChildren(std::map<std::string, ProjectPath>& outPaths) const;

  /**
   * @brief Construct DirectoryEnumerator set to project path
   */
  hecl::DirectoryEnumerator enumerateDir() const;

  /**
   * @brief Insert glob matches into existing vector
   * @param outPaths Vector to add matches to (will not erase existing contents)
   */
  void getGlobResults(std::vector<ProjectPath>& outPaths) const;

  /**
   * @brief Count how many directory levels deep in project path is
   * @return Level Count
   */
  size_t levelCount() const {
    size_t count = 0;
    for (char ch : m_relPath)
      if (ch == '/' || ch == '\\')
        ++count;
    return count;
  }

  /**
   * @brief Create directory at path
   *
   * Fatal log report is issued if directory is not able to be created or doesn't already exist.
   * If directory already exists, no action taken.
   */
  void makeDir() const { MakeDir(m_absPath.c_str()); }

  /**
   * @brief Create directory chain leading up to path
   * @param includeLastComp if set, the ProjectPath is assumed to be a
   *                        directory, creating the final component
   */
  void makeDirChain(bool includeLastComp) const {
    std::vector<std::string> comps = getPathComponents();
    auto end = comps.cend();
    if (end != comps.cbegin() && !includeLastComp)
      --end;
    ProjectPath compPath(*m_proj, ".");
    for (auto it = comps.cbegin(); it != end; ++it) {
      compPath = ProjectPath(compPath, *it);
      compPath.makeDir();
    }
  }

  /**
   * @brief Fetch project that contains path
   * @return Project
   */
  Database::Project& getProject() const {
    if (!m_proj)
      LogModule.report(logvisor::Fatal, FMT_STRING("ProjectPath::getProject() called on unqualified path"));
    return *m_proj;
  }

  /**
   * @brief HECL-specific xxhash
   * @return unique hash value
   */
  Hash hash() const noexcept { return m_hash; }
  bool operator==(const ProjectPath& other) const noexcept { return m_hash == other.m_hash; }
  bool operator!=(const ProjectPath& other) const noexcept { return !operator==(other); }

  uint32_t parsedHash32() const;
};

/**
 * @brief Handy functions not directly provided via STL strings
 */
class StringUtils {
public:
  static bool BeginsWith(std::string_view str, std::string_view test) {
    if (test.size() > str.size())
      return false;
    return str.compare(0, test.size(), test) == 0;
  }

  static bool EndsWith(std::string_view str, std::string_view test) {
    if (test.size() > str.size())
      return false;
    return str.compare(str.size() - test.size(), std::string_view::npos, test) == 0;
  }

  static std::string TrimWhitespace(std::string_view str) {
    auto bit = str.begin();
    while (bit != str.cend() && std::isspace(static_cast<unsigned char>(*bit)))
      ++bit;
    auto eit = str.end();
    while (eit != str.cbegin() && std::isspace(static_cast<unsigned char>(*(eit - 1))))
      --eit;
    return {bit, eit};
  }
};

/**
 * @brief Mutex-style centralized resource-path tracking
 *
 * Provides a means to safely parallelize resource processing; detecting when another
 * thread is working on the same resource.
 */
class ResourceLock {
  static bool SetThreadRes(const ProjectPath& path);
  static void ClearThreadRes();
  bool good;

public:
  explicit operator bool() const { return good; }
  static bool InProgress(const ProjectPath& path);
  explicit ResourceLock(const ProjectPath& path) : good{SetThreadRes(path)} {}
  ~ResourceLock() {
    if (good)
      ClearThreadRes();
  }
  ResourceLock(const ResourceLock&) = delete;
  ResourceLock& operator=(const ResourceLock&) = delete;
  ResourceLock(ResourceLock&&) = delete;
  ResourceLock& operator=(ResourceLock&&) = delete;
};

/**
 * @brief Search from within provided directory for the project root
 * @param path absolute or relative file path to search from
 * @return Newly-constructed root path (bool-evaluating to false if not found)
 */
ProjectRootPath SearchForProject(std::string_view path);

/**
 * @brief Search from within provided directory for the project root
 * @param path absolute or relative file path to search from
 * @param subpathOut remainder of provided path assigned to this ProjectPath
 * @return Newly-constructed root path (bool-evaluating to false if not found)
 */
ProjectRootPath SearchForProject(std::string_view path, std::string& subpathOut);

/**
 * @brief Test if given path is a PNG (based on file header)
 * @param path Path to test
 * @return true if PNG
 */
bool IsPathPNG(const hecl::ProjectPath& path);

/**
 * @brief Test if given path is a blend (based on file header)
 * @param path Path to test
 * @return true if blend
 */
bool IsPathBlend(const hecl::ProjectPath& path);

/**
 * @brief Test if given path is a yaml (based on file extension)
 * @param path Path to test
 * @return true if yaml
 */
bool IsPathYAML(const hecl::ProjectPath& path);

#undef bswap16
#undef bswap32
#undef bswap64

/* Type-sensitive byte swappers */
template <typename T>
constexpr T bswap16(T val) noexcept {
#if __GNUC__
  return __builtin_bswap16(val);
#elif _WIN32
  return _byteswap_ushort(val);
#else
  return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
constexpr T bswap32(T val) noexcept {
#if __GNUC__
  return __builtin_bswap32(val);
#elif _WIN32
  return _byteswap_ulong(val);
#else
  val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
  val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
  return val;
#endif
}

template <typename T>
constexpr T bswap64(T val) noexcept {
#if __GNUC__
  return __builtin_bswap64(val);
#elif _WIN32
  return _byteswap_uint64(val);
#else
  return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
         ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
         ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
         ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
#endif
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr int16_t SBig(int16_t val) noexcept { return bswap16(val); }
constexpr uint16_t SBig(uint16_t val) noexcept { return bswap16(val); }
constexpr int32_t SBig(int32_t val) noexcept { return bswap32(val); }
constexpr uint32_t SBig(uint32_t val) noexcept { return bswap32(val); }
constexpr int64_t SBig(int64_t val) noexcept { return bswap64(val); }
constexpr uint64_t SBig(uint64_t val) noexcept { return bswap64(val); }
constexpr float SBig(float val) noexcept {
  union {
    float f;
    atInt32 i;
  } uval1 = {val};
  union {
    atInt32 i;
    float f;
  } uval2 = {bswap32(uval1.i)};
  return uval2.f;
}
constexpr double SBig(double val) noexcept {
  union {
    double f;
    atInt64 i;
  } uval1 = {val};
  union {
    atInt64 i;
    double f;
  } uval2 = {bswap64(uval1.i)};
  return uval2.f;
}
#ifndef SBIG
#define SBIG(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SLittle(int16_t val) noexcept { return val; }
constexpr uint16_t SLittle(uint16_t val) noexcept { return val; }
constexpr int32_t SLittle(int32_t val) noexcept { return val; }
constexpr uint32_t SLittle(uint32_t val) noexcept { return val; }
constexpr int64_t SLittle(int64_t val) noexcept { return val; }
constexpr uint64_t SLittle(uint64_t val) noexcept { return val; }
constexpr float SLittle(float val) noexcept { return val; }
constexpr double SLittle(double val) noexcept { return val; }
#ifndef SLITTLE
#define SLITTLE(q) (q)
#endif
#else
constexpr int16_t SLittle(int16_t val) noexcept { return bswap16(val); }
constexpr uint16_t SLittle(uint16_t val) noexcept { return bswap16(val); }
constexpr int32_t SLittle(int32_t val) noexcept { return bswap32(val); }
constexpr uint32_t SLittle(uint32_t val) noexcept { return bswap32(val); }
constexpr int64_t SLittle(int64_t val) noexcept { return bswap64(val); }
constexpr uint64_t SLittle(uint64_t val) noexcept { return bswap64(val); }
constexpr float SLittle(float val) noexcept {
  int32_t ival = bswap32(*((int32_t*)(&val)));
  return *((float*)(&ival));
}
constexpr double SLittle(double val) noexcept {
  int64_t ival = bswap64(*((int64_t*)(&val)));
  return *((double*)(&ival));
}
#ifndef SLITTLE
#define SLITTLE(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SBig(int16_t val) noexcept { return val; }
constexpr uint16_t SBig(uint16_t val) noexcept { return val; }
constexpr int32_t SBig(int32_t val) noexcept { return val; }
constexpr uint32_t SBig(uint32_t val) noexcept { return val; }
constexpr int64_t SBig(int64_t val) noexcept { return val; }
constexpr uint64_t SBig(uint64_t val) noexcept { return val; }
constexpr float SBig(float val) noexcept { return val; }
constexpr double SBig(double val) noexcept { return val; }
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

template <typename SizeT>
constexpr void hash_combine_impl(SizeT& seed, SizeT value) noexcept {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace hecl

#define CHAINED_SIGNAL_HANDLER(name, signal) \
class ChainedSignalHandler_##name { \
  typedef void(*sighandler_t)(int); \
  typedef void(*sigaction_t)(int, siginfo_t*, void*); \
  static std::atomic_bool m_isSetup; \
  static sighandler_t m_nextsh; \
  static sigaction_t m_nextsa; \
  static void my_sig_action(int sig, siginfo_t* si, void* ctx); \
  static void sig_action(int sig, siginfo_t* si, void* ctx) { \
    my_sig_action(sig, si, ctx); \
    if (m_nextsa) \
      m_nextsa(sig, si, ctx); \
    else if (m_nextsh) \
      m_nextsh(sig); \
  } \
public: \
  static void setup() { \
    if (ChainedSignalHandler_##name::m_isSetup.exchange(true) == true) \
      return; \
    { \
      struct sigaction sold; \
      if (sigaction(signal, nullptr, &sold) == 0) { \
        if (sold.sa_flags & SA_SIGINFO) \
          m_nextsa = sold.sa_sigaction; \
        else \
          m_nextsh = sold.sa_handler; \
      } \
    } \
    { \
      struct sigaction snew = {}; \
      snew.sa_sigaction = sig_action; \
      snew.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_SIGINFO; \
      sigaction(signal, &snew, nullptr); \
    } \
  } \
}; \
std::atomic_bool ChainedSignalHandler_##name::m_isSetup = {false}; \
ChainedSignalHandler_##name::sighandler_t ChainedSignalHandler_##name::m_nextsh = {}; \
ChainedSignalHandler_##name::sigaction_t ChainedSignalHandler_##name::m_nextsa = {}; \
inline void ChainedSignalHandler_##name::my_sig_action

#define SETUP_CHAINED_SIGNAL_HANDLER(name) \
ChainedSignalHandler_##name::setup()

namespace std {
template <>
struct hash<hecl::ProjectPath> {
  size_t operator()(const hecl::ProjectPath& val) const noexcept { return val.hash().valSizeT(); }
};
template <>
struct hash<hecl::Hash> {
  size_t operator()(const hecl::Hash& val) const noexcept { return val.valSizeT(); }
};
} // namespace std
