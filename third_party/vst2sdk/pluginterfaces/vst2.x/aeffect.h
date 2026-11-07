// ===========================================================================
//  aeffect.h — the VST 2.4 plug-in interface.
//
//  WHY THIS FILE EXISTS
//  JUCE 8 can build VST2 plug-ins but ships no VST2 SDK, because Steinberg
//  stopped distributing it in 2018.  Its wrapper does, literally:
//
//      namespace Vst2 {
//      #include "pluginterfaces/vst2.x/aeffect.h"
//      #include "pluginterfaces/vst2.x/aeffectx.h"
//      }
//
//  so it needs two headers on the include path with those exact names, and the
//  types and constants inside them are the whole ABI: a host loads the .so, calls
//  VSTPluginMain, and reads an AEffect whose LAYOUT must match or nothing works.
//
//  WHERE THE LAYOUT COMES FROM
//  Not from Steinberg's headers.  It comes from the clean-room reimplementation
//  vendored next to this file at ../../vst2sdk-xaymar/ (github.com/Xaymar/vst2sdk,
//  BSD-2-Clause, "An attempt at an untainted clean room reimplementation of the
//  widely popular VST 2.x SDK").  That project exists exactly so that compatible
//  headers can be written without Steinberg's licence, and its vst_effect_t was
//  compared field by field with the struct below:
//
//      vst_effect_t            this file            offset
//      ----------------------  -------------------  ------
//      magic_number            magic                 0
//      control                 dispatcher            8
//      process                 process              16
//      set_parameter           setParameter         24
//      get_parameter           getParameter         32
//      num_programs            numPrograms          40
//      num_params              numParams            44
//      num_inputs              numInputs            48
//      num_outputs             numOutputs           52
//      flags                   flags                56
//      _unknown_00             resvd1               64
//      _unknown_01             resvd2               72
//      delay                   initialDelay         80
//      _unknown_02             realQualities        84
//      _unknown_03             offQualities         88
//      input_output_ratio      ioRatio              92
//      effect_internal         object               96
//      host_internal           user                104
//      unique_id               uniqueID            112
//      version                 version             116
//      process_float           processReplacing    120
//      process_double          processDoubleRep... 128
//      _unknown[56]            future[56]          136
//
//  The two places where this file deliberately does NOT follow Xaymar, because
//  the ABI and therefore JUCE require the other shape:
//
//   * VstEvents.  Xaymar's vst_events_t holds ONE pointer (`events`); the ABI has
//     `VstEvent* events[2]`, and JUCE builds its event list by indexing past that
//     member with offsetof.  A single pointer would make the second event slot
//     land outside the struct.
//   * effSetSpeakerArrangement.  Xaymar uses 0x2A (the VST 2.0 value); hosts in
//     the field use 0x46, which is what VST 2.2+ and JUCE speak.
//
//  NO SYSTEM HEADERS ARE INCLUDED HERE, on purpose: this file is expanded inside
//  `namespace Vst2`, so `#include <cstdint>` would drag the standard library into
//  that namespace.  The integer types therefore come from the compiler's own
//  predefined macros, which need no header.
// ===========================================================================
#pragma once

#ifndef OVERTEON_VST2_AEFFECT_H
#define OVERTEON_VST2_AEFFECT_H

// ---- integer types, without including anything ----------------------------
#if defined(_MSC_VER)
    typedef __int32  VstInt32;
    typedef __int16  VstInt16;
    typedef __int64  VstIntPtr;
#else
    typedef __INT32_TYPE__   VstInt32;
    typedef __INT16_TYPE__   VstInt16;
    typedef __INTPTR_TYPE__  VstIntPtr;
#endif

// ---- the plug-in structure ------------------------------------------------
struct AEffect;

typedef VstIntPtr (*audioMasterCallback) (AEffect*, VstInt32 opcode, VstInt32 index,
                                          VstIntPtr value, void* ptr, float opt);

typedef VstIntPtr (*AEffectDispatcherProc) (AEffect*, VstInt32 opcode, VstInt32 index,
                                            VstIntPtr value, void* ptr, float opt);
