// Package Slang's SPIR-V directly for Qt Quick. QSpirvShader supplies reflection
// and backend translation without compiling or generating a GLSL intermediate.
// Its private API is deliberately isolated in this host-only executable.
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>
#include <QtShaderTools/private/qspirvshader_p.h>
#include <rhi/qshader.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

// Validate the stage from OpEntryPoint rather than trusting a filename. The
// Qt cross compiler selects a single entry; reject multi-entry modules instead
// of silently reflecting and translating a different entry than Vulkan uses.
QByteArray checkedSpirv(QByteArray bytes, QShader::Stage stage, const QByteArray& requestedEntry)
{
    if (bytes.size() < 20 || bytes.size() % 4 != 0)
        fail(QStringLiteral("SPIR-V has an invalid byte length"));
    std::vector<quint32> words(size_t(bytes.size() / 4));
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = qFromLittleEndian<quint32>(bytes.constData() + i * 4);
    if (words[0] != 0x07230203)
        fail(QStringLiteral("Input is not little-endian SPIR-V"));

    const quint32 expectedModel = stage == QShader::VertexStage ? 0 : 4;
    size_t entryAt = 0, entrySize = 0, entryNameWords = 0;
    QByteArray entryName;
    for (size_t i = 5; i < words.size();) {
        const auto count = words[i] >> 16;
        const auto opcode = words[i] & 0xffff;
        if (count == 0 || count > words.size() - i)
            fail(QStringLiteral("SPIR-V instruction extends outside the module"));
        if (opcode == 15) { // OpEntryPoint
            if (entryAt != 0 || count < 4)
                fail(QStringLiteral("Expected exactly one valid SPIR-V entry point"));
            if (words[i + 1] != expectedModel)
                fail(QStringLiteral("SPIR-V execution model does not match requested stage"));
            entryAt = i;
            entrySize = count;
            bool terminated = false;
            for (size_t j = i + 3; j < i + count && !terminated; ++j) {
                for (int byte = 0; byte < 4; ++byte) {
                    const char c = char((words[j] >> (byte * 8)) & 0xff);
                    if (c == 0) { terminated = true; break; }
                    entryName.append(c);
                }
                ++entryNameWords;
            }
            if (!terminated)
                fail(QStringLiteral("SPIR-V entry-point name is unterminated"));
        }
        i += count;
    }
    if (entryAt == 0)
        fail(QStringLiteral("SPIR-V entry point is missing"));
    if (!requestedEntry.isEmpty() && requestedEntry != entryName)
        fail(QStringLiteral("Requested entry point does not match SPIR-V: %1")
             .arg(QString::fromUtf8(entryName)));

    // Qt's translator uses the conventional entry names main (HLSL) and main0
    // (MSL). Normalize only OpEntryPoint's string, preserving IDs/interfaces.
    if (entryName != "main") {
        const auto afterName = entryAt + 3 + entryNameWords;
        std::vector<quint32> result(words.begin(), words.begin() + entryAt);
        result.push_back((quint32(entrySize - entryNameWords + 2) << 16) | 15);
        result.push_back(words[entryAt + 1]);
        result.push_back(words[entryAt + 2]);
        result.push_back(0x6e69616d); // "main"
        result.push_back(0);
        result.insert(result.end(), words.begin() + afterName, words.end());
        words.swap(result);
    }
    bytes.resize(qsizetype(words.size() * 4));
    for (size_t i = 0; i < words.size(); ++i)
        qToLittleEndian(words[i], bytes.data() + i * 4);
    return bytes;
}

void writeShader(const QByteArray& spirv, QShader::Stage stage, const QString& output)
{
    QSpirvShader source;
    source.setSpirvBinary(spirv, stage);
    const auto description = source.shaderDescription();
    if (description.uniformBlocks().isEmpty())
        fail(QStringLiteral("Shader has no reflected Qt uniform block"));

    QShader shader;
    shader.setStage(stage);
    shader.setDescription(description);
    const QShaderKey spirvKey(QShader::SpirvShader, QShaderVersion(100));
    shader.setShader(spirvKey, QShaderCode(spirv, "main"));

    QShader::NativeResourceBindingMap hlslBindings;
    const auto hlsl = source.translateToHLSL(50, &hlslBindings);
    if (hlsl.isEmpty())
        fail(QStringLiteral("HLSL translation failed: %1").arg(source.translationErrorMessage()));
    const QShaderKey hlslKey(QShader::HlslShader, QShaderVersion(50));
    shader.setShader(hlslKey, QShaderCode(hlsl, "main"));
    shader.setResourceBindingMap(hlslKey, hlslBindings);

    QShader::NativeResourceBindingMap mslBindings;
    QShader::NativeShaderInfo mslInfo;
    const auto msl = source.translateToMSL(12, {}, stage, &mslBindings, &mslInfo,
                                         QSpirvShader::MultiViewInfo{},
                                         QSpirvShader::TessellationInfo{});
    if (msl.isEmpty())
        fail(QStringLiteral("MSL translation failed: %1").arg(source.translationErrorMessage()));
    const QShaderKey mslKey(QShader::MslShader, QShaderVersion(12));
    shader.setShader(mslKey, QShaderCode(msl, "main0"));
    shader.setResourceBindingMap(mslKey, mslBindings);
    shader.setNativeShaderInfo(mslKey, mslInfo);

    QSaveFile file(output);
    if (!file.open(QIODevice::WriteOnly))
        fail(file.errorString());
    const auto packed = shader.serialized(QShader::SerializedFormatVersion::Qt_6_4);
    if (file.write(packed) != packed.size() || !file.commit())
        fail(file.errorString());
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4 && args.size() != 5) {
        QTextStream(stderr) << "Usage: overteon-slang-qsb vert|frag INPUT.spv OUTPUT.qsb [ENTRY]\n";
        return 2;
    }
    try {
        QShader::Stage stage;
        if (args[1] == "vert") stage = QShader::VertexStage;
        else if (args[1] == "frag") stage = QShader::FragmentStage;
        else fail(QStringLiteral("Stage must be vert or frag"));
        QFile file(args[2]);
        if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
        const auto spirv = checkedSpirv(file.readAll(), stage,
                                        args.size() == 5 ? args[4].toUtf8() : QByteArray());
        writeShader(spirv, stage, args[3]);
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << "overteon-slang-qsb: " << error.what() << '\n';
        return 1;
    }
}
