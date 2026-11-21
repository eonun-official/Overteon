# Slang SPIR-V to Qt shader packages

This build-host tool accepts Slang's SPIR-V and writes the QSB package consumed
by Qt Quick ShaderEffect. It uses Qt ShaderTools' SPIR-V reflection and backend
translators directly. There is no GLSL source, intermediate, or output variant.
The package contains SPIR-V 1.0, HLSL Shader Model 5.0, and MSL 1.2, including
Qt's native binding maps. Output uses Qt 6.4's serialized QSB format.

```sh
cmake -S Tools/slang-qsb -B Build/tools/slang-qsb -G Ninja
cmake --build Build/tools/slang-qsb
Build/tools/slang-qsb/overteon-slang-qsb frag blur.spv blur.frag.qsb
```

The first argument is `vert` or `frag`. The input module must have exactly one
entry point with the matching SPIR-V execution model. A fourth argument can
assert the input entry-point name. The tool normalizes it to `main` for Vulkan
and HLSL; Qt's Metal translation uses `main0`.

Only the host tool links Qt's private ShaderTools and Gui modules. It must be
rebuilt against the installed host Qt development kit when that kit changes;
none of these private headers or library dependencies enter the plug-in.
The current implementation is validated against Qt 6.12.0.
