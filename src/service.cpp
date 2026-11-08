#include "service.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <map>
#include <stdexcept>
#include <cstring>
#include <cctype>

using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Windows image encoding failed"); }
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 response");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return w;
}
// A complete JSON value parser keeps strings, escapes and nested response fields distinct.
struct Json {
    enum Kind { Null, String, Number, Bool, Array, Object } kind = Null;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json& at(const char* key) const {
        static const Json empty;
        auto it = object.find(key); return it == object.end() ? empty : it->second;
    }
};
struct Parser {
    const std::string& s; size_t p = 0;
    [[noreturn]] void bad() { throw std::runtime_error("Invalid JSON response"); }
    void ws() { while (p < s.size() && std::isspace((unsigned char)s[p])) ++p; }
    bool take(char c) { ws(); if (p < s.size() && s[p] == c) { ++p; return true; } return false; }
    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p == s.size()) bad();
            char c = s[p++]; v <<= 4;
            if (c >= '0' && c <= '9') v += c - '0';
            else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
            else bad();
        }
        return v;
    }
    void utf8(std::string& out, unsigned c) {
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xc0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += (char)(0xe0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        else { out += (char)(0xf0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 63)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
    }
    std::string string() {
        if (!take('"')) bad();
        std::string out;
        while (p < s.size()) {
            unsigned char c = s[p++];
            if (c == '"') return out;
            if (c < 0x20) bad();
            if (c != '\\') { out += (char)c; continue; }
            if (p == s.size()) bad();
            switch (s[p++]) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                unsigned u = hex4();
                if (u >= 0xd800 && u <= 0xdbff) {
                    if (p + 2 > s.size() || s[p++] != '\\' || s[p++] != 'u') bad();
                    unsigned lo = hex4(); if (lo < 0xdc00 || lo > 0xdfff) bad();
                    u = 0x10000 + ((u - 0xd800) << 10) + lo - 0xdc00;
                } else if (u >= 0xdc00 && u <= 0xdfff) bad();
                utf8(out, u); break;
            }
            default: bad();
            }
        }
        bad();
    }
    Json value(unsigned depth = 0) {
        if (depth > 64) bad(); ws(); if (p == s.size()) bad();
        Json j;
        if (s[p] == '"') { j.kind = Json::String; j.text = string(); }
        else if (take('{')) {
            j.kind = Json::Object;
            if (!take('}')) do { auto key = string(); if (!take(':')) bad(); j.object[key] = value(depth + 1); if (take('}')) return j; } while (take(',')); else return j;
            bad();
        } else if (take('[')) {
            j.kind = Json::Array;
            if (!take(']')) do { j.array.push_back(value(depth + 1)); if (take(']')) return j; } while (take(',')); else return j;
            bad();
        } else if (s.compare(p, 4, "null") == 0) { p += 4; }
        else if (s.compare(p, 4, "true") == 0) { p += 4; j.kind = Json::Bool; j.text = "true"; }
        else if (s.compare(p, 5, "false") == 0) { p += 5; j.kind = Json::Bool; j.text = "false"; }
        else {
            j.kind = Json::Number; size_t start = p;
            if (s[p] == '-') ++p;
            if (p == s.size()) bad();
            if (s[p] == '0') ++p;
            else { if (s[p] < '1' || s[p] > '9') bad(); while (p < s.size() && std::isdigit((unsigned char)s[p])) ++p; }
            if (p < s.size() && s[p] == '.') { ++p; size_t b = p; while (p < s.size() && std::isdigit((unsigned char)s[p])) ++p; if (p == b) bad(); }
            if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) { ++p; if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p; size_t b = p; while (p < s.size() && std::isdigit((unsigned char)s[p])) ++p; if (b == p) bad(); }
            j.text = s.substr(start, p - start);
        }
        return j;
    }
};
struct Internet {
    HINTERNET h;
    explicit Internet(HINTERNET value) : h(value) { if (!h) throw std::runtime_error("Network initialization failed"); }
    ~Internet() { WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};
void network(BOOL ok) { if (!ok) throw std::runtime_error("Network request failed (" + std::to_string(GetLastError()) + ")"); }
void cancelled(const std::atomic_bool& c) { if (c.load()) throw std::runtime_error("Cancelled"); }
}

