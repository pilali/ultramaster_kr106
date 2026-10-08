// kr106_mod_ttlgen -- Writes the Turtle files of the MOD LV2 bundle.
//
//   kr106_mod_ttlgen <bundle-dir> <binary-name> [ports.json]
//
// Produces manifest.ttl, kr106.ttl (ports, from kr106_mod_ports.h) and
// presets.ttl (from the compiled-in factory presets), so the port list,
// the plugin and the presets can never drift apart. modgui.ttl and the
// modgui/ directory are static files copied next to them by the Makefile.
// The optional JSON dump of the port table feeds tools/gen_modgui.py.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "KR106_DSP.h"
#include "KR106_Presets_JUCE.h"
#include "kr106_mod_ports.h"

using namespace kr106mod;

static FILE* openOut(const std::string& dir, const char* name)
{
  std::string path = dir + "/" + name;
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) { std::perror(path.c_str()); std::exit(1); }
  return f;
}

static std::string escape(const char* s)
{
  std::string out;
  for (; *s; s++)
  {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s;
  }
  return out;
}

static std::string fmt(float v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
  std::string s = buf;
  if (s.find_first_of(".e") == std::string::npos) s += ".0";
  return s;
}

// --- Factory presets ---------------------------------------------------------

struct Preset
{
  std::string uri, label;
  std::vector<std::pair<const char*, float>> values; // port symbol -> value
};

static bool isPresetParam(int param)
{
  switch (param)
  {
    case kDcoLfo: case kDcoPwm: case kDcoSub: case kDcoNoise: case kHpfFreq:
    case kVcfFreq: case kVcfRes: case kVcfEnv: case kVcfLfo: case kVcfKbd:
    case kVcaLevel: case kEnvA: case kEnvD: case kEnvS: case kEnvR:
    case kDcoPulse: case kDcoSaw: case kDcoSubSw:
    case kOctTranspose: case kLfoRate: case kLfoDelay: case kLfoMode: case kPwmMode:
    case kVcfEnvInv: case kVcaMode: case kAdsrMode:
    case kSpecialChorus:
      return true;
    default:
      return false;
  }
}

static std::vector<Preset> buildPresets()
{
  std::vector<Preset> presets;
  for (int j = 0; j < kNumFactoryPresets; j++)
  {
    const auto& fp = kFactoryPresets[j];
    if (std::strstr(fp.name, "Init")) continue; // empty slots

    const bool j106Bank = j >= 128;
    const char* bank = j106Bank ? "J106" : "J60";

    Preset p;
    std::string code(fp.name, std::strcspn(fp.name, " "));
    std::string bankLower = j106Bank ? "j106" : "j60";
    p.uri = std::string(kPluginURI) + "#preset-" + bankLower + "-" + code;
    p.label = std::string(bank) + " " + fp.name;

    for (int i = 0; i < kNumControlPorts; i++)
    {
      const ControlPort& cp = kControlPorts[i];
      if (!isPresetParam(cp.param)) continue;

      float v;
      if (cp.param == kSpecialChorus)
        v = static_cast<float>((fp.values[kChorusI] ? 1 : 0) | (fp.values[kChorusII] ? 2 : 0));
      else
      {
        const int raw = fp.values[cp.param];
        if (cp.kind == PortKind::Float)
        {
          // Same conversions as KR106AudioProcessor (constructor + setCurrentProgram)
          v = raw / 127.f;
          if (cp.param == kDcoLfo && j106Bank)
            v = kr106::Voice<float>::dcoLfoDepth106_inverseTaper(v);
          if (cp.param == kDcoPwm && j106Bank)
            v = std::min(1.f, raw / 105.f);
        }
        else
          v = static_cast<float>(raw);
      }
      p.values.emplace_back(cp.symbol, v);
    }
    presets.push_back(std::move(p));
  }
  return presets;
}

// --- Writers -------------------------------------------------------------------

static const char* kPrefixes =
  "@prefix atom:  <http://lv2plug.in/ns/ext/atom#> .\n"
  "@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
  "@prefix foaf:  <http://xmlns.com/foaf/0.1/> .\n"
  "@prefix lv2:   <http://lv2plug.in/ns/lv2core#> .\n"
  "@prefix midi:  <http://lv2plug.in/ns/ext/midi#> .\n"
  "@prefix mod:   <http://moddevices.com/ns/mod#> .\n"
  "@prefix opts:  <http://lv2plug.in/ns/ext/options#> .\n"
  "@prefix pg:    <http://lv2plug.in/ns/ext/port-groups#> .\n"
  "@prefix pprop: <http://lv2plug.in/ns/ext/port-props#> .\n"
  "@prefix pset:  <http://lv2plug.in/ns/ext/presets#> .\n"
  "@prefix rdf:   <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
  "@prefix rdfs:  <http://www.w3.org/2000/01/rdf-schema#> .\n"
  "@prefix time:  <http://lv2plug.in/ns/ext/time#> .\n"
  "@prefix urid:  <http://lv2plug.in/ns/ext/urid#> .\n\n";

