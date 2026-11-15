#pragma once
// ============================================================
//  OvEnv — debug switches read by VALUE, not by presence.
//
//  Every switch in this UI used to be `std::getenv ("NAME")`, which tests
//  whether the variable EXISTS.  That makes `NAME=0` mean the opposite of what
//  it says: it is still non-null, so the switch is ON.
//
//  This is not theoretical.  `OVERTEON_ONLYBG=1` hides the spectrum fill so the
//  background layer can be inspected on its own; turning it back off with
//  `OVERTEON_ONLYBG=0` left the fill hidden and read as "the spectrum is gone".
//  The same trap sits on every other switch here, and it only ever bites in the
//  one direction nobody expects.
//
//  So: unset means off (unchanged behaviour for a normal run), an empty value
//  means on (`NAME=` is the usual way to say "enable this"), and an explicit
//  false word means off.
// ============================================================
#include <cstdlib>
#include <cstring>

// strcasecmp is POSIX; MSVC spells it _stricmp and declares it in <string.h>.
// Everything below needs a case-insensitive compare, so map the name once.
#if defined(_WIN32)
 #define ov_strcasecmp _stricmp
#else
 #include <strings.h>
 #define ov_strcasecmp strcasecmp
#endif

/// True when `name` is set to something that is not an explicit false.
inline bool ovEnvFlag (const char* name)
{
    const char* v = std::getenv (name);
    if (v == nullptr) return false;
    if (*v == '\0')   return true;

    return ! (ov_strcasecmp (v, "0")     == 0
           || ov_strcasecmp (v, "false") == 0
           || ov_strcasecmp (v, "no")    == 0
           || ov_strcasecmp (v, "off")   == 0);
}

/// Same parsing, but ON unless it is explicitly turned off.
///
/// For a switch whose desirable state is "on" and whose off state has to be
/// reachable from the environment.  Unset, empty and any non-false word are all
/// ON here — the opposite default to ovEnvFlag(), which exists for debug
/// output that must stay silent unless asked for.
inline bool ovEnvFlagDefaultOn (const char* name)
{
    const char* v = std::getenv (name);
    if (v == nullptr) return true;
    if (*v == '\0')   return true;

    return ! (ov_strcasecmp (v, "0")     == 0
           || ov_strcasecmp (v, "false") == 0
           || ov_strcasecmp (v, "no")    == 0
           || ov_strcasecmp (v, "off")   == 0);
}
