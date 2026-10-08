// kr106_mod.cpp -- Headless LV2 build of the KR-106 for MOD (mod-host / mod-ui).
//
// Wraps the header-only DSP engine (Source/DSP) directly, without JUCE:
//   - every parameter is an lv2:ControlPort (see kr106_mod_ports.h), so
//     mod-ui can show it in a modgui, MIDI-learn it, address it to
//     actuators and store it in snapshots;
//   - no X11 / OpenGL / freetype dependency, nothing but libstdc++;
//   - MIDI and host transport (time:Position) arrive on one atom port.
//
// MIDI handling is limited to performance messages: notes, pitch bend,
// mod wheel (LFO trigger), sustain (Hold) and all-notes-off. Parameter CCs
// are left to mod-ui's own MIDI learn so the GUI always shows the truth.

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#if defined(__SSE__) || defined(_M_X64)
  #include <xmmintrin.h>
#endif

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>

#include "KR106_DSP.h"
#include "KR106_DSP_SetParam.h"
#include "kr106_mod_ports.h"

using namespace kr106mod;

namespace
{

// Flush denormals to zero for the duration of run(); restores the caller's mode.
class ScopedFlushDenormals
{
public:
  ScopedFlushDenormals()
  {
#if defined(__aarch64__)
    asm volatile("mrs %0, fpcr" : "=r"(mSaved));
    uint64_t fpcr = mSaved | (1ull << 24); // FZ
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__) || defined(_M_X64)
    mSaved = _mm_getcsr();
    _mm_setcsr(static_cast<unsigned>(mSaved) | 0x8040); // FTZ | DAZ
#endif
  }
  ~ScopedFlushDenormals()
  {
#if defined(__aarch64__)
    asm volatile("msr fpcr, %0" : : "r"(mSaved));
#elif defined(__SSE__) || defined(_M_X64)
    _mm_setcsr(static_cast<unsigned>(mSaved));
#endif
  }
private:
  uint64_t mSaved = 0;
};

struct URIs
{
  LV2_URID atomBlank, atomObject, atomFloat, atomDouble, atomInt, atomLong;
  LV2_URID midiEvent;
  LV2_URID timePosition, timeBeat, timeBarBeat, timeBar, timeBeatsPerBar,
           timeBeatsPerMinute, timeSpeed;

  void map(LV2_URID_Map* m)
  {
    atomBlank          = m->map(m->handle, LV2_ATOM__Blank);
    atomObject         = m->map(m->handle, LV2_ATOM__Object);
    atomFloat          = m->map(m->handle, LV2_ATOM__Float);
    atomDouble         = m->map(m->handle, LV2_ATOM__Double);
    atomInt            = m->map(m->handle, LV2_ATOM__Int);
    atomLong           = m->map(m->handle, LV2_ATOM__Long);
    midiEvent          = m->map(m->handle, LV2_MIDI__MidiEvent);
    timePosition       = m->map(m->handle, LV2_TIME__Position);
    timeBeat           = m->map(m->handle, LV2_TIME__beat);
    timeBarBeat        = m->map(m->handle, LV2_TIME__barBeat);
    timeBar            = m->map(m->handle, LV2_TIME__bar);
    timeBeatsPerBar    = m->map(m->handle, LV2_TIME__beatsPerBar);
    timeBeatsPerMinute = m->map(m->handle, LV2_TIME__beatsPerMinute);
    timeSpeed          = m->map(m->handle, LV2_TIME__speed);
  }
};

class KR106Mod
{
public:
  // DSP is rendered in sub-blocks of at most this many frames, split at
  // MIDI event times, so the host block size never reaches the engine.
  static constexpr int kMaxChunk = 256;
  static constexpr int kFadeInTotal = 64;

  KR106Mod(double sampleRate, LV2_URID_Map* map)
    : mSampleRate(sampleRate)
  {
    mURIs.map(map);
    mDSP.mUnisonStack.reserve(128);
    // Size the oversampled mix bus for the worst case (4x) once, here,
    // so changing the oversample port later never allocates in run().
    mDSP.SetOversample(4);
    mDSP.Reset(mSampleRate, kMaxChunk);
    for (auto& p : mPorts) p = nullptr;
  }

