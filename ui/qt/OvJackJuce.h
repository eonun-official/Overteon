#pragma once
// ============================================================
//  OvJackJuce — the preview hosting the REAL DSP.
//
//  The preview used to synthesise its "after" picture: it FFT'd the raw input
//  and then ADDED an analytic EQ response computed from the parameters.  That
//  is not what an after-processing meter shows, and it was visibly wrong:
//
//    * with no audio the input table sits on its -85 dB floor, and adding the
//      EQ curve on top of that floor drew the EQ shape as a static picture —
//      the "in silence it reacts with the static EQ curve" report;
//    * the curve was an APPROXIMATION (the sum of the five bands' |H|), so it
//      could not show anything the DSP actually does — oversampling, the
//      saturator's harmonics, the dry/wet mix, the limiter's action.
//
//  So the preview now contains the plug-in's own InjectorProcessor and feeds
//  it from its JACK inputs, exactly as a host would: JACK thread -> copy into a
//  JUCE buffer -> processBlock().  Both spectrum taps then come from inside the
//  DSP and are FFTs of samples that really exist: the input tap of what entered,
//  the output tap of what left.  Silence is silence on both, and every level is
//  measured rather than modelled.
//
//  The parameter table, preset loading and the meter values come from
//  OvJuceSource, i.e. the plug-in's own APVTS — so ovctl drives the same
//  parameters the DSP reads, and a .hinj file sets what the plug-in would set.
// ============================================================
#include "OvJuceSource.h"
#include "FxProcessor.h"   // InjectorProcessor, the real DSP

#include <jack/jack.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

class OvJackJuce : public OvJuceSource
{
public:
    OvJackJuce (InjectorProcessor& processor, const std::string& clientName = "onyxpreview")
        : OvJuceSource (processor), proc (processor)
    {
        jack_status_t st {};
        m_client = jack_client_open (clientName.c_str(), JackNoStartServer, &st);
        if (m_client == nullptr)
        {
            std::fprintf (stderr, "[jack] cannot open a client (%s) — is jackd "
                                  "running and JACK_DEFAULT_SERVER right?\n",
                          clientName.c_str());
            return;
        }

        m_in[0] = jack_port_register (m_client, "in_1", JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0);
        m_in[1] = jack_port_register (m_client, "in_2", JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0);

        m_rate = jack_get_sample_rate (m_client);
        m_block = (int) jack_get_buffer_size (m_client);

        // The processor must know the rate and the block size before a single
        // sample arrives, or its oversampling and biquads are built for the
        // wrong rate and every measurement is of the wrong filter.
        proc.setPlayConfigDetails (2, 2, m_rate, m_block);
        proc.prepareToPlay (m_rate, m_block);

        // Pre-allocated, so the audio thread never allocates.  The JACK input
        // buffers are copied into it rather than processed in place: they
        // belong to JACK.
        m_buf.setSize (2, m_block, false, false, true);
        m_midi.ensureSize (64);

        jack_set_process_callback (m_client, &OvJackJuce::onProcess, this);

        if (jack_activate (m_client) != 0)
        {
            std::fprintf (stderr, "[jack] activate failed\n");
            jack_client_close (m_client);
            m_client = nullptr;
            return;
        }

        m_ok = true;
        std::fprintf (stderr, "[preview] real DSP: InjectorProcessor, %.0f Hz, "
                              "%d frames, JACK client '%s'\n",
                      m_rate, m_block, clientName.c_str());
    }

    ~OvJackJuce() override
    {
        if (m_client != nullptr)
        {
            jack_deactivate (m_client);
            jack_client_close (m_client);
        }
        proc.releaseResources();
    }

    bool ok() const { return m_ok; }

    double sampleRate() const override { return m_rate; }

private:
    static int onProcess (jack_nframes_t nframes, void* arg)
    {
        return static_cast<OvJackJuce*> (arg)->process (nframes);
    }

    int process (jack_nframes_t nframes)
    {
        auto* l = (jack_default_audio_sample_t*) jack_port_get_buffer (m_in[0], nframes);
        auto* r = (jack_default_audio_sample_t*) jack_port_get_buffer (m_in[1], nframes);

        const int n = (int) nframes;
        if (m_buf.getNumSamples() < n)
            return 0;                       // never resize on the audio thread

        if (l != nullptr) std::memcpy (m_buf.getWritePointer (0), l, sizeof (float) * (size_t) n);
        else              std::memset (m_buf.getWritePointer (0), 0, sizeof (float) * (size_t) n);

        if (r != nullptr) std::memcpy (m_buf.getWritePointer (1), r, sizeof (float) * (size_t) n);
        else              std::memcpy (m_buf.getWritePointer (1), m_buf.getReadPointer (0),
                                       sizeof (float) * (size_t) n);

        m_midi.clear();
        proc.processBlock (m_buf, m_midi);
        return 0;
    }

    InjectorProcessor& proc;
    jack_client_t*     m_client = nullptr;
    jack_port_t*       m_in[2] {};
    double             m_rate  = 48000.0;
    int                m_block = 1024;
    bool               m_ok    = false;

    juce::AudioBuffer<float> m_buf;
    juce::MidiBuffer         m_midi;
};