static void writeManifest(const std::string& dir, const char* binary, const std::vector<Preset>& presets)
{
  FILE* f = openOut(dir, "manifest.ttl");
  std::fputs(kPrefixes, f);
  std::fprintf(f,
    "<%s>\n"
    "    a lv2:Plugin , lv2:InstrumentPlugin ;\n"
    "    lv2:binary <%s> ;\n"
    "    rdfs:seeAlso <kr106.ttl> , <modgui.ttl> .\n\n",
    kPluginURI, binary);

  for (const auto& p : presets)
    std::fprintf(f,
      "<%s>\n"
      "    a pset:Preset ;\n"
      "    lv2:appliesTo <%s> ;\n"
      "    rdfs:label \"%s\" ;\n"
      "    rdfs:seeAlso <presets.ttl> .\n\n",
      p.uri.c_str(), kPluginURI, escape(p.label.c_str()).c_str());
  std::fclose(f);
}

static void writePlugin(const std::string& dir)
{
  FILE* f = openOut(dir, "kr106.ttl");
  std::fputs(kPrefixes, f);

  for (const auto& g : kGroups)
    std::fprintf(f,
      "<%s#%s>\n"
      "    a pg:Group ;\n"
      "    lv2:symbol \"%s\" ;\n"
      "    lv2:name \"%s\" .\n\n",
      kPluginURI, g.symbol, g.symbol, g.name);

  std::fprintf(f,
    "<%s>\n"
    "    a lv2:Plugin , lv2:InstrumentPlugin , doap:Project ;\n"
    "    doap:name \"Ultramaster KR-106\" ;\n"
    "    doap:license <https://www.gnu.org/licenses/gpl-3.0> ;\n"
    "    doap:maintainer [\n"
    "        foaf:name \"Kayrock\" ;\n"
    "        foaf:homepage <https://kayrock.org/kr106> ;\n"
    "    ] ;\n"
    "    rdfs:comment \"\"\"Juno-6/60/106 emulation: 6-10 voices, Juno-60 analog and Juno-106 firmware modes, "
    "ladder VCF with OTA saturation, BBD chorus, arpeggiator with host tempo sync. "
    "Headless build for MOD (mod-host / mod-ui) with a tabbed modgui. "
    "MIDI: notes, pitch bend, mod wheel (LFO trigger), sustain (Hold). "
    "Use mod-ui MIDI learn for the knobs.\"\"\" ;\n"
    "    mod:brand \"Kayrock\" ;\n"
    "    mod:label \"KR-106\" ;\n"
    "    lv2:minorVersion %d ;\n"
    "    lv2:microVersion %d ;\n"
    "    lv2:requiredFeature urid:map ;\n"
    "    lv2:optionalFeature lv2:hardRTCapable ;\n",
    kPluginURI, kModMinorVersion, kModMicroVersion);

  // Fixed ports
  std::fprintf(f,
    "    lv2:port [\n"
    "        a lv2:InputPort , atom:AtomPort ;\n"
    "        atom:bufferType atom:Sequence ;\n"
    "        atom:supports midi:MidiEvent , time:Position ;\n"
    "        lv2:designation lv2:control ;\n"
    "        lv2:index %d ;\n"
    "        lv2:symbol \"midi_in\" ;\n"
    "        lv2:name \"MIDI In\" ;\n"
    "    ] , [\n"
    "        a lv2:OutputPort , lv2:AudioPort ;\n"
    "        lv2:index %d ;\n"
    "        lv2:symbol \"out_l\" ;\n"
    "        lv2:name \"Out L\" ;\n"
    "    ] , [\n"
    "        a lv2:OutputPort , lv2:AudioPort ;\n"
    "        lv2:index %d ;\n"
    "        lv2:symbol \"out_r\" ;\n"
    "        lv2:name \"Out R\" ;\n"
    "    ]",
    kPortEventsIn, kPortAudioOutL, kPortAudioOutR);

  for (int i = 0; i < kNumControlPorts; i++)
  {
    const ControlPort& cp = kControlPorts[i];
    std::fprintf(f,
      " , [\n"
      "        a lv2:InputPort , lv2:ControlPort ;\n"
      "        lv2:index %d ;\n"
      "        lv2:symbol \"%s\" ;\n"
      "        lv2:name \"%s\" ;\n"
      "        lv2:shortName \"%s\" ;\n"
      "        pg:group <%s#%s> ;\n"
      "        lv2:default %s ;\n"
      "        lv2:minimum %s ;\n"
      "        lv2:maximum %s ;\n",
      kFirstControlPort + i, cp.symbol, cp.name, cp.shortName, kPluginURI, cp.group,
      fmt(cp.def).c_str(), fmt(cp.min).c_str(), fmt(cp.max).c_str());

    switch (cp.kind)
    {
      case PortKind::Float:
        break;
      case PortKind::Int:
        std::fputs("        lv2:portProperty lv2:integer ;\n", f);
        break;
      case PortKind::Toggle:
        std::fputs("        lv2:portProperty lv2:integer , lv2:toggled", f);
        if (cp.momentary) std::fputs(" , mod:preferMomentaryOnByDefault", f);
        std::fputs(" ;\n", f);
        break;
      case PortKind::Enum:
        std::fputs("        lv2:portProperty lv2:integer , lv2:enumeration ;\n", f);
        for (int s = 0; s < cp.numLabels; s++)
        {
          float value = cp.values ? cp.values[s] : cp.min + static_cast<float>(s);
          std::fprintf(f,
            "        lv2:scalePoint [ rdfs:label \"%s\" ; rdf:value %s ] ;\n",
            escape(cp.labels[s]).c_str(), fmt(value).c_str());
        }
        break;
    }
    std::fputs("    ]", f);
  }
  std::fputs(" .\n", f);
  std::fclose(f);
}

