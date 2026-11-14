// OvGlibShim.cpp — the glib symbols a bundled-but-outdated host lacks.
//
// WHY THIS EXISTS
//
// A JUCE LV2 UI on Linux can only be an X11UI, so the host has to load
// libOverteon.so into its own process.  That pulls in Qt6Network:
//
//     Qt6Qml's CMake target has Qt6Network in __qt_Qml_target_deps, and
//     libQt6Qml.so.6 both NEEDs it and references 18 QNetwork* symbols, so it
//     cannot be dropped -- it is what QQmlEngine uses to load a QML file over a
//     URL.
//
// Qt6Network then NEEDs libproxy.so.1, which NEEDs libpxbackend-1.0.so -- and
// libpxbackend is built against a modern glib.  Ardour's official binary ships
// its OWN glib in /opt/Ardour-9.8.0/lib and puts it first on LD_LIBRARY_PATH;
// that copy predates g_uri_parse (glib 2.66), g_strv_builder_* and
// g_string_replace (2.68).  So the moment the host dlopen()s the plug-in:
//
//     lilv_lib_open(): error: Failed to open library .../libOverteon.so
//     (/usr/lib/libproxy/libpxbackend-1.0.so: undefined symbol:
//      g_strv_builder_unref)
//
// and the plug-in simply does not exist as far as the host is concerned --
// nothing to do with our DSP or our UI.
//
// WHY A SHIM AND NOT ONE OF THE OBVIOUS FIXES
//
//   * Dropping Qt6Network: impossible, see above.
//   * LD_PRELOAD the system glib: works, but it is a per-user hack that has to
//     be applied to the host's launcher, and it swaps glib out from under the
//     host (Ardour itself then runs on a glib it was not built against).
//   * Replace libpxbackend: it is a system file and other applications need the
//     real one.
//
// The symbols below are defined here with default visibility.  A dlopen()ed
// library and its dependencies share one symbol scope, so when libpxbackend
// resolves its undefined glib symbols it finds these.  Nothing global is
// touched: the host loads the plug-in RTLD_LOCAL, so the shim is invisible to
// every other plug-in in the same process, and these names do not exist in the
// host's old glib, so there is nothing to shadow.
//
// The implementations are self-contained -- no glib dependency is added, which
// would otherwise drag the host's old glib back into the picture.  They are
// only reached if something actually parses a proxy URI; the plug-in never
// does, and they are written to behave sanely if they are called anyway.
#include <cstdlib>
#include <cstring>
#include <new>

#define OV_SHIM_EXPORT extern "C" __attribute__ ((visibility ("default")))

namespace
{
// ---- GStrvBuilder -------------------------------------------------------
// Public behaviour: build a null-terminated string array.
struct StrvBuilder
{
    char** items = nullptr;
    size_t count = 0;
    size_t cap   = 0;
};

// ---- GUri ---------------------------------------------------------------
// Opaque to callers; only these functions ever look inside it.
struct Uri
{
    char*  full   = nullptr;
    char*  scheme = nullptr;
    char*  host   = nullptr;
    int    port   = -1;
};

char* dupStr (const char* s)
{
    if (s == nullptr) return nullptr;
    const size_t n = std::strlen (s) + 1;
    auto* p = static_cast<char*> (std::malloc (n));
    if (p != nullptr) std::memcpy (p, s, n);
    return p;
}
} // namespace

// ===========================================================================
//  GStrvBuilder  (glib 2.68)
// ===========================================================================
OV_SHIM_EXPORT StrvBuilder* g_strv_builder_new (void)
{
    return new (std::nothrow) StrvBuilder();
}

OV_SHIM_EXPORT void g_strv_builder_unref (StrvBuilder* b)
{
    if (b == nullptr) return;
    for (size_t i = 0; i < b->count; ++i) std::free (b->items[i]);
    std::free (b->items);
    delete b;
}

OV_SHIM_EXPORT void g_strv_builder_add (StrvBuilder* b, const char* value)
{
    if (b == nullptr) return;
    if (b->count + 2 > b->cap)
    {
        const size_t ncap = b->cap == 0 ? 8 : b->cap * 2;
        auto* grown = static_cast<char**> (std::realloc (b->items, ncap * sizeof (char*)));
        if (grown == nullptr) return;
        b->items = grown;
        b->cap   = ncap;
    }
    b->items[b->count++] = dupStr (value != nullptr ? value : "");
    b->items[b->count]   = nullptr;          // always terminated
}

OV_SHIM_EXPORT char** g_strv_builder_end (StrvBuilder* b)
{
    if (b == nullptr) return nullptr;

    // Ownership of the array (and its strings) passes to the caller, so the
    // builder must not free them afterwards.
    char** out = b->items;
    if (out == nullptr)
    {
        out = static_cast<char**> (std::calloc (1, sizeof (char*)));
    }
    b->items = nullptr;
    b->count = 0;
    b->cap   = 0;
    delete b;
    return out;
}

// ===========================================================================
//  g_string_replace  (glib 2.68)
//
//  GString is a public structure: { gchar* str; gsize len; gsize allocated_len; }
//  so it can be maintained here without linking glib.
// ===========================================================================
namespace
{
struct GStringLayout
{
    char*  str;
    size_t len;
    size_t allocated_len;
};
} // namespace

