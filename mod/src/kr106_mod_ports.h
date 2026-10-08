// kr106_mod_ports.h -- Port table for the MOD (mod-ui / mod-host) LV2 build.
//
// Single source of truth shared by the plugin (kr106_mod.cpp) and the TTL
// generator (tools/kr106_mod_ttlgen.cpp). Every parameter is a plain
// lv2:ControlPort so mod-ui can display it, MIDI-learn it, address it to
// hardware actuators and store it in pedalboard snapshots.
//
// Port index layout:
//   0          MIDI / transport input (atom)
//   1, 2       audio out L / R
//   3 ..       control inputs, in kControlPorts order
//
// Changing a symbol breaks saved pedalboards: only ever append ports.

#pragma once

#include "KR106Arpeggiator.h"
#include "KR106LFO.h"

namespace kr106mod
{

// Parameter indices -- must match KR106DSP::SetParam's internal enum
// (same list as EParams in Source/PluginProcessor.h).
enum EParams
{
  kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay,
  kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
  kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd,
  kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
  kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw,
  kChorusOff, kChorusI, kChorusII,
  kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode,
  kVcfEnvInv, kVcaMode,
  kBender, kTuning, kPower,
  kPortaMode, kPortaRate,
  kTransposeOffset, kBenderLfo,
  kAdsrMode,
  kMasterVol,
  kSettingVoices, kSettingOversample, kSettingIgnoreVel,
  kSettingArpLimitKbd, kSettingArpSync, kSettingLfoSync,
  kSettingMonoRetrig, kSettingMidiSysEx,
  kArpQuantize, kLfoQuantize,
  kSettingOscMode,
  kNumParams
};

// Controls that don't map 1:1 onto a DSP parameter
enum ESpecial
{
  kSpecialChorus = -1,     // Off / I / II / I+II -> kChorusI + kChorusII
  kSpecialLfoTrigger = -2, // momentary LFO trigger (same as mod wheel / CC1)
};

enum class PortKind { Float, Toggle, Enum, Int };

struct ControlPort
{
  const char* symbol;
  const char* name;
  const char* shortName;  // <= 16 chars (MOD actuator display)
  const char* group;      // pg:Group symbol
  int param;              // EParams index or ESpecial
  PortKind kind;
  float def, min, max;
  const char* const* labels = nullptr; // Enum only: one label per step
  const float* values = nullptr;       // Enum only: explicit values (else min + i)
  int numLabels = 0;
  bool momentary = false;              // Toggle only: MOD footswitch default
};

struct PortGroup { const char* symbol; const char* name; };

// Order here is the on-panel order, left to right
static constexpr PortGroup kGroups[] = {
  { "dco",    "DCO" },
  { "hpf",    "HPF" },
  { "vcf",    "VCF" },
  { "vca",    "VCA" },
  { "env",    "ENV" },
  { "chorus", "Chorus" },
  { "arp",    "Arpeggio" },
  { "lfo",    "LFO" },
  { "perf",   "Performance" },
  { "setup",  "Setup" },
};

static constexpr const char* kOctaveLabels[]   = { "16'", "8'", "4'" };
static constexpr const char* kPwmModeLabels[]  = { "LFO", "Manual", "Env" };
static constexpr const char* kHpfLabels[]      = { "0 (106 Boost / 60 Flat)", "1 (106 Flat / 60 122 Hz)",
                                                   "2 (106 236 Hz / 60 269 Hz)", "3 (106 754 Hz / 60 571 Hz)" };
static constexpr const char* kVcaModeLabels[]  = { "Env", "Gate" };
static constexpr const char* kChorusLabels[]   = { "Off", "I", "II", "I+II" };
static constexpr const char* kArpModeLabels[]  = { "Up", "Up/Down", "Down" };
static constexpr const char* kArpRangeLabels[] = { "1 Oct", "2 Oct", "3 Oct" };
static constexpr const char* kLfoModeLabels[]  = { "Auto", "Manual" };
static constexpr const char* kPortaLabels[]    = { "Mono", "Poly I", "Poly II" };
static constexpr const char* kModelLabels[]    = { "Juno-60", "Juno-106" };
static constexpr const char* kVoicesLabels[]   = { "6", "8", "10" };
static constexpr float       kVoicesValues[]   = { 6.f, 8.f, 10.f };
static constexpr const char* kOsLabels[]       = { "Off", "2x", "4x" };
static constexpr float       kOsValues[]       = { 1.f, 2.f, 4.f };
static constexpr const char* kOscModeLabels[]  = { "Wavetable", "PolyBLEP" };

#define KR_ENUM(arr) arr, nullptr, static_cast<int>(sizeof(arr) / sizeof(arr[0]))
#define KR_ENUMV(arr, vals) arr, vals, static_cast<int>(sizeof(arr) / sizeof(arr[0]))

static const ControlPort kControlPorts[] = {
  // --- DCO ---
  { "dco_range",  "DCO Range",      "Range",      "dco", kOctTranspose, PortKind::Enum,   1.f, 0.f, 2.f, KR_ENUM(kOctaveLabels) },
  { "dco_lfo",    "DCO LFO",        "DCO LFO",    "dco", kDcoLfo,       PortKind::Float,  0.f, 0.f, 1.f },
  { "dco_pwm",    "DCO PWM",        "PWM",        "dco", kDcoPwm,       PortKind::Float,  0.5f, 0.f, 1.f },
  { "pwm_mode",   "PWM Mode",       "PWM Mode",   "dco", kPwmMode,      PortKind::Enum,   1.f, 0.f, 2.f, KR_ENUM(kPwmModeLabels) },
  { "dco_pulse",  "Pulse",          "Pulse",      "dco", kDcoPulse,     PortKind::Toggle, 1.f, 0.f, 1.f },
  { "dco_saw",    "Saw",            "Saw",        "dco", kDcoSaw,       PortKind::Toggle, 1.f, 0.f, 1.f },
  { "dco_sub_on", "Sub On",         "Sub On",     "dco", kDcoSubSw,     PortKind::Toggle, 1.f, 0.f, 1.f },
  { "dco_sub",    "Sub Level",      "Sub",        "dco", kDcoSub,       PortKind::Float,  1.f, 0.f, 1.f },
  { "dco_noise",  "Noise",          "Noise",      "dco", kDcoNoise,     PortKind::Float,  0.f, 0.f, 1.f },
  // --- HPF ---
  { "hpf",        "HPF",            "HPF",        "hpf", kHpfFreq,      PortKind::Enum,   1.f, 0.f, 3.f, KR_ENUM(kHpfLabels) },
  // --- VCF ---
  { "vcf_freq",   "VCF Freq",       "Cutoff",     "vcf", kVcfFreq,      PortKind::Float,  1.f, 0.f, 1.f },
  { "vcf_res",    "VCF Res",        "Resonance",  "vcf", kVcfRes,       PortKind::Float,  0.f, 0.f, 1.f },
  { "vcf_env_inv","VCF Env Invert", "Env Invert", "vcf", kVcfEnvInv,    PortKind::Toggle, 0.f, 0.f, 1.f },
  { "vcf_env",    "VCF Env",        "VCF Env",    "vcf", kVcfEnv,       PortKind::Float,  0.f, 0.f, 1.f },
  { "vcf_lfo",    "VCF LFO",        "VCF LFO",    "vcf", kVcfLfo,       PortKind::Float,  0.f, 0.f, 1.f },
  { "vcf_kbd",    "VCF Kbd",        "VCF Kbd",    "vcf", kVcfKbd,       PortKind::Float,  0.f, 0.f, 1.f },
  // --- VCA ---
  { "vca_mode",   "VCA Mode",       "VCA Mode",   "vca", kVcaMode,      PortKind::Enum,   1.f, 0.f, 1.f, KR_ENUM(kVcaModeLabels) },
  { "vca_level",  "VCA Level",      "VCA Level",  "vca", kVcaLevel,     PortKind::Float,  0.5f, 0.f, 1.f },
  // --- ENV ---
  { "env_a",      "Attack",         "Attack",     "env", kEnvA,         PortKind::Float,  0.25f, 0.f, 1.f },
  { "env_d",      "Decay",          "Decay",      "env", kEnvD,         PortKind::Float,  0.25f, 0.f, 1.f },
  { "env_s",      "Sustain",        "Sustain",    "env", kEnvS,         PortKind::Float,  0.9f, 0.f, 1.f },
  { "env_r",      "Release",        "Release",    "env", kEnvR,         PortKind::Float,  0.25f, 0.f, 1.f },
  // --- Chorus ---
  { "chorus",     "Chorus",         "Chorus",     "chorus", kSpecialChorus, PortKind::Enum, 1.f, 0.f, 3.f, KR_ENUM(kChorusLabels) },
  // --- Arpeggio ---
  { "arp_on",     "Arpeggio",       "Arp",        "arp", kArpeggio,     PortKind::Toggle, 0.f, 0.f, 1.f },
  { "arp_mode",   "Arp Mode",       "Arp Mode",   "arp", kArpMode,      PortKind::Enum,   0.f, 0.f, 2.f, KR_ENUM(kArpModeLabels) },
  { "arp_range",  "Arp Range",      "Arp Range",  "arp", kArpRange,     PortKind::Enum,   0.f, 0.f, 2.f, KR_ENUM(kArpRangeLabels) },
  { "arp_rate",   "Arp Rate",       "Arp Rate",   "arp", kArpRate,      PortKind::Float,  30.f / 128.f, 0.f, 1.f },
  { "arp_sync",   "Arp Sync",       "Arp Sync",   "arp", kSettingArpSync, PortKind::Toggle, 0.f, 0.f, 1.f },
  { "arp_div",    "Arp Division",   "Arp Div",    "arp", kArpQuantize,  PortKind::Enum,   static_cast<float>(kr106::kDiv16), 0.f,
                  static_cast<float>(kr106::kNumArpDivisions - 1), KR_ENUM(kr106::kDivNames) },
  { "arp_limit",  "Arp Limit Kbd",  "Arp Limit",  "arp", kSettingArpLimitKbd, PortKind::Toggle, 1.f, 0.f, 1.f },
  // --- LFO ---
  { "lfo_rate",   "LFO Rate",       "LFO Rate",   "lfo", kLfoRate,      PortKind::Float,  0.24f, 0.f, 1.f },
  { "lfo_delay",  "LFO Delay",      "LFO Delay",  "lfo", kLfoDelay,     PortKind::Float,  0.f, 0.f, 1.f },
  { "lfo_mode",   "LFO Mode",       "LFO Mode",   "lfo", kLfoMode,      PortKind::Enum,   0.f, 0.f, 1.f, KR_ENUM(kLfoModeLabels) },
  { "lfo_trig",   "LFO Trigger",    "LFO Trig",   "lfo", kSpecialLfoTrigger, PortKind::Toggle, 0.f, 0.f, 1.f, nullptr, nullptr, 0, true },
  { "lfo_sync",   "LFO Sync",       "LFO Sync",   "lfo", kSettingLfoSync, PortKind::Toggle, 0.f, 0.f, 1.f },
  { "lfo_div",    "LFO Division",   "LFO Div",    "lfo", kLfoQuantize,  PortKind::Enum,   static_cast<float>(kr106::kLfoDiv4), 0.f,
                  static_cast<float>(kr106::kNumLfoDivisions - 1), KR_ENUM(kr106::kLfoDivNames) },
  // --- Performance ---
  { "hold",       "Hold",           "Hold",       "perf", kHold,        PortKind::Toggle, 0.f, 0.f, 1.f },
  { "porta_mode", "Assign Mode",    "Assign",     "perf", kPortaMode,   PortKind::Enum,   1.f, 0.f, 2.f, KR_ENUM(kPortaLabels) },
  { "porta_rate", "Portamento",     "Portamento", "perf", kPortaRate,   PortKind::Float,  0.f, 0.f, 1.f },
  { "bender",     "Bender",         "Bender",     "perf", kBender,      PortKind::Float,  0.f, -1.f, 1.f },
  { "bend_dco",   "Bender DCO",     "Bend DCO",   "perf", kBenderDco,   PortKind::Float,  0.f, 0.f, 1.f },
  { "bend_vcf",   "Bender VCF",     "Bend VCF",   "perf", kBenderVcf,   PortKind::Float,  0.f, 0.f, 1.f },
  { "bend_lfo",   "Bender LFO",     "Bend LFO",   "perf", kBenderLfo,   PortKind::Float,  0.f, 0.f, 1.f },
  // --- Setup ---
  { "volume",     "Master Volume",  "Volume",     "setup", kMasterVol,  PortKind::Float,  0.5f, 0.f, 1.f },
  { "tuning",     "Tuning",         "Tuning",     "setup", kTuning,     PortKind::Float,  0.f, -1.f, 1.f },
  { "transpose",  "Transpose",      "Transpose",  "setup", kTransposeOffset, PortKind::Int, 0.f, -24.f, 36.f },
  { "model",      "Model",          "Model",      "setup", kAdsrMode,   PortKind::Enum,   1.f, 0.f, 1.f, KR_ENUM(kModelLabels) },
  { "voices",     "Voices",         "Voices",     "setup", kSettingVoices, PortKind::Enum, 6.f, 6.f, 10.f, KR_ENUMV(kVoicesLabels, kVoicesValues) },
  { "osc_mode",   "Oscillator Mode","Osc Mode",   "setup", kSettingOscMode, PortKind::Enum, 1.f, 0.f, 1.f, KR_ENUM(kOscModeLabels) },
  { "oversample", "VCF Oversample", "Oversample", "setup", kSettingOversample, PortKind::Enum, 2.f, 1.f, 4.f, KR_ENUMV(kOsLabels, kOsValues) },
  { "ignore_vel", "Ignore Velocity","Ignore Vel", "setup", kSettingIgnoreVel, PortKind::Toggle, 1.f, 0.f, 1.f },
  { "mono_retrig","Mono Retrigger", "Retrigger",  "setup", kSettingMonoRetrig, PortKind::Toggle, 1.f, 0.f, 1.f },
};

#undef KR_ENUM
#undef KR_ENUMV

static constexpr int kNumControlPorts = static_cast<int>(sizeof(kControlPorts) / sizeof(kControlPorts[0]));

enum EFixedPorts { kPortEventsIn = 0, kPortAudioOutL, kPortAudioOutR, kFirstControlPort };

static constexpr const char* kPluginURI = "https://kayrock.org/kr106/mod";

// LV2 version of this bundle. Bump the micro version on every release that
// changes the TTL; MOD treats odd numbers as "testing" builds.
static constexpr int kModMinorVersion = 2;
static constexpr int kModMicroVersion = 0;

} // namespace kr106mod