  void connect(uint32_t port, void* data)
  {
    if (port == kPortEventsIn) mEventsIn = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == kPortAudioOutL) mOutL = static_cast<float*>(data);
    else if (port == kPortAudioOutR) mOutR = static_cast<float*>(data);
    else if (port >= kFirstControlPort && port < kFirstControlPort + kNumControlPorts)
      mPorts[port - kFirstControlPort] = static_cast<const float*>(data);
  }

  void activate()
  {
    mDSP.SetOversample(4);
    mDSP.Reset(mSampleRate, kMaxChunk);
    mDSP.SetParam(kPower, 1.0);
    mNeedFullPush = true;
    mFadeInRemaining = kFadeInTotal;
    mSustainPedal = false;
    mModWheel = false;
    mMidiBend = 0.f;
    mPlaying = false;
    mHaveBPM = mHavePPQ = false;
  }

  void run(uint32_t nFrames)
  {
    ScopedFlushDenormals noDenormals;

    syncControlPorts();

    uint32_t pos = 0;
    if (mEventsIn)
    {
      LV2_ATOM_SEQUENCE_FOREACH(mEventsIn, ev)
      {
        uint32_t evFrame = static_cast<uint32_t>(ev->time.frames);
        if (evFrame > nFrames) evFrame = nFrames;
        if (evFrame > pos)
        {
          render(pos, evFrame - pos);
          pos = evFrame;
        }
        handleEvent(&ev->body);
      }
    }
    if (pos < nFrames)
      render(pos, nFrames - pos);
  }