std::string base64(const Bytes& bytes) {
    DWORD n = 0;
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n)) throw std::runtime_error("Base64 failed");
    std::string s(n, 0);
    if (!CryptBinaryToStringA(bytes.data(), (DWORD)bytes.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, s.data(), &n)) throw std::runtime_error("Base64 failed");
    s.resize(n); return s;
}
std::string urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
Bytes png(HBITMAP bitmap, RECT r) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmap> source;
    check(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source));
    ComPtr<IWICBitmapClipper> clip;
    check(factory->CreateBitmapClipper(&clip));
    WICRect wr{r.left, r.top, r.right - r.left, r.bottom - r.top};
    check(clip->Initialize(source.Get(), &wr));
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(wr.Width, wr.Height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(frame->SetPixelFormat(&format));
    check(frame->WriteSource(clip.Get(), nullptr)); check(frame->Commit()); check(encoder->Commit());
    STATSTG stat{}; check(stream->Stat(&stat, STATFLAG_NONAME));
    HGLOBAL memory{}; check(GetHGlobalFromStream(stream.Get(), &memory));
    auto ptr = static_cast<const unsigned char*>(GlobalLock(memory));
    if (!ptr) throw std::runtime_error("PNG memory failed");
    Bytes data(ptr, ptr + stat.cbSize.LowPart); GlobalUnlock(memory); return data;
}
Bytes wav(const Bytes& pcm) {
    Bytes out;
    auto text = [&](const char* s) { out.insert(out.end(), s, s + 4); };
    auto le = [&](unsigned v, unsigned n) { while (n--) { out.push_back((unsigned char)v); v >>= 8; } };
    text("RIFF"); le(36 + (unsigned)pcm.size(), 4); text("WAVE"); text("fmt "); le(16, 4);
    le(1, 2); le(1, 2); le(16000, 4); le(32000, 4); le(2, 2); le(16, 2);
    text("data"); le((unsigned)pcm.size(), 4); out.insert(out.end(), pcm.begin(), pcm.end()); return out;
}
std::wstring parseRecognition(const std::string& json, bool voice) {
    Parser parser{json}; Json j = parser.value(); parser.ws();
    if (parser.p != json.size() || j.kind != Json::Object) throw std::runtime_error("Invalid recognition response");
    const auto& code = j.at("errorCode");
    if (code.kind != Json::Null && code.text != "0") throw std::runtime_error("Youdao error: " + code.text);
    std::string text;
    if (voice) {
        const auto& result = j.at("result");
        if (result.kind == Json::String) text = result.text;
        else if (result.kind == Json::Array) for (const auto& line : result.array) {
            if (line.kind != Json::String) throw std::runtime_error("Invalid ASR result");
            if (!text.empty()) text += "\r\n"; text += line.text;
        }
    } else {
        const auto& lines = j.at("lines");
        if (lines.kind == Json::Array) for (size_t i = 0; i < lines.array.size(); ++i) {
            const auto& words = lines.array[i].at("words");
            if (words.kind != Json::String) throw std::runtime_error("Invalid OCR line");
            if (i) text += "\r\n"; text += words.text;
        }
    }
    if (text.empty()) throw std::runtime_error("No text recognized");
    if (text.find('\0') != std::string::npos) throw std::runtime_error("Invalid text response");
    return wide(text);
}
std::wstring recognize(const Bytes& data, bool voice, bool english, const std::atomic_bool& stop) {
    cancelled(stop);
    std::string body;
    std::wstring path, content;
    if (voice) {
        const std::string boundary = "----ShotOcrWavBoundary";
        path = english ? L"/asr?lang=en" : L"/asr?lang=zh-CHS";
        content = L"Content-Type: multipart/form-data; boundary=----ShotOcrWavBoundary\r\n";
        body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"audioData\"; filename=\"record.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
        body.append((const char*)data.data(), data.size()); body += "\r\n--" + boundary + "--\r\n";
    } else {
        path = L"/ocrapi1"; content = L"Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n";
        body = "imgBase=" + urlEncode("data:image/png;base64," + base64(data)) + "&lang=&company=";
    }
    Internet session(WinHttpOpen(L"ShotOcr/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
    network(WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000));
    Internet connection(WinHttpConnect(session, L"aidemo.youdao.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet request(WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    network(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    cancelled(stop);
    network(WinHttpSendRequest(request, content.c_str(), (DWORD)-1, body.data(), (DWORD)body.size(), (DWORD)body.size(), 0));
    cancelled(stop); network(WinHttpReceiveResponse(request, nullptr)); cancelled(stop);
    DWORD status = 0, size = sizeof(status);
    network(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr));
    if (status != 200) throw std::runtime_error("HTTP " + std::to_string(status));
    std::string response;
    for (;;) {
        cancelled(stop); char buffer[8192]; DWORD read = 0;
        network(WinHttpReadData(request, buffer, sizeof(buffer), &read));
        if (!read) break;
        if (response.size() + read > 2 * 1024 * 1024) throw std::runtime_error("Response too large");
        response.append(buffer, read);
    }
    cancelled(stop); return parseRecognition(response, voice);
}
