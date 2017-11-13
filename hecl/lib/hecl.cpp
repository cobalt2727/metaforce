#include "hecl/hecl.hpp"
#include <thread>
#include <mutex>
#include <unordered_map>

#ifdef WIN32
#include <windows.h>
#ifndef _WIN32_IE
#define _WIN32_IE 0x0400
#endif
#include <shlobj.h>
#endif

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif

#ifdef __linux__
#include <mntent.h>
#include <sys/wait.h>
#endif

namespace hecl
{
unsigned VerbosityLevel = 0;
logvisor::Module LogModule("hecl");
static const std::string Illegals {"<>?\"|"};

#if __GNUC__
__attribute__((__format__ (__printf__, 1, 2)))
#endif
SystemString SysFormat(const SystemChar* format, ...)
{
    SystemChar resultBuf[FORMAT_BUF_SZ];
    va_list va;
    va_start(va, format);
#if HECL_UCS2
    int printSz = vswprintf(resultBuf, FORMAT_BUF_SZ, format, va);
#else
    int printSz = vsnprintf(resultBuf, FORMAT_BUF_SZ, format, va);
#endif
    va_end(va);
    return SystemString(resultBuf, printSz);
}

#if __GNUC__
__attribute__((__format__ (__printf__, 1, 2)))
#endif
std::string Format(const char* format, ...)
{
    char resultBuf[FORMAT_BUF_SZ];
    va_list va;
    va_start(va, format);
    int printSz = vsnprintf(resultBuf, FORMAT_BUF_SZ, format, va);
    va_end(va);
    return std::string(resultBuf, printSz);
}

std::wstring WideFormat(const wchar_t* format, ...)
{
    wchar_t resultBuf[FORMAT_BUF_SZ];
    va_list va;
    va_start(va, format);
    int printSz = vswprintf(resultBuf, FORMAT_BUF_SZ, format, va);
    va_end(va);
    return std::wstring(resultBuf, printSz);
}

std::u16string Char16Format(const wchar_t* format, ...)
{
    wchar_t resultBuf[FORMAT_BUF_SZ];
    va_list va;
    va_start(va, format);
    int printSz = vswprintf(resultBuf, FORMAT_BUF_SZ, format, va);
    va_end(va);
    std::u16string res;
    res.reserve(printSz);
    for (size_t i=0 ; i<printSz ; ++i)
        res.push_back(resultBuf[i]);
    return res;
}

void SanitizePath(std::string& path)
{
    if (path.empty())
        return;
    path.erase(std::remove(path.begin(), path.end(), '\n'), path.end());
    path.erase(std::remove(path.begin(), path.end(), '\r'), path.end());
    std::string::iterator p1 = path.begin();
    bool ic = false;
    std::transform(path.begin(), path.end(), path.begin(), [&](const char a) -> char {
        ++p1;
        if (Illegals.find_first_of(a) != std::string::npos)
        {
            ic = false;
            return '_';
        }

        if (ic)
        {
            ic = false;
            return a;
        }
        if (a == '\\' && (p1 == path.end() || *p1 != '\\'))
        {
            ic = true;
            return '/';
        }
        return a;
    });
    while (path.back() == '/')
        path.pop_back();
}

static const std::wstring WIllegals {L"<>?\"|"};

void SanitizePath(std::wstring& path)
{
    if (path.empty())
        return;
    path.erase(std::remove(path.begin(), path.end(), L'\n'), path.end());
    path.erase(std::remove(path.begin(), path.end(), L'\r'), path.end());
    std::wstring::iterator p1 = path.begin();
    bool ic = false;
    std::transform(path.begin(), path.end(), path.begin(), [&](const wchar_t a) -> wchar_t {
        ++p1;
        if (WIllegals.find_first_of(a) != std::wstring::npos)
        {
            ic = false;
            return L'_';
        }

        if (ic)
        {
            ic = false;
            return a;
        }
        if (a == L'\\' && (p1 == path.end() || *p1 != L'\\'))
        {
            ic = true;
            return L'/';
        }
        return a;
    });
    while (path.back() == L'/')
        path.pop_back();
}

static std::mutex PathsMutex;
static std::unordered_map<std::thread::id, ProjectPath> PathsInProgress;

bool ResourceLock::InProgress(const ProjectPath& path)
{
    std::unique_lock<std::mutex> lk(PathsMutex);
    for (const auto& p : PathsInProgress)
        if (p.second == path)
            return true;
    return false;
}

bool ResourceLock::SetThreadRes(const ProjectPath& path)
{
    std::unique_lock<std::mutex> lk(PathsMutex);
    if (PathsInProgress.find(std::this_thread::get_id()) != PathsInProgress.cend())
        LogModule.report(logvisor::Fatal, "multiple resource locks on thread");

    for (const auto& p : PathsInProgress)
        if (p.second == path)
            return false;

    PathsInProgress[std::this_thread::get_id()] = path;
    return true;
}

void ResourceLock::ClearThreadRes()
{
    std::unique_lock<std::mutex> lk(PathsMutex);
    PathsInProgress.erase(std::this_thread::get_id());
}

bool IsPathPNG(const hecl::ProjectPath& path)
{
    FILE* fp = hecl::Fopen(path.getAbsolutePath().data(), _S("rb"));
    if (!fp)
        return false;
    uint32_t buf = 0;
    if (fread(&buf, 1, 4, fp) != 4)
    {
        fclose(fp);
        return false;
    }
    fclose(fp);
    buf = hecl::SBig(buf);
    if (buf == 0x89504e47)
        return true;
    return false;
}

bool IsPathBlend(const hecl::ProjectPath& path)
{
    auto lastCompExt = path.getLastComponentExt();
    if (lastCompExt.empty() || hecl::StrCmp(lastCompExt.data(), _S("blend")))
        return false;
    FILE* fp = hecl::Fopen(path.getAbsolutePath().data(), _S("rb"));
    if (!fp)
        return false;
    uint32_t buf = 0;
    if (fread(&buf, 1, 4, fp) != 4)
    {
        fclose(fp);
        return false;
    }
    fclose(fp);
    buf = hecl::SLittle(buf);
    if (buf == 0x4e454c42 || buf == 0x88b1f)
        return true;
    return false;
}

bool IsPathYAML(const hecl::ProjectPath& path)
{
    if (!hecl::StrCmp(path.getLastComponent().data(), _S("!catalog.yaml")))
        return false; /* !catalog.yaml is exempt from general use */
    auto lastCompExt = path.getLastComponentExt();
    if (lastCompExt.empty())
        return false;
    if (!hecl::StrCmp(lastCompExt.data(), _S("yaml")) ||
        !hecl::StrCmp(lastCompExt.data(), _S("yml")))
        return true;
    return false;
}

hecl::DirectoryEnumerator::DirectoryEnumerator(SystemStringView path, Mode mode,
                                               bool sizeSort, bool reverse, bool noHidden)
{
    hecl::Sstat theStat;
    if (hecl::Stat(path.data(), &theStat) || !S_ISDIR(theStat.st_mode))
        return;

#if _WIN32
    hecl::SystemString wc(path);
    wc += _S("/*");
    WIN32_FIND_DATAW d;
    HANDLE dir = FindFirstFileW(wc.c_str(), &d);
    if (dir == INVALID_HANDLE_VALUE)
        return;
    switch (mode)
    {
    case Mode::Native:
        do
        {
            if (!wcscmp(d.cFileName, _S(".")) || !wcscmp(d.cFileName, _S("..")))
                continue;
            if (noHidden && (d.cFileName[0] == L'.' || (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0))
                continue;
            hecl::SystemString fp(path);
            fp += _S('/');
            fp += d.cFileName;
            hecl::Sstat st;
            if (hecl::Stat(fp.c_str(), &st))
                continue;

            size_t sz = 0;
            bool isDir = false;
            if (S_ISDIR(st.st_mode))
                isDir = true;
            else if (S_ISREG(st.st_mode))
                sz = st.st_size;
            else
                continue;

            m_entries.push_back(std::move(Entry(std::move(fp), d.cFileName, sz, isDir)));
        } while (FindNextFileW(dir, &d));
        break;
    case Mode::DirsThenFilesSorted:
    case Mode::DirsSorted:
    {
        std::map<hecl::SystemString, Entry, CaseInsensitiveCompare> sort;
        do
        {
            if (!wcscmp(d.cFileName, _S(".")) || !wcscmp(d.cFileName, _S("..")))
                continue;
            if (noHidden && (d.cFileName[0] == L'.' || (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0))
                continue;
            hecl::SystemString fp(path);
            fp +=_S('/');
            fp += d.cFileName;
            hecl::Sstat st;
            if (hecl::Stat(fp.c_str(), &st) || !S_ISDIR(st.st_mode))
                continue;
            sort.emplace(std::make_pair(d.cFileName, Entry(std::move(fp), d.cFileName, 0, true)));
        } while (FindNextFileW(dir, &d));

        if (reverse)
            for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                m_entries.push_back(std::move(it->second));
        else
            for (auto& e : sort)
                m_entries.push_back(std::move(e.second));

        if (mode == Mode::DirsSorted)
            break;
        FindClose(dir);
        dir = FindFirstFileW(wc.c_str(), &d);
    }
    case Mode::FilesSorted:
    {
        if (mode == Mode::FilesSorted)
            m_entries.clear();

        if (sizeSort)
        {
            std::multimap<size_t, Entry> sort;
            do
            {
                if (!wcscmp(d.cFileName, _S(".")) || !wcscmp(d.cFileName, _S("..")))
                    continue;
                if (noHidden && (d.cFileName[0] == L'.' || (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0))
                    continue;
                hecl::SystemString fp(path);
                fp += _S('/');
                fp += d.cFileName;
                hecl::Sstat st;
                if (hecl::Stat(fp.c_str(), &st) || !S_ISREG(st.st_mode))
                    continue;
                sort.emplace(std::make_pair(st.st_size, Entry(std::move(fp), d.cFileName, st.st_size, false)));
            } while (FindNextFileW(dir, &d));

            if (reverse)
                for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                    m_entries.push_back(std::move(it->second));
            else
                for (auto& e : sort)
                    m_entries.push_back(std::move(e.second));
        }
        else
        {
            std::map<hecl::SystemString, Entry, CaseInsensitiveCompare> sort;
            do
            {
                if (!wcscmp(d.cFileName, _S(".")) || !wcscmp(d.cFileName, _S("..")))
                    continue;
                if (noHidden && (d.cFileName[0] == L'.' || (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0))
                    continue;
                hecl::SystemString fp(path);
                fp += _S('/');
                fp += d.cFileName;
                hecl::Sstat st;
                if (hecl::Stat(fp.c_str(), &st) || !S_ISREG(st.st_mode))
                    continue;
                sort.emplace(std::make_pair(d.cFileName, Entry(std::move(fp), d.cFileName, st.st_size, false)));
            } while (FindNextFileW(dir, &d));

            if (reverse)
                for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                    m_entries.push_back(std::move(it->second));
            else
                for (auto& e : sort)
                    m_entries.push_back(std::move(e.second));
        }

        break;
    }
    }
    FindClose(dir);

#else

    DIR* dir = opendir(path.data());
    if (!dir)
        return;
    const dirent* d;
    switch (mode)
    {
    case Mode::Native:
        while ((d = readdir(dir)))
        {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                continue;
            if (noHidden && d->d_name[0] == '.')
                continue;
            hecl::SystemString fp(path);
            fp += '/';
            fp += d->d_name;
            hecl::Sstat st;
            if (hecl::Stat(fp.c_str(), &st))
                continue;

            size_t sz = 0;
            bool isDir = false;
            if (S_ISDIR(st.st_mode))
                isDir = true;
            else if (S_ISREG(st.st_mode))
                sz = st.st_size;
            else
                continue;

            m_entries.push_back(Entry(std::move(fp), d->d_name, sz, isDir));
        }
        break;
    case Mode::DirsThenFilesSorted:
    case Mode::DirsSorted:
    {
        std::map<hecl::SystemString, Entry, CaseInsensitiveCompare> sort;
        while ((d = readdir(dir)))
        {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                continue;
            if (noHidden && d->d_name[0] == '.')
                continue;
            hecl::SystemString fp(path);
            fp += '/';
            fp += d->d_name;
            hecl::Sstat st;
            if (hecl::Stat(fp.c_str(), &st) || !S_ISDIR(st.st_mode))
                continue;
            sort.emplace(std::make_pair(d->d_name, Entry(std::move(fp), d->d_name, 0, true)));
        }

        if (reverse)
            for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                m_entries.push_back(std::move(it->second));
        else
            for (auto& e : sort)
                m_entries.push_back(std::move(e.second));

        if (mode == Mode::DirsSorted)
            break;
        rewinddir(dir);
    }
    case Mode::FilesSorted:
    {
        if (mode == Mode::FilesSorted)
            m_entries.clear();

        if (sizeSort)
        {
            std::multimap<size_t, Entry> sort;
            while ((d = readdir(dir)))
            {
                if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                    continue;
                if (noHidden && d->d_name[0] == '.')
                    continue;
                hecl::SystemString fp(path);
                fp += '/';
                fp += d->d_name;
                hecl::Sstat st;
                if (hecl::Stat(fp.c_str(), &st) || !S_ISREG(st.st_mode))
                    continue;
                sort.emplace(std::make_pair(st.st_size, Entry(std::move(fp), d->d_name, st.st_size, false)));
            }

            if (reverse)
                for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                    m_entries.push_back(std::move(it->second));
            else
                for (auto& e : sort)
                    m_entries.push_back(std::move(e.second));
        }
        else
        {
            std::map<hecl::SystemString, Entry, CaseInsensitiveCompare> sort;
            while ((d = readdir(dir)))
            {
                if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
                    continue;
                if (noHidden && d->d_name[0] == '.')
                    continue;
                hecl::SystemString fp(path);
                fp += '/';
                fp += d->d_name;
                hecl::Sstat st;
                if (hecl::Stat(fp.c_str(), &st) || !S_ISREG(st.st_mode))
                    continue;
                sort.emplace(std::make_pair(d->d_name, Entry(std::move(fp), d->d_name, st.st_size, false)));
            }

            if (reverse)
                for (auto it=sort.crbegin() ; it != sort.crend() ; ++it)
                    m_entries.push_back(std::move(it->second));
            else
                for (auto& e : sort)
                    m_entries.push_back(std::move(e.second));
        }

        break;
    }
    }
    closedir(dir);

#endif
}

#define FILE_MAXDIR 768

static std::pair<hecl::SystemString, std::string>
NameFromPath(hecl::SystemStringView path)
{
    hecl::SystemUTF8Conv utf8(path);
    if (utf8.str().size() == 1 && utf8.str()[0] == '/')
        return {hecl::SystemString(path), "/"};
    size_t lastSlash = utf8.str().rfind('/');
    if (lastSlash != std::string::npos)
        return {hecl::SystemString(path), std::string(utf8.str().cbegin() + lastSlash + 1, utf8.str().cend())};
    else
        return {hecl::SystemString(path), std::string(utf8.str())};
}

std::vector<std::pair<hecl::SystemString, std::string>> GetSystemLocations()
{
    std::vector<std::pair<hecl::SystemString, std::string>> ret;
#ifdef WIN32
    /* Add the drive names to the listing (as queried by blender) */
    {
        wchar_t wline[FILE_MAXDIR];
        wchar_t* name;
        __int64 tmp;
        int i;

        tmp = GetLogicalDrives();

        for (i = 0; i < 26; i++)
        {
            if ((tmp >> i) & 1)
            {
                wline[0] = L'A' + i;
                wline[1] = L':';
                wline[2] = L'/';
                wline[3] = L'\0';
                name = nullptr;

                /* Flee from horrible win querying hover floppy drives! */
                if (i > 1)
                {
                    /* Try to get volume label as well... */
                    if (GetVolumeInformationW(wline, wline + 4, FILE_MAXDIR - 4, nullptr, nullptr, nullptr, nullptr, 0))
                    {
                        size_t labelLen = wcslen(wline + 4);
                        _snwprintf(wline + 4 + labelLen, FILE_MAXDIR - 4 - labelLen, L" (%.2s)", wline);
                        name = wline + 4;
                    }
                }

                wline[2] = L'\0';
                if (name)
                    ret.emplace_back(wline, hecl::WideToUTF8(name));
                else
                    ret.push_back(NameFromPath(wline));
            }
        }

        /* Adding Desktop and My Documents */
        SystemString wpath;
        SHGetSpecialFolderPathW(0, wline, CSIDL_PERSONAL, 0);
        wpath.assign(wline);
        SanitizePath(wpath);
        ret.push_back(NameFromPath(wpath));
        SHGetSpecialFolderPathW(0, wline, CSIDL_DESKTOPDIRECTORY, 0);
        wpath.assign(wline);
        SanitizePath(wpath);
        ret.push_back(NameFromPath(wpath));
    }
#else
#ifdef __APPLE__
    {
        hecl::Sstat theStat;
        const char* home = getenv("HOME");

        if (home)
        {
            ret.push_back(NameFromPath(home));
            std::string desktop(home);
            desktop += "/Desktop";
            if (!hecl::Stat(desktop.c_str(), &theStat))
                ret.push_back(NameFromPath(desktop));
        }

        /* Get mounted volumes better method OSX 10.6 and higher, see: */
        /*https://developer.apple.com/library/mac/#documentation/CoreFOundation/Reference/CFURLRef/Reference/reference.html*/
        /* we get all volumes sorted including network and do not relay on user-defined finder visibility, less confusing */

        CFURLRef cfURL = NULL;
        CFURLEnumeratorResult result = kCFURLEnumeratorSuccess;
        CFURLEnumeratorRef volEnum = CFURLEnumeratorCreateForMountedVolumes(NULL, kCFURLEnumeratorSkipInvisibles, NULL);

        while (result != kCFURLEnumeratorEnd)
        {
            char defPath[1024];

            result = CFURLEnumeratorGetNextURL(volEnum, &cfURL, NULL);
            if (result != kCFURLEnumeratorSuccess)
                continue;

            CFURLGetFileSystemRepresentation(cfURL, false, (UInt8 *)defPath, 1024);
            ret.push_back(NameFromPath(defPath));
        }

        CFRelease(volEnum);
    }
#else
    /* unix */
    {
        hecl::Sstat theStat;
        const char* home = getenv("HOME");

        if (home)
        {
            ret.push_back(NameFromPath(home));
            std::string desktop(home);
            desktop += "/Desktop";
            if (!hecl::Stat(desktop.c_str(), &theStat))
                ret.push_back(NameFromPath(desktop));
        }

        {
            bool found = false;
#ifdef __linux__
            /* Loop over mount points */
            struct mntent *mnt;

            FILE* fp = setmntent(MOUNTED, "r");
            if (fp)
            {
                while ((mnt = getmntent(fp)))
                {
                    if (strlen(mnt->mnt_fsname) < 4 || strncmp(mnt->mnt_fsname, "/dev", 4))
                        continue;

                    std::string mntStr(mnt->mnt_dir);
                    if (mntStr.size() > 1 && mntStr.back() == '/')
                        mntStr.pop_back();
                    ret.push_back(NameFromPath(mntStr));

                    found = true;
                }
                endmntent(fp);
            }
#endif
            /* Fallback */
            if (!found)
                ret.push_back(NameFromPath("/"));
        }
    }
#endif
#endif
    return ret;
}

std::wstring Char16ToWide(std::u16string_view src)
{
    return std::wstring(src.begin(), src.end());
}

/* recursive mkdir */
#if _WIN32
int RecursiveMakeDir(const SystemChar* dir) {
    SystemChar tmp[1024];
    SystemChar *p = nullptr;
    Sstat sb;
    size_t len;

    /* copy path */
    wcsncpy(tmp, dir, 1024);
    len = wcslen(tmp);
    if (len >= 1024) {
        return -1;
    }

    /* remove trailing slash */
    if(tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
        tmp[len - 1] = 0;
    }

    /* recursive mkdir */
    for(p = tmp + 1; *p; p++) {
        if(*p == '/' || *p == '\\') {
            *p = 0;
            /* test path */
            if (Stat(tmp, &sb) != 0) {
                /* path does not exist - create directory */
                if (!CreateDirectoryW(tmp, nullptr)) {
                    return -1;
                }
            } else if (!S_ISDIR(sb.st_mode)) {
                /* not a directory */
                return -1;
            }
            *p = '/';
        }
    }
    /* test path */
    if (Stat(tmp, &sb) != 0) {
        /* path does not exist - create directory */
        if (!CreateDirectoryW(tmp, nullptr)) {
            return -1;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        /* not a directory */
        return -1;
    }
    return 0;
}
#else
int RecursiveMakeDir(const SystemChar* dir) {
    SystemChar tmp[1024];
    SystemChar *p = nullptr;
    Sstat sb;
    size_t len;

    /* copy path */
    strncpy(tmp, dir, 1024);
    len = strlen(tmp);
    if (len >= 1024) {
        return -1;
    }

    /* remove trailing slash */
    if(tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    /* recursive mkdir */
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            /* test path */
            if (Stat(tmp, &sb) != 0) {
                /* path does not exist - create directory */
                if (mkdir(tmp, 0755) < 0) {
                    return -1;
                }
            } else if (!S_ISDIR(sb.st_mode)) {
                /* not a directory */
                return -1;
            }
            *p = '/';
        }
    }
    /* test path */
    if (Stat(tmp, &sb) != 0) {
        /* path does not exist - create directory */
        if (mkdir(tmp, 0755) < 0) {
            return -1;
        }
    } else if (!S_ISDIR(sb.st_mode)) {
        /* not a directory */
        return -1;
    }
    return 0;
}
#endif

const SystemChar* GetTmpDir()
{
#ifdef _WIN32
    wchar_t* TMPDIR = _wgetenv(L"TEMP");
    if (!TMPDIR)
        TMPDIR = (wchar_t*)L"\\Temp";
#else
    char* TMPDIR = getenv("TMPDIR");
    if (!TMPDIR)
        TMPDIR = (char*)"/tmp";
#endif
    return TMPDIR;
}

int RunProcess(const SystemChar* path, const SystemChar* const args[])
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES sattrs = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE consoleOutReadTmp, consoleOutWrite, consoleErrWrite, consoleOutRead;
    if (!CreatePipe(&consoleOutReadTmp, &consoleOutWrite, &sattrs, 0))
    {
        LogModule.report(logvisor::Fatal, "Error with CreatePipe");
        return -1;
    }

    if (!DuplicateHandle(GetCurrentProcess(), consoleOutWrite,
                         GetCurrentProcess(), &consoleErrWrite, 0,
                         TRUE,DUPLICATE_SAME_ACCESS))
    {
        LogModule.report(logvisor::Fatal, "Error with DuplicateHandle");
        CloseHandle(consoleOutReadTmp);
        CloseHandle(consoleOutWrite);
        return -1;
    }

    if (!DuplicateHandle(GetCurrentProcess(), consoleOutReadTmp,
                         GetCurrentProcess(),
                         &consoleOutRead, // Address of new handle.
                         0, FALSE, // Make it uninheritable.
                         DUPLICATE_SAME_ACCESS))
    {
        LogModule.report(logvisor::Fatal, "Error with DupliateHandle");
        CloseHandle(consoleOutReadTmp);
        CloseHandle(consoleOutWrite);
        CloseHandle(consoleErrWrite);
        return -1;
    }

    CloseHandle(consoleOutReadTmp);

    hecl::SystemString cmdLine;
    const SystemChar* const* arg = &args[1];
    while (*arg)
    {
        cmdLine += _S(" \"");
        cmdLine += *arg++;
        cmdLine += _S('"');
    }

    STARTUPINFO sinfo = {sizeof(STARTUPINFO)};
    HANDLE nulHandle = CreateFileW(L"nul", GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                                   &sattrs, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    sinfo.dwFlags = STARTF_USESTDHANDLES;
    sinfo.hStdInput = nulHandle;
    sinfo.hStdError = consoleErrWrite;
    sinfo.hStdOutput = consoleOutWrite;

    PROCESS_INFORMATION pinfo = {};
    if (!CreateProcessW(path, (LPWSTR)cmdLine.c_str(), NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &sinfo, &pinfo))
    {
        LPWSTR messageBuffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);
        LogModule.report(logvisor::Error, L"unable to launch process from %s: %s", path, messageBuffer);
        LocalFree(messageBuffer);

        CloseHandle(nulHandle);
        CloseHandle(consoleErrWrite);
        CloseHandle(consoleOutWrite);
        CloseHandle(consoleOutRead);
        return -1;
    }

    CloseHandle(nulHandle);
    CloseHandle(consoleErrWrite);
    CloseHandle(consoleOutWrite);

    bool consoleThreadRunning = true;
    auto consoleThread = std::thread([=, &consoleThreadRunning]()
    {
        CHAR lpBuffer[256];
        DWORD nBytesRead;
        DWORD nCharsWritten;

        while (consoleThreadRunning)
        {
           if (!ReadFile(consoleOutRead, lpBuffer, sizeof(lpBuffer),
                         &nBytesRead, NULL) || !nBytesRead)
           {
               DWORD err = GetLastError();
               if (err == ERROR_BROKEN_PIPE)
                   break; // pipe done - normal exit path.
               else
                   LogModule.report(logvisor::Error, "Error with ReadFile: %08X", err); // Something bad happened.
           }

           // Display the character read on the screen.
           auto lk = logvisor::LockLog();
           if (!WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), lpBuffer,
                              nBytesRead, &nCharsWritten, NULL))
               LogModule.report(logvisor::Error, "Error with WriteConsole: %08X", GetLastError());
        }

        CloseHandle(consoleOutRead);
    });

    WaitForSingleObject(pinfo.hProcess, INFINITE);
    DWORD ret;
    if (!GetExitCodeProcess(pinfo.hProcess, &ret))
        ret = -1;
    consoleThreadRunning = false;
    if (consoleThread.joinable())
        consoleThread.join();

    CloseHandle(pinfo.hProcess);
    CloseHandle(pinfo.hThread);

    return ret;
#else
    pid_t pid = fork();
    if (!pid)
    {
        execvp(path, (char * const *)args);
        exit(1);
    }
    int ret;
    if (waitpid(pid, &ret, 0) < 0)
        return -1;
    if (WIFEXITED(ret))
        return WEXITSTATUS(ret);
    return -1;
#endif
}

}
