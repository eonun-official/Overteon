cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
#  Entry point
#
#  Everything real lives in Config/CMakeLists.txt; this file exists so that the
#  conventional flow works:
#
#      mkdir build && cd build && cmake .. && ninja vst3
#
#  Both entry points configure the same project and produce the same products:
#
#      cmake -S .      -B Build/host     # from the repository root
#      cmake -S Config -B Build/host     # straight at the build system
#
#  Config/ resolves its own paths relative to itself (../Source, ../Tools,
#  ../Assets), which is why forwarding here rather than moving the file keeps
#  every path in the build system relative.
# ---------------------------------------------------------------------------
add_subdirectory(Config)
