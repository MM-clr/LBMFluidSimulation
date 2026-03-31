#pragma once
#include <string>
#include <Windows.h>

// Shift_JIS -> UTF-8
inline std::string SjisToUtf8(const char* sjis)
{
    if (!sjis || sjis[0] == '\0') return std::string();

    // CP_SHIFT_JIS == 932
    int wlen = ::MultiByteToWideChar(932, 0, sjis, -1, nullptr, 0);
    if (wlen == 0) return std::string();

    std::wstring w;
    w.resize(wlen - 1);
    ::MultiByteToWideChar(932, 0, sjis, -1, &w[0], wlen);

    int utf8len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) return std::string();

    std::string utf8;
    utf8.resize(utf8len - 1);
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &utf8[0], utf8len, nullptr, nullptr);
    return utf8;
}

// wide (L"...") -> UTF-8
inline std::string WideToUtf8(const wchar_t* w)
{
    if (!w || w[0] == L'\0') return std::string();
    int utf8len = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) return std::string();
    std::string utf8;
    utf8.resize(utf8len - 1);
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &utf8[0], utf8len, nullptr, nullptr);
    return utf8;
}
