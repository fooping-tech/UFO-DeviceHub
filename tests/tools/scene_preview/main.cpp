// Renders the M5Dial screen (firmware/m5dial/dial_scene.cpp) on a PC.
//   scene_preview OUT_DIR   writes one PPM per frame (name: <scene>_<ms>.ppm)
// Build + contact sheet: tests/tools/scene_preview/run.sh
#include <cstdio>
#include <string>

#include "m5dial/dial_scene.h"

using namespace ufo;

static void save(const DialScene& s, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  const int n = DialScene::kSize;
  fprintf(f, "P6\n%d %d\n255\n", n, n);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float dx = x - (n - 1) / 2.0f, dy = y - (n - 1) / 2.0f;
      uint16_t p = dx * dx + dy * dy > 40.0f * 40.0f ? 0x2104 : s.pixel(x, y);  // outside the round glass: grey
      unsigned char rgb[3] = {static_cast<unsigned char>((p >> 11) << 3), static_cast<unsigned char>(((p >> 5) & 0x3F) << 2),
                              static_cast<unsigned char>((p & 0x1F) << 3)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : ".";
  struct Shot {
    const char* name;
    DialState state;
    int dial;
    bool link, session;
    uint32_t ms[8];
  };
  const Shot shots[] = {
      {"standby", DialState::Disabled, 1, true, false, {0, 400, 800, 1200, 1600, 2000, 2400, 2800}},
      {"ready", DialState::Ready, 1, true, true, {0, 400, 800, 1200, 1600, 2000, 2400, 2800}},
      {"waiting", DialState::Armed, 1, true, true, {0, 100, 200, 300, 400, 500, 600, 700}},
      {"gotball", DialState::BallDetected, 1, true, true, {50, 250, 500, 750, 1000, 1150, 1350, 2500}},
      {"release", DialState::Releasing, 2, true, true, {100, 400, 800, 1200, 1450, 1700, 1900, 2600}},
      {"done", DialState::Complete, 2, true, true, {0, 150, 300, 450, 800, 1200, 2000, 3000}},
      {"nohub", DialState::Disabled, 2, false, false, {0, 300, 600, 900, 1200, 1500, 1800, 2100}},
  };
  for (const auto& sh : shots) {
    DialScene scene;
    DisplayModel m;
    m.dial_number = sh.dial;
    m.link = sh.link;
    m.session = sh.session;
    m.mac = "24:58:7C:5C:D7:E4";
    if (sh.dial == 2) {
      m.r = 255;
      m.g = 0;
      m.b = 255;
    }
    m.op = sh.state == DialState::Disabled ? OperationMode::Standby : OperationMode::Game;
    m.cycle = 12;
    m.state = DialState::Ready;
    scene.render(m, 5000);  // the state before
    m.state = sh.state;
    m.switch_on = sh.state == DialState::BallDetected;
    for (uint32_t ms : sh.ms) {
      scene.render(m, 10000 + ms);
      char name[64];
      snprintf(name, sizeof(name), "/%s_%04u.ppm", sh.name, static_cast<unsigned>(ms));
      save(scene, out + name);
    }
  }
  return 0;
}
