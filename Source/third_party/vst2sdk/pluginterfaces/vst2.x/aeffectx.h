// ===========================================================================
//  aeffectx.h — the VST 2.4 extensions on top of aeffect.h.
//
//  Events (MIDI), pin properties and speaker arrangements.  Same rules as
//  aeffect.h: the struct layouts are the ABI, and nothing here may include a
//  system header because this file is expanded inside `namespace Vst2`.
//
//  Where this differs from the clean-room reference in ../../vst2sdk-xaymar/,
//  it is deliberate and noted at the definition.
// ===========================================================================
#pragma once

#ifndef OVERTEON_VST2_AEFFECTX_H
#define OVERTEON_VST2_AEFFECTX_H

#include "aeffect.h"

// ===========================================================================
//  Events
// ===========================================================================

enum VstEventTypes
{
    kVstMidiType  = 1,      // VstMidiEvent
    kVstAudioType = 2,      // deprecated
    kVstVideoType = 3,      // deprecated
    kVstParameterType = 4,  // deprecated
    kVstTriggerType = 5,    // deprecated
    kVstSysExType = 6,      // VstMidiSysexEvent (Steinberg's spelling)
    kVstSysexType = kVstSysExType
};

enum VstEventFlags
{
    kVstMidiEventIsRealtime = 1 << 0
};

typedef struct VstEvent
{
    VstInt32 type;
    VstInt32 byteSize;      // size of the WHOLE event, this header included
    VstInt32 deltaFrames;   // sample offset from the start of the block
    VstInt32 flags;
    char     data[16];
} VstEvent;

typedef struct VstMidiEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 noteLength;    // in samples, 0 when not a note
    VstInt32 noteOffset;    // samples until note-off, 0 when not a note
    char     midiData[4];   // up to three bytes plus a 0
    char     detune;
    char     noteOffVelocity;
    char     reserved1;
    char     reserved2;
} VstMidiEvent;

typedef struct VstMidiSysexEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 dumpBytes;     // number of bytes in sysexDump
    VstIntPtr resvd1;       // must be 0
    char*    sysexDump;
    VstIntPtr resvd2;       // must be 0
} VstMidiSysexEvent;

// ---- VstEvents ------------------------------------------------------------
// `events` is an ARRAY in the ABI, not a bare pointer, and the array is sized by
// the host: the struct is allocated as sizeof(VstEvents) + (n - 2) * sizeof(ptr)
// so that events[0..n-1] are all valid.
//
// The clean-room reference declares a single `struct vst_event_t** events;`
// here, which cannot work with that allocation: JUCE builds its list by taking
// offsetof(VstEvents, events) and indexing past it, so a second element would
// fall outside the struct.  The array is kept for that reason.
typedef struct VstEvents
{
    VstInt32  numEvents;
    VstIntPtr reserved;     // must be 0
    VstEvent* events[2];
} VstEvents;

// ---- MIDI key name (effGetMidiKeyName) ------------------------------------
typedef struct MidiKeyName
{
    VstInt32 thisProgramIndex;
    VstInt32 thisKeyNumber;
    char     keyName[kVstMaxNameLen];
    VstInt32 reserved;
    VstInt32 flags;
} MidiKeyName;

// ---- editor rectangle (effEditGetRect) ------------------------------------
typedef struct ERect
{
    short top;
    short left;
    short bottom;
    short right;
} ERect;

// ===========================================================================
//  Pin properties (effGetInputProperties / effGetOutputProperties)
// ===========================================================================

enum VstPinPropertiesFlags
{
    kVstPinIsActive  = 1 << 0,
    kVstPinIsStereo  = 1 << 1,
    kVstPinUseSpeaker = 1 << 2
};

typedef struct VstPinProperties
{
    char     label[kVstMaxLabelLen];
    VstInt32 flags;
    VstInt32 arrangementType;
    char     shortLabel[kVstMaxShortLabelLen];
    char     future[47];
} VstPinProperties;

// ===========================================================================
//  Speaker arrangements (effGet/SetSpeakerArrangement)
// ===========================================================================