private:
  // ---------------------------------------------------------------- params

  float portValue(int i) const
  {
    const ControlPort& cp = kControlPorts[i];
    float v = mPorts[i] ? *mPorts[i] : cp.def;
    if (!std::isfinite(v)) v = cp.def;
    return std::min(cp.max, std::max(cp.min, v));
  }

  void syncControlPorts()
  {
    // Model first: it re-dispatches every model-dependent slider curve,
    // so a preset that changes model and sliders together lands correctly.
    for (int i = 0; i < kNumControlPorts; i++)
      if (kControlPorts[i].param == kAdsrMode)
      {
        float v = portValue(i);
        if (mNeedFullPush || v != mLastPort[i])
          applyPort(i, v);
      }

    if (mNeedFullPush)
    {
      for (int i = 0; i < kNumControlPorts; i++)
        if (kControlPorts[i].param != kAdsrMode)
          applyPort(i, portValue(i));
      // Snap the level smoothers to their targets so the first block
      // doesn't ramp down from unity (audible as a loud first note).
      mDSP.mMasterVol = mMasterVol * mMasterVol;
      mDSP.mMasterVolSmooth = mDSP.mMasterVol;
      mDSP.mVcaLevelSmooth = mDSP.mVcaLevel;
      mNeedFullPush = false;
      return;
    }

    for (int i = 0; i < kNumControlPorts; i++)
    {
      float v = portValue(i);
      if (v != mLastPort[i])
        applyPort(i, v);
    }
  }

  void applyPort(int i, float v)
  {
    mLastPort[i] = v;
    const ControlPort& cp = kControlPorts[i];
    if (cp.kind != PortKind::Float)
      v = std::round(v);

    switch (cp.param)
    {
      case kSpecialChorus: {
        int mode = static_cast<int>(v);
        mDSP.SetParam(kChorusI,  (mode & 1) ? 1.0 : 0.0);
        mDSP.SetParam(kChorusII, (mode & 2) ? 1.0 : 0.0);
        break;
      }
      case kSpecialLfoTrigger:
        mLfoTrigPort = v > 0.5f;
        updateLfoTrigger();
        break;
      case kHold:
        mHoldPort = v > 0.5f;
        updateHold();
        break;
      case kBender:
        mBenderPort = v;
        updateBender();
        break;
      case kMasterVol:
        mMasterVol = v;
        break;
      case kTransposeOffset:
        mDSP.SetKeyTranspose(static_cast<int>(v));
        break;
      case kSettingVoices: {
        int n = static_cast<int>(v);
        mDSP.SetActiveVoices(n <= 7 ? 6 : n <= 9 ? 8 : 10);
        break;
      }
      case kSettingOversample:
        mDSP.SetOversample(static_cast<int>(v));
        break;
      case kSettingIgnoreVel:
        mDSP.mIgnoreVelocity = v > 0.5f;
        break;
      case kSettingArpLimitKbd:
        mDSP.mArp.mLimitToKeyboard = v > 0.5f;
        break;
      case kSettingArpSync:
        mArpSync = v > 0.5f;
        break;
      case kSettingLfoSync:
        mLfoSync = v > 0.5f;
        break;
      case kSettingMonoRetrig:
        mDSP.mMonoRetrigger = v > 0.5f;
        break;
      case kSettingOscMode: {
        int mode = static_cast<int>(v);
        mDSP.ForEachVoice([mode](kr106::Voice<float>& voice) { voice.mOscMode = mode; });
        break;
      }
      default:
        mDSP.SetParam(cp.param, static_cast<double>(v));
        break;
    }
  }

  void updateHold()
  {
    bool hold = mHoldPort || mSustainPedal;
    if (hold != mDSP.mHold)
      mDSP.SetParam(kHold, hold ? 1.0 : 0.0);
  }

  void updateBender()
  {
    float b = std::min(1.f, std::max(-1.f, mBenderPort + mMidiBend));
    mDSP.SetParam(kBender, static_cast<double>(b));
  }

  void updateLfoTrigger()
  {
    bool on = mLfoTrigPort || mModWheel;
    if (on != mLfoTrigOn)
    {
      mLfoTrigOn = on;
      mDSP.ControlChange(1, on ? 1.f : 0.f);
    }
  }

  // ---------------------------------------------------------------- events

  void handleEvent(const LV2_Atom* body)
  {
    if (body->type == mURIs.midiEvent)
      handleMidi(reinterpret_cast<const uint8_t*>(body + 1), body->size);
    else if (body->type == mURIs.atomObject || body->type == mURIs.atomBlank)
    {
      const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(body);
      if (obj->body.otype == mURIs.timePosition)
        handleTimePosition(obj);
    }
  }

  void handleMidi(const uint8_t* msg, uint32_t size)
  {
    if (size < 1) return;
    const uint8_t status = msg[0] & 0xF0;

    switch (status)
    {
      case 0x90:
        if (size < 3) return;
        if (msg[2] > 0) mDSP.NoteOn(msg[1], msg[2]);
        else            mDSP.NoteOff(msg[1]);
        break;
      case 0x80:
        if (size < 3) return;
        mDSP.NoteOff(msg[1]);
        break;
      case 0xB0:
        if (size < 3) return;
        if (msg[1] == 1)
        {
          mModWheel = msg[2] > 0;
          updateLfoTrigger();
        }
        else if (msg[1] == 64)
        {
          mSustainPedal = msg[2] >= 64;
          updateHold();
        }
        else if (msg[1] == 120 || msg[1] == 123)
          mDSP.AllNotesOff();
        break;
      case 0xE0:
        if (size < 3) return;
        mMidiBend = static_cast<float>(((msg[2] << 7) | msg[1]) - 8192) / 8192.f;
        updateBender();
        break;
      default:
        break;
    }
  }

  static bool atomToDouble(const URIs& u, const LV2_Atom* a, double& out)
  {
    if (!a) return false;
    if (a->type == u.atomFloat)  { out = reinterpret_cast<const LV2_Atom_Float*>(a)->body;  return true; }
    if (a->type == u.atomDouble) { out = reinterpret_cast<const LV2_Atom_Double*>(a)->body; return true; }
    if (a->type == u.atomInt)    { out = reinterpret_cast<const LV2_Atom_Int*>(a)->body;    return true; }
    if (a->type == u.atomLong)   { out = static_cast<double>(reinterpret_cast<const LV2_Atom_Long*>(a)->body); return true; }
    return false;
  }

  // Hosts (mod-host included) send time:Position on transport changes only,
  // so the beat position is extrapolated between updates in render().
  void handleTimePosition(const LV2_Atom_Object* obj)
  {
    const LV2_Atom *beat = nullptr, *barBeat = nullptr, *bar = nullptr,
                   *beatsPerBar = nullptr, *bpm = nullptr, *speed = nullptr;
    lv2_atom_object_get(obj,
                        mURIs.timeBeat, &beat,
                        mURIs.timeBarBeat, &barBeat,
                        mURIs.timeBar, &bar,
                        mURIs.timeBeatsPerBar, &beatsPerBar,
                        mURIs.timeBeatsPerMinute, &bpm,
                        mURIs.timeSpeed, &speed,
                        0);
    double d;
    if (atomToDouble(mURIs, bpm, d) && d > 0.0) { mBPM = d; mHaveBPM = true; }
    if (atomToDouble(mURIs, speed, d)) mPlaying = d != 0.0;

    if (atomToDouble(mURIs, beat, d)) { mPPQ = d; mHavePPQ = true; }
    else
    {
      double b, bb, bpb;
      if (atomToDouble(mURIs, bar, b) && atomToDouble(mURIs, barBeat, bb) &&
          atomToDouble(mURIs, beatsPerBar, bpb))
      {
        mPPQ = b * bpb + bb;
        mHavePPQ = true;
      }
    }
  }

  // ---------------------------------------------------------------- audio

  void render(uint32_t offset, uint32_t nFrames)
  {
    while (nFrames > 0)
    {
      const int n = static_cast<int>(std::min<uint32_t>(nFrames, kMaxChunk));
      renderChunk(mOutL + offset, mOutR + offset, n);
      offset += static_cast<uint32_t>(n);
      nFrames -= static_cast<uint32_t>(n);
    }
  }

  void renderChunk(float* outL, float* outR, int n)
  {
    const double bpm = mHaveBPM ? mBPM : 120.0;

    mDSP.mArp.mSyncToHost = mArpSync;
    mDSP.mLFO.mSyncToHost = mLfoSync;
    if (mArpSync)
    {
      // Without a beat position, beat-grid sync can't work: free-run at host tempo
      mDSP.mArp.mHostPlaying = mHavePPQ ? mPlaying : false;
      mDSP.mArp.mHostBPM = bpm;
      mDSP.mArp.mHostBeatPos = mPPQ;
    }
    if (mLfoSync)
    {
      mDSP.mLFO.mHostPlaying = mHavePPQ ? mPlaying : true;
      mDSP.mLFO.mHostBPM = bpm;
    }

    std::memset(outL, 0, sizeof(float) * static_cast<size_t>(n));
    std::memset(outR, 0, sizeof(float) * static_cast<size_t>(n));
    float* outputs[2] = { outL, outR };
    mDSP.mMasterVol = mMasterVol * mMasterVol; // audio taper (squared)
    mDSP.ProcessBlock(nullptr, outputs, 2, n);

    if (mPlaying)
      mPPQ += bpm / (60.0 * mSampleRate) * n;

    if (mFadeInRemaining > 0)
    {
      const int fadeLen = std::min(mFadeInRemaining, n);
      for (int i = 0; i < fadeLen; i++)
      {
        const float g = static_cast<float>(kFadeInTotal - mFadeInRemaining + i) / kFadeInTotal;
        outL[i] *= g;
        outR[i] *= g;
      }
      mFadeInRemaining -= fadeLen;
    }
  }

  // ---------------------------------------------------------------- state

  KR106DSP<float> mDSP { 6 };
  URIs mURIs {};
  double mSampleRate;

  const LV2_Atom_Sequence* mEventsIn = nullptr;
  float* mOutL = nullptr;
  float* mOutR = nullptr;
  const float* mPorts[kNumControlPorts];
  float mLastPort[kNumControlPorts] = {};
  bool mNeedFullPush = true;

  float mMasterVol = 0.5f;
  float mBenderPort = 0.f;
  float mMidiBend = 0.f;
  bool mHoldPort = false;
  bool mSustainPedal = false;
  bool mLfoTrigPort = false;
  bool mModWheel = false;
  bool mLfoTrigOn = false;
  bool mArpSync = false;
  bool mLfoSync = false;

  // Cached host transport
  bool mPlaying = false;
  bool mHaveBPM = false;
  bool mHavePPQ = false;
  double mBPM = 120.0;
  double mPPQ = 0.0;

  int mFadeInRemaining = kFadeInTotal;
};

