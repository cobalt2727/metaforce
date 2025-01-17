#include "Runtime/CMemoryCardSys.hpp"

#include "Runtime/GameGlobalObjects.hpp"
#include "Runtime/IMain.hpp"

#include <shlobj.h>

namespace metaforce {

#if WINDOWS_STORE
using namespace Windows::Storage;
#endif

/* Partial path-selection logic from
 * https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/UICommon/UICommon.cpp
 * Modified to not use dolphin-binary-relative paths. */
std::string CMemoryCardSys::ResolveDolphinCardPath(kabufuda::ECardSlot slot) {
  if (g_Main->IsUSA() && !g_Main->IsTrilogy()) {
#if !WINDOWS_STORE
    /* Detect where the User directory is. There are two different cases
     * 1. HKCU\Software\Dolphin Emulator\UserConfigPath exists
     *    -> Use this as the user directory path
     * 2. My Documents exists
     *    -> Use My Documents\Dolphin Emulator as the User directory path
     */

    /* Check our registry keys */
    HKEY hkey;
    wchar_t configPath[MAX_PATH] = {0};
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Dolphin Emulator", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
      DWORD size = MAX_PATH;
      if (RegQueryValueEx(hkey, L"UserConfigPath", nullptr, nullptr, (LPBYTE)configPath, &size) != ERROR_SUCCESS)
        configPath[0] = 0;
      RegCloseKey(hkey);
    }

    /* Get My Documents path in case we need it. */
    wchar_t my_documents[MAX_PATH];
    bool my_documents_found =
        SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, my_documents));

    std::string path;
    if (configPath[0]) /* Case 1 */
      path = nowide::narrow(configPath);
    else if (my_documents_found) /* Case 2 */
      path = nowide::narrow(my_documents) + "/Dolphin Emulator";
    else /* Unable to find */
      return {};
#else
    StorageFolder ^ localFolder = ApplicationData::Current->LocalFolder;
    std::string path(localFolder->Path->Data());
#endif

    path += fmt::format(FMT_STRING("/GC/MemoryCard{}.USA.raw"), slot == kabufuda::ECardSlot::SlotA ? 'A' : 'B');

    hecl::Sstat theStat;
    if (hecl::Stat(path.c_str(), &theStat) || !S_ISREG(theStat.st_mode))
      return {};

    return path;
  }
  return {};
}

std::string CMemoryCardSys::_CreateDolphinCard(kabufuda::ECardSlot slot, bool dolphin) {
  if (g_Main->IsUSA() && !g_Main->IsTrilogy()) {
    if (dolphin) {
#if !WINDOWS_STORE
      /* Detect where the User directory is. There are two different cases
       * 1. HKCU\Software\Dolphin Emulator\UserConfigPath exists
       *    -> Use this as the user directory path
       * 2. My Documents exists
       *    -> Use My Documents\Dolphin Emulator as the User directory path
       */

      /* Check our registry keys */
      HKEY hkey;
      wchar_t configPath[MAX_PATH] = {0};
      if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Dolphin Emulator", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS) {
        DWORD size = MAX_PATH;
        if (RegQueryValueEx(hkey, L"UserConfigPath", nullptr, nullptr, (LPBYTE)configPath, &size) != ERROR_SUCCESS)
          configPath[0] = 0;
        RegCloseKey(hkey);
      }

      /* Get My Documents path in case we need it. */
      wchar_t my_documents[MAX_PATH];
      bool my_documents_found =
          SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, my_documents));

      std::string path;
      if (configPath[0]) /* Case 1 */
        path = nowide::narrow(configPath);
      else if (my_documents_found) /* Case 2 */
        path = nowide::narrow(my_documents) + "/Dolphin Emulator";
      else /* Unable to find */
        return {};
#else
      StorageFolder ^ localFolder = ApplicationData::Current->LocalFolder;
      std::string path(localFolder->Path->Data());
#endif

      path += "/GC";
      if (hecl::RecursiveMakeDir(path.c_str()) < 0)
        return {};

      path += fmt::format(FMT_STRING("/MemoryCard{}.USA.raw"), slot == kabufuda::ECardSlot::SlotA ? 'A' : 'B');
      const auto fp = hecl::FopenUnique(path.c_str(), "wb");
      if (fp == nullptr) {
        return {};
      }

      return path;
    } else {
      std::string path = _GetDolphinCardPath(slot);
      hecl::SanitizePath(path);
      if (path.find('/') == std::string::npos) {
        path = hecl::GetcwdStr() + "/" + _GetDolphinCardPath(slot);
      }
      std::string tmpPath = path.substr(0, path.find_last_of("/"));
      hecl::RecursiveMakeDir(tmpPath.c_str());
      const auto fp = hecl::FopenUnique(path.c_str(), "wb");
      if (fp) {
        return path;
      }
    }
  }
  return {};
}

} // namespace metaforce