// The VST 2.4 numbering.  It is NOT a simple 1..N run: the surround layouts are
// split into a Cine and a Music variant, so anything that assumes kSpeakerArr51
// follows kSpeakerArr41 will silently mis-address the array.
enum VstSpeakerArrangementType
{
    kSpeakerArrUserDefined = -2,
    kSpeakerArrEmpty       = -1,
    kSpeakerArrMono        =  0,
    kSpeakerArrStereo      =  1,
    kSpeakerArrStereoSurround = 2,
    kSpeakerArrStereoCenter   = 3,
    kSpeakerArrStereoSide     = 4,
    kSpeakerArrStereoCLfe     = 5,
    kSpeakerArr30Cine      =  6,
    kSpeakerArr30Music     =  7,
    kSpeakerArr31Cine      =  8,
    kSpeakerArr31Music     =  9,
    kSpeakerArr40Cine      = 10,
    kSpeakerArr40Music     = 11,
    kSpeakerArr41Cine      = 12,
    kSpeakerArr41Music     = 13,
    kSpeakerArr50          = 14,
    kSpeakerArr51          = 15,
    kSpeakerArr60Cine      = 16,
    kSpeakerArr60Music     = 17,
    kSpeakerArr61Cine      = 18,
    kSpeakerArr61Music     = 19,
    kSpeakerArr70Cine      = 20,
    kSpeakerArr70Music     = 21,
    kSpeakerArr71Cine      = 22,
    kSpeakerArr71Music     = 23,
    kSpeakerArr80Cine      = 24,
    kSpeakerArr80Music     = 25,
    kSpeakerArr81Cine      = 26,
    kSpeakerArr81Music     = 27,
    kSpeakerArr102         = 28,
    kNumSpeakerArr         = 29
};

enum VstSpeakerType
{
    kSpeakerUndefined = 0x7fffffff,
    kSpeakerL = 1,
    kSpeakerR = 2,
    kSpeakerC = 3,
    kSpeakerLfe = 4,
    kSpeakerLs = 5,
    kSpeakerRs = 6,
    kSpeakerLc = 7,
    kSpeakerRc = 8,
    kSpeakerS = 9,
    kSpeakerCs = kSpeakerS,
    kSpeakerSl = 10,
    kSpeakerSr = 11,
    kSpeakerTm = 12,
    kSpeakerTfl = 13,
    kSpeakerTfc = 14,
    kSpeakerTfr = 15,
    kSpeakerTrl = 16,
    kSpeakerTrc = 17,
    kSpeakerTrr = 18,
    kSpeakerLfe2 = 19
};

#define kMaxSpeakers 8

typedef struct VstSpeakerProperties
{
    float    azimuth;
    float    elevation;
    float    radius;
    float    reserved;
    char     name[kVstMaxNameLen];
    VstInt32 type;
    char     future[28];
} VstSpeakerProperties;

typedef struct VstSpeakerArrangement
{
    VstInt32              type;
    VstInt32              numChannels;
    VstSpeakerProperties  speakers[kMaxSpeakers];
} VstSpeakerArrangement;

// ===========================================================================
//  Audio file selectors (audioMasterOpenFileSelector)
// ===========================================================================

enum VstFileSelectCommand
{
    kVstFileLoad = 0,
    kVstFileSave,
    kVstMultipleFilesLoad,
    kVstDirectorySelect
};

enum VstFileSelectType
{
    kVstFileType = 0
};

typedef struct VstFileType
{
    char name[128];
    char macType[8];
    char dosType[8];
    char unixType[8];
    char mimeType1[128];
    char mimeType2[128];
} VstFileType;

// ===========================================================================
//  Host language (audioMasterGetLanguage)
// ===========================================================================

enum VstHostLanguage
{
    kVstLangEnglish = 1,
    kVstLangGerman,
    kVstLangFrench,
    kVstLangItalian,
    kVstLangSpanish,
    kVstLangJapanese
};

#endif // OVERTEON_VST2_AEFFECTX_H
