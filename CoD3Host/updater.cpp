// The updater (updater.h). WinHTTP, which Windows has, for the GitHub API's
// list of releases and the download; a small JSON reader for the list.
//
// Turned off by --no-update, COD3_NOUPDATE=1 (the test runs set it) or
// auto_update = 0 in the settings. COD3_UPDATEURL=<url> asks another place
// for the list of releases, for a test against a local server.

#include "updater.h"
#include "log.h"
#include "settings.h"
#include "cod3_version.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr wchar_t ReleasesUrl[] = L"https://api.github.com/repos/FrankHUN88453/CoD3Recomp/releases?per_page=20";
    constexpr char AssetName[] = "CoD3SP_s.exe";

    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
        std::wstring out(size_t(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size);
        return out;
    }

    std::wstring ExecutablePath()
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
        return length != 0 ? std::wstring(buffer, length) : std::wstring();
    }

    // --- HTTP ------------------------------------------------------------------------------------

    struct Handle
    {
        HINTERNET h = nullptr;
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };

    // A GET, the body into `text`, or into `file` with the progress said as
    // it comes (`expected` bytes, 0 unknown). Redirects are followed.
    bool Get(const std::wstring& url, std::string* text, FILE* file, uint64_t expected, std::string& error)
    {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256] = {}, path[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = DWORD(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = DWORD(std::size(path));
        wchar_t extra[2048] = {};
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = DWORD(std::size(extra));
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) { error = "a bad address"; return false; }
        const std::wstring agent = L"CoD3SP_s/" + Wide(COD3_VERSION_TEXT);
        Handle session{ WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session.h) { error = "no connection to the internet"; return false; }
        // Short for the list (an offline machine should not wait long for
        // the game), longer for each read of a download.
        WinHttpSetTimeouts(session.h, 4000, 4000, 8000, file != nullptr ? 30000 : 8000);
        Handle connection{ WinHttpConnect(session.h, host, parts.nPort, 0) };
        if (!connection.h) { error = "no connection to the server"; return false; }
        const std::wstring target = std::wstring(path) + extra;
        Handle request{ WinHttpOpenRequest(connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) };
        if (!request.h) { error = "the request could not be made"; return false; }
        const wchar_t* headers = text != nullptr ? L"Accept: application/vnd.github+json\r\n" : L"Accept: application/octet-stream\r\n";
        if (!WinHttpSendRequest(request.h, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr))
        {
            error = "no answer (offline?)";
            return false;
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) { error = "the server answered " + std::to_string(status); return false; }

        std::vector<char> buffer(1 << 16);
        uint64_t done = 0, shown = 0;
        for (;;)
        {
            DWORD got = 0;
            if (!WinHttpReadData(request.h, buffer.data(), DWORD(buffer.size()), &got)) { error = "the download broke off"; return false; }
            if (got == 0) break;
            if (text != nullptr) text->append(buffer.data(), got);
            if (file != nullptr && fwrite(buffer.data(), 1, got, file) != got) { error = "the file could not be written"; return false; }
            done += got;
            if (file != nullptr && (done - shown >= (4u << 20) || (expected != 0 && done == expected)))
            {
                shown = done;
                if (expected != 0) printf("\r  [%3d%%] %.1f of %.1f MB ", int(done * 100 / expected), done / 1048576.0, expected / 1048576.0);
                else printf("\r  %.1f MB ", done / 1048576.0);
                fflush(stdout);
            }
        }
        if (file != nullptr) printf("\n");
        return true;
    }

    // --- JSON ------------------------------------------------------------------------------------

    // Enough of JSON for the list of releases: values parsed into a tree.
    struct Value
    {
        enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
        bool flag = false;
        double number = 0;
        std::string text;
        std::vector<Value> items;
        std::vector<std::pair<std::string, Value>> members;
        const Value* Find(const char* key) const
        {
            for (const auto& [name, value] : members)
                if (name == key) return &value;
            return nullptr;
        }
    };

    struct Reader
    {
        const char* at;
        const char* end;
        void Space() { while (at < end && (*at == ' ' || *at == '\n' || *at == '\r' || *at == '\t')) at++; }
        bool Literal(const char* word)
        {
            const size_t n = strlen(word);
            if (size_t(end - at) < n || strncmp(at, word, n) != 0) return false;
            at += n;
            return true;
        }
        bool ParseString(std::string& out)
        {
            if (at >= end || *at != '"') return false;
            at++;
            while (at < end && *at != '"')
            {
                if (*at != '\\') { out += *at++; continue; }
                if (++at >= end) return false;
                const char c = *at++;
                switch (c)
                {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                {
                    if (end - at < 4) return false;
                    const unsigned code = unsigned(strtoul(std::string(at, 4).c_str(), nullptr, 16));
                    at += 4;
                    // Only names and versions are wanted: anything past ASCII
                    // is kept as a question mark.
                    out += code < 0x80 ? char(code) : '?';
                    break;
                }
                default: out += c; break;
                }
            }
            if (at >= end) return false;
            at++;
            return true;
        }
        bool Parse(Value& value, int depth = 0)
        {
            if (depth > 64) return false;
            Space();
            if (at >= end) return false;
            if (*at == '{')
            {
                value.kind = Value::Object;
                at++;
                Space();
                if (at < end && *at == '}') { at++; return true; }
                for (;;)
                {
                    Space();
                    std::string name;
                    if (!ParseString(name)) return false;
                    Space();
                    if (at >= end || *at != ':') return false;
                    at++;
                    value.members.emplace_back(std::move(name), Value{});
                    if (!Parse(value.members.back().second, depth + 1)) return false;
                    Space();
                    if (at < end && *at == ',') { at++; continue; }
                    if (at < end && *at == '}') { at++; return true; }
                    return false;
                }
            }
            if (*at == '[')
            {
                value.kind = Value::Array;
                at++;
                Space();
                if (at < end && *at == ']') { at++; return true; }
                for (;;)
                {
                    value.items.emplace_back();
                    if (!Parse(value.items.back(), depth + 1)) return false;
                    Space();
                    if (at < end && *at == ',') { at++; continue; }
                    if (at < end && *at == ']') { at++; return true; }
                    return false;
                }
            }
            if (*at == '"') { value.kind = Value::String; return ParseString(value.text); }
            if (Literal("true")) { value.kind = Value::Bool; value.flag = true; return true; }
            if (Literal("false")) { value.kind = Value::Bool; return true; }
            if (Literal("null")) { value.kind = Value::Null; return true; }
            char* stop = nullptr;
            value.number = strtod(at, &stop);
            if (stop == at) return false;
            value.kind = Value::Number;
            at = stop;
            return true;
        }
    };

    // --- versions --------------------------------------------------------------------------------

    struct Version { int major = -1, minor = 0, patch = 0; };

    // "v0.5.0" or "0.5.0" (anything after the numbers left aside).
    Version ParseVersion(const std::string& tag)
    {
        Version v;
        const char* p = tag.c_str();
        if (*p == 'v' || *p == 'V') p++;
        if (sscanf_s(p, "%d.%d.%d", &v.major, &v.minor, &v.patch) < 1) v.major = -1;
        return v;
    }
    bool Newer(const Version& a, const Version& b)
    {
        if (a.major != b.major) return a.major > b.major;
        if (a.minor != b.minor) return a.minor > b.minor;
        return a.patch > b.patch;
    }

    bool TurnedOff()
    {
        if (const char* t = getenv("COD3_NOUPDATE"); t != nullptr && t[0] != 0 && t[0] != '0') return true;
        return !Settings::Get().autoUpdate;
    }
}