OV_SHIM_EXPORT unsigned g_string_replace (GStringLayout* s, const char* find,
                                          const char* replace, unsigned limit)
{
    if (s == nullptr || s->str == nullptr || find == nullptr || *find == '\0')
        return 0;

    const size_t findLen = std::strlen (find);
    const size_t repLen  = replace != nullptr ? std::strlen (replace) : 0;
    unsigned done = 0;

    char* pos = s->str;
    while ((limit == 0 || done < limit) && (pos = std::strstr (pos, find)) != nullptr)
    {
        const size_t before = static_cast<size_t> (pos - s->str);

        if (repLen <= findLen)
        {
            std::memmove (pos + repLen, pos + findLen, s->len - before - findLen + 1);
            if (repLen > 0) std::memcpy (pos, replace, repLen);
            s->len = s->len - findLen + repLen;
        }
        else
        {
            const size_t grow = repLen - findLen;
            const size_t need = s->len + grow + 1;
            if (need > s->allocated_len)
            {
                size_t ncap = s->allocated_len == 0 ? 32 : s->allocated_len;
                while (ncap < need) ncap *= 2;
                auto* grown = static_cast<char*> (std::realloc (s->str, ncap));
                if (grown == nullptr) return done;
                s->str = grown;
                s->allocated_len = ncap;
            }
            pos = s->str + before;
            std::memmove (pos + repLen, pos + findLen, s->len - before - findLen + 1);
            if (repLen > 0) std::memcpy (pos, replace, repLen);
            s->len += grow;
        }

        pos += repLen;
        ++done;
    }
    return done;
}

// ===========================================================================
//  GUri  (glib 2.66)
// ===========================================================================
OV_SHIM_EXPORT Uri* g_uri_parse (const char* text, int /*flags*/, void** /*error*/)
{
    if (text == nullptr) return nullptr;

    auto* u = new (std::nothrow) Uri();
    if (u == nullptr) return nullptr;
    u->full = dupStr (text);

    // scheme://[host][:port]...
    const char* sep = std::strstr (text, "://");
    if (sep != nullptr)
    {
        const size_t schemeLen = static_cast<size_t> (sep - text);
        u->scheme = static_cast<char*> (std::malloc (schemeLen + 1));
        if (u->scheme != nullptr)
        {
            std::memcpy (u->scheme, text, schemeLen);
            u->scheme[schemeLen] = '\0';
        }

        const char* hostStart = sep + 3;
        const char* hostEnd   = hostStart;
        while (*hostEnd != '\0' && *hostEnd != '/' && *hostEnd != ':'
               && *hostEnd != '?' && *hostEnd != '#')
            ++hostEnd;

        const size_t hostLen = static_cast<size_t> (hostEnd - hostStart);
        u->host = static_cast<char*> (std::malloc (hostLen + 1));
        if (u->host != nullptr)
        {
            std::memcpy (u->host, hostStart, hostLen);
            u->host[hostLen] = '\0';
        }

        if (*hostEnd == ':')
            u->port = std::atoi (hostEnd + 1);
    }
    else if (const char* colon = std::strchr (text, ':'))
    {
        const size_t schemeLen = static_cast<size_t> (colon - text);
        u->scheme = static_cast<char*> (std::malloc (schemeLen + 1));
        if (u->scheme != nullptr)
        {
            std::memcpy (u->scheme, text, schemeLen);
            u->scheme[schemeLen] = '\0';
        }
    }
    return u;
}

OV_SHIM_EXPORT char* g_uri_to_string (Uri* u)
{
    return u != nullptr ? dupStr (u->full) : nullptr;
}

OV_SHIM_EXPORT Uri* g_uri_ref (Uri* u)   { return u; }

OV_SHIM_EXPORT void g_uri_unref (Uri* u)
{
    if (u == nullptr) return;
    std::free (u->full);
    std::free (u->scheme);
    std::free (u->host);
    delete u;
}

// glib returns these as borrowed pointers that stay valid while the GUri lives.
OV_SHIM_EXPORT const char* g_uri_get_scheme (Uri* u) { return u != nullptr ? u->scheme : nullptr; }
OV_SHIM_EXPORT const char* g_uri_get_host   (Uri* u) { return u != nullptr ? u->host   : nullptr; }
OV_SHIM_EXPORT int         g_uri_get_port   (Uri* u) { return u != nullptr ? u->port   : -1; }

// ===========================================================================
//  g_memdup2  (glib 2.68)
//
//  Replaces the deprecated g_memdup, which took a guint length and so could
//  truncate a copy on 64-bit.  Size is a gsize here for that reason.
// ===========================================================================
OV_SHIM_EXPORT void* g_memdup2 (const void* src, size_t n)
{
    if (src == nullptr || n == 0) return nullptr;
    auto* out = static_cast<char*> (std::malloc (n));
    if (out != nullptr) std::memcpy (out, src, n);
    return out;
}

// ===========================================================================
//  g_once_init_*_pointer  (glib 2.80)
//
//  A one-shot latch: the first caller gets TRUE and must publish a value with
//  g_once_init_leave_pointer(); everyone else gets FALSE.  The sentinel used
//  while initialisation is in flight is 1, which is what glib uses, so a value
//  published here is readable by real glib code and vice versa.
// ===========================================================================
OV_SHIM_EXPORT int g_once_init_enter_pointer (void* location)
{
    auto* slot = static_cast<void**> (location);
    if (__atomic_load_n (slot, __ATOMIC_ACQUIRE) != nullptr)
        return 0;

    void* expected = nullptr;
    return __atomic_compare_exchange_n (slot, &expected, (void*) 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}

OV_SHIM_EXPORT void g_once_init_leave_pointer (void* location, void* result)
{
    auto* slot = static_cast<void**> (location);
    __atomic_store_n (slot, result != nullptr ? result : (void*) 1, __ATOMIC_RELEASE);
}