// ------------------------------------------------------------------ LV2 glue

LV2_Handle instantiate(const LV2_Descriptor*, double sampleRate, const char*,
                       const LV2_Feature* const* features)
{
  LV2_URID_Map* map = nullptr;
  for (int i = 0; features && features[i]; i++)
    if (std::strcmp(features[i]->URI, LV2_URID__map) == 0)
      map = static_cast<LV2_URID_Map*>(features[i]->data);
  if (!map) return nullptr;

  return new (std::nothrow) KR106Mod(sampleRate, map);
}

void connectPort(LV2_Handle h, uint32_t port, void* data) { static_cast<KR106Mod*>(h)->connect(port, data); }
void activate(LV2_Handle h) { static_cast<KR106Mod*>(h)->activate(); }
void run(LV2_Handle h, uint32_t nFrames) { static_cast<KR106Mod*>(h)->run(nFrames); }
void deactivate(LV2_Handle) {}
void cleanup(LV2_Handle h) { delete static_cast<KR106Mod*>(h); }
const void* extensionData(const char*) { return nullptr; }

const LV2_Descriptor kDescriptor = {
  kPluginURI, instantiate, connectPort, activate, run, deactivate, cleanup, extensionData
};

} // namespace

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
  return index == 0 ? &kDescriptor : nullptr;
}
