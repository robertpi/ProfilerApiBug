#ifndef CLR_PROFILER_STRING_H_
#define CLR_PROFILER_STRING_H_

#include <corhlpr.h>
#include <string>

namespace trace {

    typedef std::basic_string<WCHAR> WSTRING;
    typedef std::basic_stringstream<WCHAR> WSTRINGSTREAM;

    std::string ToString(const std::string& str);
    std::string ToString(const char* str);
    std::string ToString(const uint64_t i);
    std::string ToString(const WSTRING& wstr);

    WSTRING ToWSTRING(const std::string& str);

    WCHAR operator"" _W(const char c);
    WSTRING operator"" _W(const char* arr, size_t size);
}  // namespace trace

#endif
