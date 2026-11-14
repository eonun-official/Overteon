// OvCryptoShim.cpp — when the host lets another plug-in's OpenSSL win.
//
// THE CRASH
//
// Ardour and Mixbus load plug-ins into their own process.  Ardour 9's bundled
// LV2 plug-ins under lib/LV2/Harrison.lv2/ statically link an old OpenSSL and
// export it with default visibility -- `nm -D` on any of them says
//
//     0000000000347650 T EVP_DigestInit_ex
//
// They are loaded during session setup, so they land in the process-wide
// (global) symbol scope long before a plug-in UI is opened.  libcrypto arrives
// later, behind libQt6Core, and glibc searches the global scope before the
// scope of a dlopen()ed group -- so every OpenSSL relocation inside
// libQt6Core binds to the Harrison copy instead of the library it was built
// against.
//
// That is not survivable, because the two are different OpenSSL generations.
// libQt6Core calls EVP_MD_fetch() (a 3.x/4.x API, which Harrison does not
// export, so it resolves to the system library) and then hands that EVP_MD to
// Harrison's 1.1-era EVP_DigestInit_ex, which reads it as its own structure:
//
//     #0 0x0000004000000000            <- the jump the corrupted struct produced
//     #1 QCryptographicHash::QCryptographicHash(Algorithm)
//     #2 QV4::CompiledData::CompilationUnit::localCacheFilePath(QUrl const&)
//     #3 QV4::CompiledData::CompilationUnit::loadFromDisk(...)
//
// i.e. the UI dies the moment Qt compiles any QML -- which is the first thing
// our editor does.  Disabling the QML disk cache only moves the crash to the
// next hashing site (JSUnitGenerator::generateUnitChecksum); Qt hashes on every
// compile, so there is no "avoid hashing" route.
//
// WHY THIS FILE AND NOT SOMETHING SIMPLER
//
//   * Bundling our own libcrypto: does not help.  The global scope is searched
//     first, so a library loaded by us loses to the host's plug-in no matter
//     what we ship (verified: even with the plug-in opened RTLD_LOCAL, the
//     global-scope definition wins for relocations inside a dependency).
//   * Asking the host to load us RTLD_LOCAL: not ours to decide, and it would
//     not help for the reason just given.
//   * Exporting correct EVP_* from this plug-in: same ordering problem.
//   * LD_PRELOAD: a per-user hack on the host's launcher, and it swaps OpenSSL
//     out from under the host itself.
//
// What is left is to repair the damage directly: libQt6Core has 14 relocations
// against libcrypto, whose names we can read out of its own dynamic table.  For
// each of those we ask the *correct* library -- the very DT_NEEDED entry that
// libQt6Core was linked against -- for the address and write it into the slot.
// Nothing else in the process is touched, no symbol is added, no other library's
// relocations are altered, and when the host is well-behaved every slot already
// holds the right pointer and this function writes nothing.
#include "OvCryptoShim.h"

#include "OvEnv.h"

#include <link.h>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(__x86_64__)
 #define OV_ELF_MACHINE_OK 1
#endif

namespace
{
#if defined(OV_ELF_MACHINE_OK)

constexpr const char* kQtCore   = "libQt6Core.so";
constexpr const char* kCryptoSo = "libcrypto.so";

// Only these families are ever rewritten.  A name that libcrypto also defines
// is the real test, but narrowing by prefix first means a surprising same-name
// symbol elsewhere can never be touched.
bool looksLikeCrypto (const char* n)
{
    static const char* const prefixes[] = {
        "EVP_", "OSSL_", "OPENSSL_", "CRYPTO_", "ERR_", "BN_", "RAND_",
        "HMAC_", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512", "MD5_", "AES_"
    };

    for (const char* p : prefixes)
        if (std::strncmp (n, p, std::strlen (p)) == 0)
            return true;
    return false;
}

struct QtCore
{
    uintptr_t          base  = 0;
    const ElfW(Phdr)*  phdr  = nullptr;
    int                phnum = 0;
    std::string        path;
};

int findQtCore (dl_phdr_info* info, size_t, void* data)
{
    auto* out = static_cast<QtCore*> (data);
    if (info->dlpi_name == nullptr || std::strstr (info->dlpi_name, kQtCore) == nullptr)
        return 0;

    out->base  = info->dlpi_addr;
    out->phdr  = info->dlpi_phdr;
    out->phnum = info->dlpi_phnum;
    out->path  = info->dlpi_name;
    return 1;                     // stop: the first match is the loaded object
}

const char* baseName (const char* p)
{
    const char* slash = std::strrchr (p, '/');
    return slash != nullptr ? slash + 1 : p;
}

#endif // OV_ELF_MACHINE_OK

} // namespace