void Updater::CleanUp()
{
    const std::wstring self = ExecutablePath();
    if (self.empty()) return;
    const std::wstring old = self + L".old";
    // The process that left it may still be ending: a few tries.
    for (int i = 0; i < 10 && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES; i++)
    {
        if (DeleteFileW(old.c_str())) break;
        Sleep(100);
    }
    DeleteFileW((self + L".download").c_str());
}

void Updater::UpdateAndRestart()
{
    if (TurnedOff()) return;
    const Version current{ COD3_VERSION_MAJOR, COD3_VERSION_MINOR, COD3_VERSION_PATCH };

    std::wstring listUrl = ReleasesUrl;
    if (const char* t = getenv("COD3_UPDATEURL"); t != nullptr && t[0] != 0) listUrl = Wide(t);
    std::string text, error;
    printf("Looking for a newer version... ");
    fflush(stdout);
    if (!Get(listUrl, &text, nullptr, 0, error))
    {
        printf("could not ask (%s).\n\n", error.c_str());
        return;
    }
    Value releases;
    Reader reader{ text.data(), text.data() + text.size() };
    if (!reader.Parse(releases) || releases.kind != Value::Array)
    {
        printf("the answer could not be read.\n\n");
        return;
    }

    // The highest version with the executable among its files.
    Version best = current;
    std::string bestTag, bestUrl;
    uint64_t bestSize = 0;
    for (const Value& release : releases.items)
    {
        const Value* draft = release.Find("draft");
        const Value* tag = release.Find("tag_name");
        const Value* assets = release.Find("assets");
        if (tag == nullptr || tag->kind != Value::String || assets == nullptr || assets->kind != Value::Array) continue;
        if (draft != nullptr && draft->flag) continue;
        const Version version = ParseVersion(tag->text);
        if (version.major < 0 || !Newer(version, best)) continue;
        for (const Value& asset : assets->items)
        {
            const Value* name = asset.Find("name");
            const Value* url = asset.Find("browser_download_url");
            const Value* size = asset.Find("size");
            if (name == nullptr || url == nullptr || name->text != AssetName) continue;
            best = version;
            bestTag = tag->text;
            bestUrl = url->text;
            bestSize = size != nullptr ? uint64_t(size->number) : 0;
        }
    }
    if (bestUrl.empty())
    {
        printf("this is the newest (%s).\n\n", COD3_VERSION_TEXT);
        return;
    }

    printf("%s is out (this is %s).\n", bestTag.c_str(), COD3_VERSION_TEXT);
    printf("Downloading %s...\n", AssetName);
    const std::wstring self = ExecutablePath();
    const std::wstring download = self + L".download", old = self + L".old";
    FILE* file = nullptr;
    if (_wfopen_s(&file, download.c_str(), L"wb") != 0 || file == nullptr)
    {
        printf("It cannot be saved beside this program (is its folder read only?); going on with this version.\n\n");
        return;
    }
    const bool got = Get(Wide(bestUrl), nullptr, file, bestSize, error);
    const long written = ftell(file);
    fclose(file);
    // A whole file, an executable.
    bool whole = got && (bestSize == 0 || uint64_t(written) == bestSize);
    if (whole)
    {
        FILE* check = nullptr;
        char mz[2] = {};
        if (_wfopen_s(&check, download.c_str(), L"rb") == 0 && check != nullptr)
        {
            whole = fread(mz, 1, 2, check) == 2 && mz[0] == 'M' && mz[1] == 'Z';
            fclose(check);
        }
        else whole = false;
    }
    if (!whole)
    {
        DeleteFileW(download.c_str());
        printf("The download failed (%s); going on with this version.\n\n", error.empty() ? "it is not whole" : error.c_str());
        return;
    }

    // This executable out of the way (a running one can be renamed, not
    // written over), the new one in its place.
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(download.c_str());
        printf("This program could not be replaced; going on with this version.\n\n");
        return;
    }
    if (!MoveFileExW(download.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(download.c_str());
        printf("The new version could not be put in place; going on with this version.\n\n");
        return;
    }

    // Started as this one was, in the same console; this one ends.
    printf("Updated to %s. Starting it...\n\n", bestTag.c_str());
    fflush(stdout);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = GetCommandLineW();
    if (!CreateProcessW(self.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        printf("It could not be started; start %ls again.\n", self.c_str());
        fflush(stdout);
        Log::Finish();
        ExitProcess(1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Log::Finish();
    ExitProcess(0);
}
