#!/bin/sh
# ===========================================================================
#  product.sh — find the directory this configure writes products into.
#
#  CMake names it
#
#      Build/<BuildName>_<Time>_<Host>_<OS>_<Platform>
#
#  so its exact name changes with the build, the host and the platform.  Scripts
#  must not hard-code it -- they source this file and call product_dir.
#
#      . "$HERE/product.sh"
#      PRODUCT=$(product_dir linux)          # or windows, darwin, ...
#      "$PRODUCT/overteon-qml-preview"
#
#  The newest matching directory wins: the Time field is YYYYmmdd-HHMMSS, so with
#  LC_ALL=C the lexical order is the chronological order.  Set OVERTEON_PRODUCT
#  to point at one explicitly (useful for testing a build that is not the newest).
#
#  Prints nothing and returns 1 when nothing has been built for that OS yet.
# ===========================================================================

product_dir() {
    _pd_os=${1:-linux}

    if [ -n "${OVERTEON_PRODUCT:-}" ]; then
        printf '%s\n' "$OVERTEON_PRODUCT"
        return 0
    fi

    # Build/host and Build/win are ninja scratch trees; they are skipped by the
    # pattern below because neither ends in _<os>_<platform>.
    _pd_latest=
    for _pd_d in "$ROOT"/Build/*_"$_pd_os"_*/; do
        [ -d "$_pd_d" ] || continue
        _pd_latest=$_pd_d
    done

    if [ -z "$_pd_latest" ]; then
        return 1
    fi
    printf '%s\n' "${_pd_latest%/}"
}

# where ninja keeps its cache for a given OS
product_tree() {
    case "${1:-linux}" in
        windows) printf '%s\n' "$ROOT/Build/win" ;;
        *)       printf '%s\n' "$ROOT/Build/host" ;;
    esac
}