typedef void (*AEffectProcessProc) (AEffect*, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void (*AEffectProcessDoubleProc) (AEffect*, double** inputs, double** outputs, VstInt32 sampleFrames);
typedef void (*AEffectSetParameterProc) (AEffect*, VstInt32 index, float parameter);
typedef float (*AEffectGetParameterProc) (AEffect*, VstInt32 index);

typedef struct AEffect
{
    VstInt32 magic;                          // 'VstP', or 'VstP' with kEffectMagic below
    AEffectDispatcherProc      dispatcher;
    AEffectProcessProc         process;      // deprecated in 2.4, kept for layout
    AEffectSetParameterProc    setParameter;
    AEffectGetParameterProc    getParameter;

    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;

    VstIntPtr resvd1;                        // must be 0
    VstIntPtr resvd2;                        // must be 0

    VstInt32 initialDelay;                   // samples of latency
    VstInt32 realQualities;
    VstInt32 offQualities;
    float    ioRatio;

    void*    object;                         // the plug-in's own object
    void*    user;                           // the host's own object

    VstInt32 uniqueID;
    VstInt32 version;

    AEffectProcessProc       processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;

    char future[56];                         // keep this zeroed
} AEffect;

// The magic value every host checks: four characters 'VstP' in big-endian
// order, written as a number so the header needs no helper.
#define kEffectMagic 0x56737450

// ---- flags in AEffect::flags ----------------------------------------------
enum AEffectFlags
{
    effFlagsHasEditor          = 1 << 0,
    effFlagsCanReplacing       = 1 << 4,     // has processReplacing
    effFlagsProgramChunks      = 1 << 5,
    effFlagsIsSynth            = 1 << 8,
    effFlagsNoSoundInStop      = 1 << 9,
    effFlagsCanDoubleReplacing = 1 << 12,
    effFlagsExtIsAsync         = 1 << 20,
    effFlagsExtHasBuffer       = 1 << 21
};

// ---- dispatcher opcodes ----------------------------------------------------
// The numbers are the protocol.  They are not arbitrary and not renumberable.
enum AEffectOpcodes
{
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetProgramName = 4,
    effGetProgramName = 5,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effGetLineNo = 9,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditDraw = 16,
    effEditMouse = 17,
    effEditKey = 18,
    effEditIdle = 19,
    effEditTop = 20,
    effEditSleep = 21,
    effIdentify = 22,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effCanBeAutomated = 26,
    effString2Parameter = 27,
    effGetNumProgramCategories = 28,
    effGetProgramNameIndexed = 29,
    effCopyProgram = 30,
    effConnectInput = 31,
    effConnectOutput = 32,
    effGetInputProperties = 33,
    effGetOutputProperties = 34,
    effGetPlugCategory = 35,
    effGetCurrentPosition = 36,
    effGetDestinationPosition = 37,
    effSetBypass = 44,
    effGetEffectName = 45,
    effGetErrorText = 46,
    effGetVendorString = 47,
    effGetProductString = 48,
    effGetVendorVersion = 49,
    effVendorSpecific = 50,
    effCanDo = 51,
    effGetTailSize = 52,
    effIdle = 53,
    effGetIcon = 54,
    effSetViewPosition = 55,
    effGetParameterProperties = 56,
    effKeysRequired = 57,
    effGetVstVersion = 58,
    effEditKeyDown = 59,
    effEditKeyUp = 60,
    effSetEditKnobMode = 61,
    effGetMidiProgramName = 62,
    effGetCurrentMidiProgram = 63,
    effGetMidiProgramCategory = 64,
    effHasMidiProgramsChanged = 65,
    effGetMidiKeyName = 66,
    effBeginSetProgram = 67,
    effEndSetProgram = 68,
    effGetSpeakerArrangement = 69,
    effSetSpeakerArrangement = 70,
    effSetBlockSizeAndSampleRate = 71,
    effSetProcessPrecision = 77,
    effGetNumMidiInputChannels = 78,
    effGetNumMidiOutputChannels = 79,

    // VST 2.0 legacy opcode, deprecated in 2.4.  It shares the number 46 with
    // effGetErrorText: both appear as 46 in the historical headers, because the
    // older opcode was reused when the newer one was added.  Kept as its own
    // enumerator so the name exists; no 2.4 host sends it.
    effSetTotalSampleToProcess = 46
};

// ---- plug-in categories (effGetPlugCategory) ------------------------------
enum AEffectPlugCategory
{
    kPlugCategUnknown = 0,
    kPlugCategEffect,
    kPlugCategSynth,
    kPlugCategAnalysis,
    kPlugCategMastering,
    kPlugCategSpacializer,
    kPlugCategRoomFx,
    kPlugSurroundFx,
    kPlugCategRestoration,
    kPlugCategOfflineProcess,
    kPlugCategShell,
    kPlugCategGenerator,
    kPlugCategMaxCount
};

// ---- misc limits ----------------------------------------------------------
// kVstVersion is a macro rather than an enumerator on purpose.  JUCE reports it
// from a file-scope function, outside `namespace Vst2`, where an enumerator
// declared inside that namespace would not be visible.
#define kVstVersion 2400

enum
{
    kVstMaxProgNameLen    = 24,
    kVstMaxParamStrLen    = 8,
    kVstMaxVendorStrLen   = 64,
    kVstMaxProductStrLen  = 64,
    kVstMaxEffectNameLen  = 32,
    kVstMaxNameLen        = 64,
    kVstMaxLabelLen       = 64,
    kVstMaxShortLabelLen  = 8,
    kVstMaxCategLabelLen  = 64,
    kVstMaxFileNameLen    = 100
};

// ---- host callback opcodes (audioMaster*) ---------------------------------
enum AudioMasterOpcodes
{
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterCurrentId = 2,
    audioMasterIdle = 3,
    audioMasterPinConnected = 4,
    audioMasterWantMidi = 6,
    audioMasterGetTime = 7,
    audioMasterProcessEvents = 8,
    audioMasterSetTime = 10,
    audioMasterTempoAt = 11,
    audioMasterGetNumAutomatableParameters = 12,
    audioMasterGetParameterQuantization = 13,
    audioMasterIOChanged = 14,
    audioMasterNeedIdle = 15,
    audioMasterSizeWindow = 16,
    audioMasterGetSampleRate = 17,
    audioMasterGetBlockSize = 18,
    audioMasterGetInputLatency = 19,
    audioMasterGetOutputLatency = 20,
    audioMasterGetPreviousPlug = 21,
    audioMasterGetNextPlug = 22,
    audioMasterWillReplaceOrAccumulate = 23,
    audioMasterGetCurrentProcessLevel = 24,
    audioMasterGetAutomationState = 25,
    audioMasterOfflineStart = 26,
    audioMasterOfflineRead = 27,
    audioMasterOfflineWrite = 28,
    audioMasterOfflineGetCurrentPass = 29,
    audioMasterOfflineGetCurrentMetaPass = 30,
    audioMasterSetOutputSampleRate = 31,
    audioMasterGetOutputSpeakerArrangement = 32,
    audioMasterGetLanguage = 33,
    audioMasterOpenWindow = 34,
    audioMasterCloseWindow = 35,
    audioMasterGetDirectory = 36,
    audioMasterUpdateDisplay = 37,
    audioMasterBeginEdit = 38,
    audioMasterEndEdit = 39,
    audioMasterOpenFileSelector = 40,
    audioMasterCloseFileSelector = 41,
    audioMasterEditFile = 42,
    audioMasterGetChunkFile = 43,
    audioMasterGetInputSpeakerArrangement = 44,

    // Not in the core 0..44 run, but JUCE sends it (Ableton-specific host
    // commands) and some headers carry it, so define it rather than leave a
    // hole.  50 is the value those headers use.
    audioMasterVendorSpecific = 50,
    audioMasterCanDo = 51
};

// The 2.4 code uses a second name for host opcodes when it wants to store one
// in a variable: JUCE has `Vst2::AudioMasterOpcodesX opcode;` and initialises it
// from audioMasterUpdateDisplay / audioMasterIOChanged, which live in the enum
// above.  A separate enum type would reject that initialisation (no implicit
// conversion between enum types), so this is an integer type that carries any
// opcode number.
typedef VstInt32 AudioMasterOpcodesX;

// ---- VstTimeInfo, returned by audioMasterGetTime --------------------------
enum VstTimeInfoFlags
{
    kVstTransportChanged     = 1 << 0,
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,
    kVstAutomationWriting    = 1 << 6,
    kVstAutomationReading    = 1 << 7,
    kVstNanosValid           = 1 << 8,
    kVstPpqPosValid          = 1 << 9,
    kVstTempoValid           = 1 << 10,
    kVstBarsValid            = 1 << 11,
    kVstCyclePosValid        = 1 << 12,
    kVstTimeSigValid         = 1 << 13,
    kVstSmpteValid           = 1 << 14,
    kVstClockValid           = 1 << 15
};

enum VstSmpteFrameRate
{
    kVstSmpte24fps    = 0,
    kVstSmpte25fps    = 1,
    kVstSmpte2997fps  = 2,
    kVstSmpte30fps    = 3,
    kVstSmpte2997dfps = 4,
    kVstSmpte30dfps   = 5,
    kVstSmpteFilm16mm = 6,
    kVstSmpteFilm35mm = 7,
    kVstSmpte239fps   = 10,
    kVstSmpte249fps   = 11,
    kVstSmpte599fps   = 12,
    kVstSmpte60fps    = 13
};

typedef struct VstTimeInfo
{
    double  samplePos;
    double  sampleRate;
    double  nanoSeconds;
    double  ppqPos;
    double  tempo;
    double  barStartPos;
    double  cycleStartPos;
    double  cycleEndPos;
    VstInt32 timeSigNumerator;
    VstInt32 timeSigDenominator;
    VstInt32 smpteOffset;
    VstInt32 smpteFrameRate;
    VstInt32 samplesToNextClock;
    VstInt32 flags;
} VstTimeInfo;

// ---- process precision (effSetProcessPrecision) ---------------------------
enum VstProcessPrecision
{
    kVstProcessPrecision32 = 0,
    kVstProcessPrecision64
};

// ---- the entry point every host looks for ---------------------------------
typedef AEffect* (*audioMasterCallbackProc) (audioMasterCallback);

#if defined(_WIN32)
    #define VSTCALLBACK __cdecl
    #define VST_FUNCTION_INTERFACE __cdecl
#else
    #define VSTCALLBACK
    #define VST_FUNCTION_INTERFACE
#endif

#ifdef __cplusplus
extern "C" {
#endif
    // The host resolves this symbol by name.  It is only a declaration here:
    // JUCE's wrapper defines it, in juce_audio_plugin_client_VST2.cpp.
    typedef AEffect* (*VSTPluginMainFn) (audioMasterCallback audioMaster);
#ifdef __cplusplus
}
#endif

#endif // OVERTEON_VST2_AEFFECT_H