static void writePresets(const std::string& dir, const std::vector<Preset>& presets)
{
  FILE* f = openOut(dir, "presets.ttl");
  std::fputs(kPrefixes, f);
  for (const auto& p : presets)
  {
    std::fprintf(f,
      "<%s>\n"
      "    a pset:Preset ;\n"
      "    lv2:appliesTo <%s> ;\n"
      "    rdfs:label \"%s\"",
      p.uri.c_str(), kPluginURI, escape(p.label.c_str()).c_str());
    for (const auto& [symbol, value] : p.values)
      std::fprintf(f, " ;\n    lv2:port [ lv2:symbol \"%s\" ; pset:value %s ]", symbol, fmt(value).c_str());
    std::fputs(" .\n\n", f);
  }
  std::fclose(f);
}

static void writeJson(const char* path)
{
  FILE* f = std::fopen(path, "w");
  if (!f) { std::perror(path); std::exit(1); }
  std::fputs("[\n", f);
  for (int i = 0; i < kNumControlPorts; i++)
  {
    const ControlPort& cp = kControlPorts[i];
    static const char* kinds[] = { "float", "toggle", "enum", "int" };
    std::fprintf(f, "  {\"symbol\": \"%s\", \"name\": \"%s\", \"short\": \"%s\", \"group\": \"%s\", "
                    "\"kind\": \"%s\", \"def\": %s, \"min\": %s, \"max\": %s, \"momentary\": %s, \"points\": [",
                 cp.symbol, cp.name, cp.shortName, cp.group, kinds[static_cast<int>(cp.kind)],
                 fmt(cp.def).c_str(), fmt(cp.min).c_str(), fmt(cp.max).c_str(), cp.momentary ? "true" : "false");
    for (int s = 0; s < cp.numLabels; s++)
      std::fprintf(f, "%s{\"label\": \"%s\", \"value\": %s}", s ? ", " : "", escape(cp.labels[s]).c_str(),
                   fmt(cp.values ? cp.values[s] : cp.min + static_cast<float>(s)).c_str());
    std::fprintf(f, "]}%s\n", i + 1 < kNumControlPorts ? "," : "");
  }
  std::fputs("]\n", f);
  std::fclose(f);
}

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    std::fprintf(stderr, "usage: %s <bundle-dir> <binary-name>\n", argv[0]);
    return 1;
  }

  // Sanity checks on the port table
  for (int i = 0; i < kNumControlPorts; i++)
  {
    const ControlPort& cp = kControlPorts[i];
    if (std::strlen(cp.shortName) > 16)
    { std::fprintf(stderr, "shortName too long: %s\n", cp.shortName); return 1; }
    if (cp.kind == PortKind::Enum && cp.numLabels < 2)
    { std::fprintf(stderr, "enum without labels: %s\n", cp.symbol); return 1; }
    for (int k = 0; k < i; k++)
      if (std::strcmp(kControlPorts[k].symbol, cp.symbol) == 0)
      { std::fprintf(stderr, "duplicate symbol: %s\n", cp.symbol); return 1; }
  }

  const std::string dir = argv[1];
  const auto presets = buildPresets();
  writeManifest(dir, argv[2], presets);
  writePlugin(dir);
  writePresets(dir, presets);
  if (argc > 3) writeJson(argv[3]);
  std::printf("Wrote %d control ports, %d presets to %s\n",
              kNumControlPorts, static_cast<int>(presets.size()), dir.c_str());
  return 0;
}
