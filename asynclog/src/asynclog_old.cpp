#define NDEBUG
#include "dlog.hpp"

#include <memory>
#include <algorithm>
#include <new>
#include <thread>
#include <deque>

#include <sstream> // TODO probably won't need this when all is said and done
#include <iostream>

#include <cstring>
#include <cassert>
#include <cstdio>
#include <cmath>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

namespace {
    int const g_page_size = static_cast<int>(sysconf(_SC_PAGESIZE));
}

namespace dlog {
    namespace detail {
        void output_worker();

#ifdef ASYNCLOG_USE_CXX11_THREAD_LOCAL
        thread_local input_buffer tls_input_buffer; 
        thread_local input_buffer* tls_pinput_buffer; 
#else
        input_buffer* tls_pinput_buffer; 
#endif
        std::size_t const TLS_INPUT_BUFFER_SIZE = 8*4096;   // static

        shared_input_queue_t g_shared_input_queue;
        spsc_event g_shared_input_consumed_event;
        spsc_event g_shared_input_queue_full_event;

        std::unique_ptr<output_buffer> g_poutput_buffer;
        std::thread g_output_thread;
    }
}

void dlog::initialize(writer* pwriter)
{
    initialize(pwriter, 0, 0);
}

void dlog::initialize(writer* pwriter, std::size_t max_output_buffer_size)
{
    using namespace detail;

    if(max_output_buffer_size == 0)
        max_output_buffer_size = 1024*1024;

    g_poutput_buffer.reset(new output_buffer(pwriter, max_output_buffer_size));

    g_output_thread = move(std::thread(&output_worker));
}

// TODO dlog::crash_cleanup or panic_cleanup or similar, just dump everything
// to disk in the event of a crash.
void dlog::cleanup()
{
    using namespace detail;
    commit();
    queue_commit_extent({nullptr, 0});
    g_output_thread.join();
    assert(g_input_queue.empty());
    g_poutput_buffer.reset();
}

namespace dlog {
namespace {
    template <typename T>
    bool generic_format_int(output_buffer* pbuffer, char const*& pformat, T v)
    {
        char f = *pformat;
        if(f == 'd') {
            std::ostringstream ostr;
            ostr << v;
            std::string const& s = ostr.str();
            char* p = pbuffer->reserve(s.size());
            std::memcpy(p, s.data(), s.size());
            pbuffer->commit(s.size());
            pformat += 1;
            return true;
        } else if(f == 'x') {
            // FIXME
            return false;
        } else if(f == 'b') {
            // FIXME
            return false;
        } else {
            return false;
        }
    }

    int typesafe_sprintf(char* str, double v)
    {
        return sprintf(str, "%f", v);
    }

    int typesafe_sprintf(char* str, long double v)
    {
        return sprintf(str, "%Lf", v);
    }

    template <typename T>
    bool generic_format_float(output_buffer* pbuffer, char const*& pformat, T v)
    {
        char f = *pformat;
        if(f != 'd')
            return false;

        T v_for_counting_digits = std::fabs(v);
        if(v_for_counting_digits < 1.0)
            v_for_counting_digits = static_cast<T>(1.0);
        std::size_t digits = static_cast<std::size_t>(std::log10(v_for_counting_digits)) + 1;
        // %f without precision modifier gives us 6 digits after the decimal point.
        // The format is [-]ddd.dddddd (minimum 9 chars). We can also get e.g.
        // "-infinity" but that won't be more chars than the digits.
        char* p = pbuffer->reserve(1+digits+1+6+1);
        int written = typesafe_sprintf(p, v);
        pbuffer->commit(written);
        pformat += 1;
        return true;
    }

    template <typename T>
    bool generic_format_char(output_buffer* pbuffer, char const*& pformat, T v)
    {
        char f = *pformat;
        if(f == 's') {
            char* p = pbuffer->reserve(1);
            *p = static_cast<char>(v);
            pbuffer->commit(1);
            pformat += 1;
            return true;
        } else {
            return generic_format_int(pbuffer, pformat, static_cast<int>(v));
        }
    }
}   // anonymous namespace
}   // namespace dlog

bool dlog::format(output_buffer* pbuffer, char const*& pformat, char v)
{
    return generic_format_char(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, signed char v)
{
    return generic_format_char(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, unsigned char v)
{
    return generic_format_char(pbuffer, pformat, v);
}

//bool dlog::format(output_buffer* pbuffer, char const*& pformat, wchar_t v);
//bool dlog::format(output_buffer* pbuffer, char const*& pformat, char16_t v);
//bool dlog::format(output_buffer* pbuffer, char const*& pformat, char32_t v);

bool dlog::format(output_buffer* pbuffer, char const*& pformat, short v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, unsigned short v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, int v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, unsigned int v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, long v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, unsigned long v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, long long v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, unsigned long long v)
{
    return generic_format_int(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, float v)
{
    return generic_format_float(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, double v)
{
    return generic_format_float(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, long double v)
{
    return generic_format_float(pbuffer, pformat, v);
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, char const* v)
{
    if(*pformat != 's')
        return false;
    auto len = std::strlen(v);
    char* p = pbuffer->reserve(len);
    std::memcpy(p, v, len);
    pbuffer->commit(len);
    ++pformat;
    return true;
}

bool dlog::format(output_buffer* pbuffer, char const*& pformat, std::string const& v)
{
    if(*pformat != 's')
        return false;
    auto len = v.size();
    char* p = pbuffer->reserve(len);
    std::memcpy(p, v.data(), len);
    pbuffer->commit(len);
    ++pformat;
    return true;
}


void dlog::formatter::append_percent(output_buffer* pbuffer)
{
    auto p = pbuffer->reserve(1u);
    *p = '%';
    pbuffer->commit(1u);
}

char const* dlog::formatter::next_specifier(output_buffer* pbuffer,
        char const* pformat)
{
    while(true) {
        char const* pspecifier = std::strchr(pformat, '%');
        if(pspecifier == nullptr) {
            format(pbuffer, pformat);
            return nullptr;
        }

        auto len = pspecifier - pformat;
        auto p = pbuffer->reserve(len);
        std::memcpy(p, pformat, len);
        pbuffer->commit(len);

        pformat = pspecifier + 1;

        if(*pformat != '%')
            return pformat;

        ++pformat;
        append_percent(pbuffer);
    }
}

void dlog::commit()
{
    using namespace detail;
    auto pib = get_input_buffer();
    pib->commit();
}