namespace ovqt
{
void bindQtToRealLibcrypto()
{
#if defined(OV_ELF_MACHINE_OK)
    static std::once_flag once;
    std::call_once (once, []
    {
        const bool verbose = ovEnvFlag ("OVERTEON_CRYPTOFIX_DEBUG");

        if (ovEnvFlag ("OVERTEON_NOCRYPTFIX"))
        {
            if (verbose) std::fprintf (stderr, "[crypto] disabled by OVERTEON_NOCRYPTFIX\n");
            return;
        }

        QtCore qt;
        dl_iterate_phdr (&findQtCore, &qt);
        if (qt.phdr == nullptr || qt.base == 0)
        {
            if (verbose) std::fprintf (stderr, "[crypto] libQt6Core not found; nothing to do\n");
            return;
        }

        const ElfW(Dyn)* dyn = nullptr;
        for (int i = 0; i < qt.phnum; ++i)
            if (qt.phdr[i].p_type == PT_DYNAMIC)
                dyn = reinterpret_cast<const ElfW(Dyn)*> (qt.base + qt.phdr[i].p_vaddr);

        if (dyn == nullptr)
            return;

        const ElfW(Sym)* symtab  = nullptr;
        const char*      strtab  = nullptr;
        const ElfW(Rela)* rela   = nullptr;
        size_t           relasz  = 0;
        const ElfW(Rela)* jmprel = nullptr;
        size_t           pltrelsz = 0;
        size_t           pltrel  = 0;
        std::string      cryptoName;

        // DT_NEEDED indices refer to offsets into the same string table, so the
        // needed names can only be read once strtab is known.
        std::vector<ElfW(Xword)> needed;

        for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d)
        {
            switch (d->d_tag)
            {
                case DT_SYMTAB:   symtab   = reinterpret_cast<const ElfW(Sym)*> (d->d_un.d_ptr); break;
                case DT_STRTAB:   strtab   = reinterpret_cast<const char*> (d->d_un.d_ptr); break;
                case DT_RELA:     rela     = reinterpret_cast<const ElfW(Rela)*> (d->d_un.d_ptr); break;
                case DT_RELASZ:   relasz   = d->d_un.d_val; break;
                case DT_JMPREL:   jmprel   = reinterpret_cast<const ElfW(Rela)*> (d->d_un.d_ptr); break;
                case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
                case DT_PLTREL:   pltrel   = d->d_un.d_val; break;
                case DT_NEEDED:   needed.push_back (d->d_un.d_val); break;
                default: break;
            }
        }

        if (symtab == nullptr || strtab == nullptr)
            return;

        for (auto off : needed)
        {
            const char* n = baseName (strtab + off);
            if (std::strncmp (n, kCryptoSo, std::strlen (kCryptoSo)) == 0)
            {
                cryptoName = n;
                break;
            }
        }

        if (cryptoName.empty())
        {
            if (verbose) std::fprintf (stderr, "[crypto] libQt6Core does not link libcrypto\n");
            return;
        }

        // The library libQt6Core was built against.  dlsym() on this handle
        // searches its own dependency closure first, so it returns the real
        // implementation regardless of what the host put in the global scope.
        void* real = dlopen (cryptoName.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (real == nullptr)
        {
            if (verbose) std::fprintf (stderr, "[crypto] cannot open %s: %s\n",
                                       cryptoName.c_str(), dlerror());
            return;
        }

        const long pageSize = ::sysconf (_SC_PAGESIZE);
        int patched = 0;
        int seen    = 0;

        auto fixOne = [&] (const ElfW(Rela)* r)
        {
            const unsigned type = ELF64_R_TYPE (r->r_info);
            if (type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT)
                return;

            const unsigned symIdx = ELF64_R_SYM (r->r_info);
            const char* name = strtab + symtab[symIdx].st_name;

            if (name[0] == '\0' || ! looksLikeCrypto (name))
                return;

            void* correct = dlsym (real, name);
            if (correct == nullptr)
                return;              // not something this libcrypto provides

            ++seen;

            auto** slot = reinterpret_cast<void**> (qt.base + r->r_offset);
            if (*slot == correct)
                return;              // already bound to the right place

            // RELRO made the slot read-only; open just that page.
            const uintptr_t addr = reinterpret_cast<uintptr_t> (slot);
            void* pageStart = reinterpret_cast<void*> (addr & ~uintptr_t (pageSize - 1));
            const size_t span = size_t (addr - reinterpret_cast<uintptr_t> (pageStart)) + sizeof (void*);

            if (mprotect (pageStart, span, PROT_READ | PROT_WRITE) != 0)
            {
                if (verbose) std::fprintf (stderr, "[crypto] mprotect failed for %s\n", name);
                return;
            }

            void* had = *slot;
            *slot = correct;
            mprotect (pageStart, span, PROT_READ);

            ++patched;
            if (verbose)
                std::fprintf (stderr, "[crypto] %-24s %p -> %p (was %p)\n",
                              name, (void*) slot, correct, had);
        };

        if (rela != nullptr)
            for (size_t i = 0; i < relasz / sizeof (ElfW(Rela)); ++i)
                fixOne (&rela[i]);

        if (jmprel != nullptr && pltrel == DT_RELA)
            for (size_t i = 0; i < pltrelsz / sizeof (ElfW(Rela)); ++i)
                fixOne (&jmprel[i]);

        if (verbose || patched > 0)
            std::fprintf (stderr, "[crypto] %s: %d of %d libcrypto import(s) rebound\n",
                          qt.path.c_str(), patched, seen);
    });
#else
    // Nothing to repair on architectures this file was not written for.
#endif
}

} // namespace ovqt
