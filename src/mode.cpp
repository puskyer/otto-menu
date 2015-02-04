#include "gfx.hpp"
#include "gtx/rotate_vector.hpp"
#include "nanosvg.h"
#include "choreograph/Choreograph.h"

#include <iostream>
#include <vector>
#include <chrono>

#define STAK_EXPORT extern "C"

using namespace glm;
using namespace choreograph;

using fsecs = std::chrono::duration<float>;

static const float TWO_PI = M_PI * 2.0f;

static const float screenWidth = 96.0f;
static const float screenHeight = 96.0f;

static float regularPolyRadius(float sideLen, uint32_t numSides) {
  return sideLen / (2.0f * std::sin(M_PI / numSides));
}

static float tileDiameter = screenWidth * 0.95f;
static float wheelEdgeLen = screenWidth * 1.1f;
static float wheelRadius = regularPolyRadius(wheelEdgeLen, 6);

static bool wheelIsMoving = false;

static const vec3 tileDefaultColor = { 0, 1, 1 };

struct AngularParticle {
  float angle = 0.0f;
  float anglePrev = angle;

  float friction = 0.0f;

  void step() {
    float vel = (angle - anglePrev) * (1.0f - friction);

    anglePrev = angle;
    angle = angle + vel;

    // Wrap angle to the range 0-2pi. Assumes angle is not less than -2pi.
    float wrappedAngle = std::fmod(angle + TWO_PI, TWO_PI);
    if (wrappedAngle != angle) {
      anglePrev += wrappedAngle - angle;
      angle = wrappedAngle;
    }
  }

  void spring(float targetAngle, float power) {
    float angleDiff = std::abs(targetAngle - angle);
    if (std::abs(angle - (targetAngle + TWO_PI)) < angleDiff) {
      targetAngle += TWO_PI;
    }
    angle = angle + (targetAngle - angle) * power;
  }
};

struct Tile {
  Output<vec3> color = tileDefaultColor;
  Output<float> scale = 1.0f;

  std::shared_ptr<NSVGimage> numberSvg;
};

struct ModeData {
  ch::Timeline timeline;

  AngularParticle wheel;

  std::chrono::steady_clock::time_point lastFrameTime;
  std::chrono::steady_clock::time_point lastCrankTime;

  std::array<Tile, 6> tiles;
};

static ModeData data;

STAK_EXPORT int init() {
  // Load number graphics
  for (int i = 0; i < data.tiles.size(); ++i) {
    auto filename = "assets/" + std::to_string(i + 1) + ".svg";
    auto img = nsvgParseFromFile(filename.c_str(), "px", 96);
    data.tiles[i].numberSvg = std::shared_ptr<NSVGimage>(img, nsvgDelete);
  }

  data.wheel.friction = 0.2f;
  data.lastFrameTime = std::chrono::steady_clock::now();

  otto::initFontOCRA();

  return 0;
}

STAK_EXPORT int shutdown() { return 0; }

STAK_EXPORT int update() {
  using namespace otto;
  using namespace std::chrono;

  static const mat3 defaultMatrix{ 0.0f,        -1.0f,        -0.0f,
                                   -1.0f,       0.0f,         0.0f,
                                   screenWidth, screenHeight, 1.0f };

  auto frameTime = steady_clock::now();
  auto dt = duration_cast<fsecs>(frameTime - data.lastFrameTime).count();

  data.timeline.step(dt);

  clearColor(0, 0, 0);
  clear(0, 0, 96, 96);

  setTransform(defaultMatrix);
  translate(48, 48);

  translate(wheelRadius, 0.0f);
  rotate(data.wheel.angle);

  strokeWidth(2.0f);
  strokeColor(1, 0, 0);

  float angleIncr = -TWO_PI / 6.0f;
  int i = 1;
  for (const auto &tile : data.tiles) {
    pushTransform();
      translate(vec2(-wheelRadius, 0.0f));
      scale(tile.scale());

      beginPath();
      circle(vec2(), tileDiameter * 0.5f);
      fillColor(tile.color());
      fill();

      translate(-20, 20);
      fillColor(0, 0, 0);
      text(std::to_string(i++));
    popTransform();

    rotate(angleIncr);
  }

  data.wheel.step();

  auto timeSinceLastCrank = frameTime - data.lastCrankTime;
  if (timeSinceLastCrank > std::chrono::milliseconds(300)) {
    int tileIndex = std::fmod(std::round(data.wheel.angle / TWO_PI * 6.0f), 6.0f);

    auto &tile = data.tiles[tileIndex];
    data.timeline.apply(&tile.color).then<RampTo>(vec3(1, 1, 0), 0.1f);
    data.timeline.apply(&tile.scale).then<RampTo>(1.0f, 0.1f);

    data.wheel.spring(tileIndex / 6.0f * TWO_PI, 0.2f);

    wheelIsMoving = false;
  }

  data.lastFrameTime = frameTime;

  return 0;
}

STAK_EXPORT int rotary_changed(int delta) {
  data.wheel.angle += delta * 0.02f;
  data.lastCrankTime = std::chrono::steady_clock::now();

  if (!wheelIsMoving) {
    wheelIsMoving = true;
    for (auto &tile : data.tiles) {
      data.timeline.apply(&tile.color).then<RampTo>(tileDefaultColor, 0.2f);
      data.timeline.apply(&tile.scale).then<RampTo>(0.7f, 0.2f);
    }
  }

  return 0;
}
