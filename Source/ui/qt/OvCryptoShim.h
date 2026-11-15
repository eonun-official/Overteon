// OvCryptoShim.h — repair Qt's OpenSSL imports after a host has polluted the
// process symbol scope (see OvCryptoShim.cpp for the whole story).
#pragma once

namespace ovqt
{
    /// Point libQt6Core's OpenSSL imports at the libcrypto it was linked
    /// against.  Idempotent, silent no-op when nothing is wrong.
    /// Call once, before the first QML engine is created.
    void bindQtToRealLibcrypto();
} // namespace ovqt
