// kr106_mod_test -- Smoke test for the MOD LV2 bundle, using lilv as host.
//
// Loads the plugin from LV2_PATH, applies a factory preset through lilv,
// plays a chord through the atom MIDI port in small blocks (like mod-host),
// and checks the output is finite, audible, and silent again after release.
// Also reports the real-time CPU load of the render loop.

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::map<std::string, LV2_URID> sUris;
static std::vector<std::string> sUriList;

static LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri)
{
  auto it = sUris.find(uri);
  if (it != sUris.end()) return it->second;
  sUriList.push_back(uri);
  LV2_URID id = static_cast<LV2_URID>(sUriList.size());
  sUris[uri] = id;
  return id;
}

static const char* kURI = "https://kayrock.org/kr106/mod";

struct PortValues { std::vector<float> v; std::map<std::string, uint32_t> index; };

static void setPortValue(const char* symbol, void* userData, const void* value, uint32_t size, uint32_t type)
{
  auto* pv = static_cast<PortValues*>(userData);
  if (size != sizeof(float) || type != mapUri(nullptr, LV2_ATOM__Float)) return;
  auto it = pv->index.find(symbol);
  if (it != pv->index.end()) pv->v[it->second] = *static_cast<const float*>(value);
}

int main()
{
  const double sr = 48000.0;
  const uint32_t block = 128;
  int failures = 0;

  LilvWorld* world = lilv_world_new();
  lilv_world_load_all(world);
  LilvNode* uri = lilv_new_uri(world, kURI);
  const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uri);
  if (!plugin) { std::fprintf(stderr, "FAIL: plugin %s not found (LV2_PATH?)\n", kURI); return 1; }
  std::printf("Plugin: %s, %u ports\n", lilv_node_as_string(lilv_plugin_get_name(plugin)),
              lilv_plugin_get_num_ports(plugin));

  LV2_URID_Map map = { nullptr, mapUri };
  LV2_Feature mapFeature = { LV2_URID__map, &map };
  const LV2_Feature* features[] = { &mapFeature, nullptr };
  LilvInstance* inst = lilv_plugin_instantiate(plugin, sr, features);
  if (!inst) { std::fprintf(stderr, "FAIL: instantiate\n"); return 1; }

  // Control ports at their defaults
  const uint32_t nPorts = lilv_plugin_get_num_ports(plugin);
  PortValues pv;
  pv.v.assign(nPorts, 0.f);
  std::vector<float> mins(nPorts), maxs(nPorts), defs(nPorts);
  lilv_plugin_get_port_ranges_float(plugin, mins.data(), maxs.data(), defs.data());
  LilvNode* controlClass = lilv_new_uri(world, LV2_CORE__ControlPort);
  for (uint32_t i = 0; i < nPorts; i++)
  {
    const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
    if (!lilv_port_is_a(plugin, port, controlClass)) continue;
    pv.v[i] = std::isnan(defs[i]) ? 0.f : defs[i];
    pv.index[lilv_node_as_string(lilv_port_get_symbol(plugin, port))] = i;
    lilv_instance_connect_port(inst, i, &pv.v[i]);
  }

  // Apply a factory preset the way a host does
  const char* presetUri = "https://kayrock.org/kr106/mod#preset-j106-A11";
  LilvNode* presetNode = lilv_new_uri(world, presetUri);
  lilv_world_load_resource(world, presetNode);
  LilvState* state = lilv_state_new_from_world(world, &map, presetNode);
  if (!state) { std::fprintf(stderr, "FAIL: preset %s not loadable\n", presetUri); failures++; }
  else
  {
    lilv_state_restore(state, inst, setPortValue, &pv, 0, nullptr);
    std::printf("Preset: %s (vcf_freq=%.3f, chorus=%.0f, model=%.0f)\n", lilv_state_get_label(state),
                pv.v[pv.index["vcf_freq"]], pv.v[pv.index["chorus"]], pv.v[pv.index["model"]]);
    lilv_state_free(state);
  }

  std::vector<float> outL(block), outR(block);
  lilv_instance_connect_port(inst, 1, outL.data());
  lilv_instance_connect_port(inst, 2, outR.data());

  // Atom sequence buffer for MIDI
  alignas(8) uint8_t seqBuf[4096];
  auto* seq = reinterpret_cast<LV2_Atom_Sequence*>(seqBuf);
  lilv_instance_connect_port(inst, 0, seq);
  const LV2_URID midiType = mapUri(nullptr, LV2_MIDI__MidiEvent);
  const LV2_URID seqType = mapUri(nullptr, LV2_ATOM__Sequence);

  struct Ev { uint32_t frame; uint8_t msg[3]; };
  auto fillSeq = [&](const std::vector<Ev>& evs) {
    seq->atom.type = seqType;
    seq->atom.size = sizeof(LV2_Atom_Sequence_Body);
    seq->body.unit = 0; seq->body.pad = 0;
    for (const auto& e : evs)
    {
      auto* ev = reinterpret_cast<LV2_Atom_Event*>(
        reinterpret_cast<uint8_t*>(&seq->body) + seq->atom.size);
      ev->time.frames = e.frame;
      ev->body.type = midiType;
      ev->body.size = 3;
      std::memcpy(ev + 1, e.msg, 3);
      seq->atom.size += static_cast<uint32_t>(lv2_atom_pad_size(sizeof(LV2_Atom_Event) + 3));
    }
  };

  lilv_instance_activate(inst);

  auto runSeconds = [&](double seconds, std::vector<Ev> first, double& rms, double& peak, bool& finite) {
    const int nBlocks = static_cast<int>(seconds * sr / block);
    double sum = 0.0; peak = 0.0; finite = true;
    for (int b = 0; b < nBlocks; b++)
    {
      fillSeq(b == 0 ? first : std::vector<Ev>{});
      lilv_instance_run(inst, block);
      for (uint32_t i = 0; i < block; i++)
      {
        if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) finite = false;
        sum += outL[i] * outL[i] + outR[i] * outR[i];
        peak = std::max(peak, static_cast<double>(std::fabs(outL[i])));
      }
    }
    rms = std::sqrt(sum / (2.0 * nBlocks * block));
  };

  double rms, peak; bool finite;
  auto t0 = std::chrono::steady_clock::now();
  runSeconds(2.0, { {5, {0x90, 60, 100}}, {17, {0x90, 64, 100}}, {33, {0x90, 67, 100}} }, rms, peak, finite);
  auto t1 = std::chrono::steady_clock::now();
  const double cpu = std::chrono::duration<double>(t1 - t0).count() / 2.0 * 100.0;
  std::printf("Chord held 2 s:  rms=%.4f peak=%.4f finite=%d  (CPU %.1f%% of one core)\n", rms, peak, finite, cpu);
  if (!finite || rms < 1e-3) { std::fprintf(stderr, "FAIL: no audible output\n"); failures++; }

  runSeconds(3.0, { {0, {0x80, 60, 0}}, {0, {0x80, 64, 0}}, {0, {0x80, 67, 0}} }, rms, peak, finite);
  double tailRms = rms;
  runSeconds(1.0, {}, rms, peak, finite);
  std::printf("After release:   rms=%.5f (last 1 s, release tail was %.4f)\n", rms, tailRms);
  if (!finite || rms > 0.01) { std::fprintf(stderr, "FAIL: notes did not release\n"); failures++; }

  // Arpeggiator + Hold through control ports
  pv.v[pv.index["arp_on"]] = 1.f;
  pv.v[pv.index["hold"]] = 1.f;
  runSeconds(0.5, { {0, {0x90, 60, 100}}, {1, {0x90, 64, 100}} }, rms, peak, finite);
  runSeconds(1.0, { {0, {0x80, 60, 0}}, {0, {0x80, 64, 0}} }, rms, peak, finite);
  std::printf("Arp + Hold:      rms=%.4f after key release (expected audible)\n", rms);
  if (!finite || rms < 1e-3) { std::fprintf(stderr, "FAIL: hold/arp\n"); failures++; }

  // Switch every enumerated / toggled setting while playing
  pv.v[pv.index["voices"]] = 10.f;
  pv.v[pv.index["oversample"]] = 4.f;
  pv.v[pv.index["model"]] = 0.f;
  pv.v[pv.index["chorus"]] = 3.f;
  runSeconds(0.5, {}, rms, peak, finite);
  std::printf("Settings change: rms=%.4f finite=%d\n", rms, finite);
  if (!finite) { std::fprintf(stderr, "FAIL: non-finite output after settings change\n"); failures++; }

  pv.v[pv.index["hold"]] = 0.f;
  pv.v[pv.index["arp_on"]] = 0.f;
  runSeconds(3.0, {}, rms, peak, finite);
  runSeconds(1.0, {}, rms, peak, finite);
  std::printf("Hold released:   rms=%.5f\n", rms);
  if (rms > 0.01) { std::fprintf(stderr, "FAIL: hold release\n"); failures++; }

  lilv_instance_deactivate(inst);
  lilv_instance_free(inst);
  lilv_node_free(presetNode);
  lilv_node_free(controlClass);
  lilv_node_free(uri);
  lilv_world_free(world);

  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nAll checks passed\n", failures);
  return failures ? 1 : 0;
}
