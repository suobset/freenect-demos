#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <ApplicationServices/ApplicationServices.h>
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "frame_grabber.hpp"

namespace {

using blog_demos::BlobStats;
using blog_demos::FrameGrabber;
using blog_demos::kFrameHeight;
using blog_demos::kFrameWidth;

volatile std::sig_atomic_t g_should_stop = 0;

FrameGrabber* g_device = nullptr;
GLuint g_depth_texture = 0;
GLuint g_rgb_texture = 0;
int g_window = 0;

std::vector<uint16_t> g_depth_raw(kFrameWidth * kFrameHeight);
std::vector<uint8_t> g_depth_rgb(kFrameWidth * kFrameHeight * 3);
std::vector<uint8_t> g_rgb(kFrameWidth * kFrameHeight * 3);
std::vector<uint16_t> g_gamma(2048);

bool g_have_depth = false;
bool g_have_rgb = false;
bool g_cursor_enabled = true;
bool g_click_enabled = true;
bool g_freeze_tracking = false;

double g_smooth_x = 0.0;
double g_smooth_y = 0.0;
double g_filtered_norm_x = 0.5;
double g_filtered_norm_y = 0.5;
double g_screen_width = 0.0;
double g_screen_height = 0.0;
double g_tilt_angle = 0.0;
double g_accel_x = 0.0;
double g_accel_y = 0.0;
double g_accel_z = 0.0;
BlobStats g_last_blob;

struct ClickGestureState {
  double baseline_depth = 0.0;
  double last_delta = 0.0;
  bool initialized = false;
  bool armed = true;
  int click_count = 0;
  std::chrono::steady_clock::time_point last_click_at = std::chrono::steady_clock::now();
};

ClickGestureState g_click_state;
std::chrono::steady_clock::time_point g_last_frame_at = std::chrono::steady_clock::now();

void HandleSignal(int) {
  g_should_stop = 1;
}

void RenderBitmapString(float x, float y, const std::string& text) {
  glRasterPos2f(x, y);
  for (char ch : text) {
    glutBitmapCharacter(GLUT_BITMAP_8_BY_13, ch);
  }
}

void PerformLeftClick(double x, double y) {
#ifdef __APPLE__
  CGPoint point = CGPointMake(x, y);
  CGEventRef down = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);
  CGEventRef up = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, point, kCGMouseButtonLeft);
  if (down != nullptr) {
    CGEventPost(kCGHIDEventTap, down);
    CFRelease(down);
  }
  if (up != nullptr) {
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(up);
  }
#else
  (void)x;
  (void)y;
#endif
}

bool MaybeTriggerAirTap(const BlobStats& blob) {
  if (!g_click_enabled) {
    return false;
  }

  if (!blob.found) {
    g_click_state.initialized = false;
    g_click_state.armed = true;
    g_click_state.last_delta = 0.0;
    return false;
  }

  if (!g_click_state.initialized) {
    g_click_state.baseline_depth = blob.nearest_depth;
    g_click_state.initialized = true;
  }

  g_click_state.baseline_depth = blog_demos::Lerp(
      g_click_state.baseline_depth, blob.nearest_depth, g_click_state.armed ? 0.04 : 0.18);
  g_click_state.last_delta = g_click_state.baseline_depth - blob.nearest_depth;

  const auto now = std::chrono::steady_clock::now();
  const bool cooled_down = (now - g_click_state.last_click_at) > std::chrono::milliseconds(350);

  if (g_click_state.armed && cooled_down && g_click_state.last_delta > 95.0 && blob.active_pixels > 300) {
    PerformLeftClick(g_smooth_x, g_smooth_y);
    g_click_state.armed = false;
    g_click_state.last_click_at = now;
    ++g_click_state.click_count;
    return true;
  }

  if (!g_click_state.armed && g_click_state.last_delta < 35.0) {
    g_click_state.armed = true;
  }

  return false;
}

void InitTextures() {
  glGenTextures(1, &g_depth_texture);
  glBindTexture(GL_TEXTURE_2D, g_depth_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glGenTextures(1, &g_rgb_texture);
  glBindTexture(GL_TEXTURE_2D, g_rgb_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void InitGamma() {
  for (std::size_t i = 0; i < g_gamma.size(); ++i) {
    const float normalized = static_cast<float>(i) / 2048.0f;
    g_gamma[i] = static_cast<uint16_t>(std::pow(normalized, 3.0f) * 6.0f * 6.0f * 256.0f);
  }
}

void ColorizeDepth(const std::vector<uint16_t>& depth, std::vector<uint8_t>& out) {
  for (int i = 0; i < kFrameWidth * kFrameHeight; ++i) {
    const uint16_t pval = g_gamma[depth[i]];
    const int lb = pval & 0xff;
    switch (pval >> 8) {
      case 0:
        out[3 * i + 0] = 255;
        out[3 * i + 1] = 255 - lb;
        out[3 * i + 2] = 255 - lb;
        break;
      case 1:
        out[3 * i + 0] = 255;
        out[3 * i + 1] = lb;
        out[3 * i + 2] = 0;
        break;
      case 2:
        out[3 * i + 0] = 255 - lb;
        out[3 * i + 1] = 255;
        out[3 * i + 2] = 0;
        break;
      case 3:
        out[3 * i + 0] = 0;
        out[3 * i + 1] = 255;
        out[3 * i + 2] = lb;
        break;
      case 4:
        out[3 * i + 0] = 0;
        out[3 * i + 1] = 255 - lb;
        out[3 * i + 2] = 255;
        break;
      case 5:
        out[3 * i + 0] = 0;
        out[3 * i + 1] = 0;
        out[3 * i + 2] = 255 - lb;
        break;
      default:
        out[3 * i + 0] = 0;
        out[3 * i + 1] = 0;
        out[3 * i + 2] = 0;
        break;
    }
  }
}

void RefreshTelemetry() {
  g_device->updateState();
  Freenect::FreenectTiltState state = g_device->getState();
  g_tilt_angle = state.getTiltDegs();
  state.getAccelerometers(&g_accel_x, &g_accel_y, &g_accel_z);
}

void DrawTexturedPane(GLuint texture, float left, float right, float bottom, float top) {
  glBindTexture(GL_TEXTURE_2D, texture);
  glBegin(GL_TRIANGLE_FAN);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(left, bottom);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(right, bottom);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(right, top);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(left, top);
  glEnd();
}

void DrawCrosshair(float origin_x, const BlobStats& blob) {
  if (!blob.found) {
    return;
  }

  const float x = origin_x + static_cast<float>(blob.center_x);
  const float y = static_cast<float>(kFrameHeight - blob.center_y);
  const float radius = 12.0f;

  glColor3f(1.0f, 1.0f, 1.0f);
  glBegin(GL_LINES);
  glVertex2f(x - radius, y);
  glVertex2f(x + radius, y);
  glVertex2f(x, y - radius);
  glVertex2f(x, y + radius);
  glEnd();
}

void DrawClickPulse() {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_click_state.last_click_at)
          .count();
  if (elapsed > 150) {
    return;
  }

  glColor3f(1.0f, 0.3f, 0.3f);
  glBegin(GL_LINE_LOOP);
  glVertex2f(610.0f, 20.0f);
  glVertex2f(670.0f, 20.0f);
  glVertex2f(670.0f, 80.0f);
  glVertex2f(610.0f, 80.0f);
  glEnd();
}

void DrawTextPanel() {
  char line[256];

  glColor3f(1.0f, 1.0f, 1.0f);
  RenderBitmapString(20.0f, 525.0f, "kinect_cursor_debug");
  RenderBitmapString(20.0f,
                     505.0f,
                     "Esc: quit  c: cursor  g: clicks  space: freeze  j/k: tilt  0-6: LED");

  std::snprintf(line,
                sizeof(line),
                "blob found: %s  center: (%.1f, %.1f)  nearest depth: %.0f  active pixels: %zu",
                g_last_blob.found ? "yes" : "no",
                g_last_blob.center_x,
                g_last_blob.center_y,
                g_last_blob.nearest_depth,
                g_last_blob.active_pixels);
  RenderBitmapString(20.0f, 480.0f, line);

  std::snprintf(line,
                sizeof(line),
                "cursor: %s  click gesture: %s  pointer: (%.0f, %.0f)  screen: %.0fx%.0f",
                g_cursor_enabled ? "on" : "off",
                g_click_enabled ? "air tap" : "off",
                g_smooth_x,
                g_smooth_y,
                g_screen_width,
                g_screen_height);
  RenderBitmapString(20.0f, 460.0f, line);

  std::snprintf(line,
                sizeof(line),
                "clicks: %d  delta: %.0f  armed: %s",
                g_click_state.click_count,
                g_click_state.last_delta,
                g_click_state.armed ? "yes" : "no");
  RenderBitmapString(20.0f, 440.0f, line);

  std::snprintf(line,
                sizeof(line),
                "tilt: %.1f  accel: (%.3f, %.3f, %.3f)",
                g_tilt_angle,
                g_accel_x,
                g_accel_y,
                g_accel_z);
  RenderBitmapString(20.0f, 420.0f, line);

  const auto age_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_last_frame_at)
          .count();
  std::snprintf(line, sizeof(line), "last frame age: %lld ms", static_cast<long long>(age_ms));
  RenderBitmapString(20.0f, 400.0f, line);
}

void UpdateFramesAndCursor() {
  const bool got_rgb = g_device->GetLatestRgb(g_rgb);
  const bool got_depth = g_device->GetLatestDepth(g_depth_raw);

  if (!got_rgb && !got_depth) {
    return;
  }

  g_last_frame_at = std::chrono::steady_clock::now();

  if (got_rgb) {
    g_have_rgb = true;
  }
  if (got_depth) {
    g_have_depth = true;
    ColorizeDepth(g_depth_raw, g_depth_rgb);
  }

  RefreshTelemetry();

  if (!g_freeze_tracking && got_depth) {
    g_last_blob = blog_demos::FindForegroundBlob(g_depth_raw, 120, 2);
  }

  if (!g_last_blob.found) {
    MaybeTriggerAirTap(g_last_blob);
    return;
  }

  if (g_cursor_enabled) {
    const double normalized_x = blog_demos::Clamp(g_last_blob.center_x / (kFrameWidth - 1.0), 0.0, 1.0);
    const double normalized_y = blog_demos::Clamp(g_last_blob.center_y / (kFrameHeight - 1.0), 0.0, 1.0);
    g_filtered_norm_x = blog_demos::Lerp(g_filtered_norm_x, normalized_x, 0.10);
    g_filtered_norm_y = blog_demos::Lerp(g_filtered_norm_y, normalized_y, 0.10);

    const double centered_x = (g_filtered_norm_x - 0.5) * 0.7 + 0.5;
    const double centered_y = (g_filtered_norm_y - 0.5) * 0.7 + 0.5;
    const double target_x = blog_demos::Clamp(centered_x, 0.0, 1.0) * g_screen_width;
    const double target_y = blog_demos::Clamp(centered_y, 0.0, 1.0) * g_screen_height;

    const double max_step = 28.0;
    const double delta_x = blog_demos::Clamp(target_x - g_smooth_x, -max_step, max_step);
    const double delta_y = blog_demos::Clamp(target_y - g_smooth_y, -max_step, max_step);
    g_smooth_x += delta_x;
    g_smooth_y += delta_y;
#ifdef __APPLE__
    CGWarpMouseCursorPosition(CGPointMake(g_smooth_x, g_smooth_y));
#endif
  }

  MaybeTriggerAirTap(g_last_blob);
}

void DrawScene() {
  if (g_should_stop) {
    g_device->stopVideo();
    g_device->stopDepth();
    glutDestroyWindow(g_window);
    return;
  }

  UpdateFramesAndCursor();

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glLoadIdentity();
  glEnable(GL_TEXTURE_2D);

  if (g_have_depth) {
    glBindTexture(GL_TEXTURE_2D, g_depth_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kFrameWidth, kFrameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, g_depth_rgb.data());
    DrawTexturedPane(g_depth_texture, 0.0f, 640.0f, 0.0f, 480.0f);
  }

  if (g_have_rgb) {
    glBindTexture(GL_TEXTURE_2D, g_rgb_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kFrameWidth, kFrameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, g_rgb.data());
    DrawTexturedPane(g_rgb_texture, 640.0f, 1280.0f, 0.0f, 480.0f);
  }

  glDisable(GL_TEXTURE_2D);
  DrawCrosshair(0.0f, g_last_blob);
  DrawCrosshair(640.0f, g_last_blob);
  DrawClickPulse();
  DrawTextPanel();
  glutSwapBuffers();
}

void HandleKey(unsigned char key, int, int) {
  switch (key) {
    case 27:
      g_should_stop = 1;
      break;
    case 'c':
      g_cursor_enabled = !g_cursor_enabled;
      break;
    case 'g':
      g_click_enabled = !g_click_enabled;
      break;
    case ' ':
      g_freeze_tracking = !g_freeze_tracking;
      break;
    case 'j':
      g_device->setTiltDegrees(blog_demos::Clamp(g_tilt_angle - 2.0, -30.0, 30.0));
      break;
    case 'k':
      g_device->setTiltDegrees(blog_demos::Clamp(g_tilt_angle + 2.0, -30.0, 30.0));
      break;
    case '0':
      g_device->setLed(LED_OFF);
      break;
    case '1':
      g_device->setLed(LED_GREEN);
      break;
    case '2':
      g_device->setLed(LED_RED);
      break;
    case '3':
      g_device->setLed(LED_YELLOW);
      break;
    case '4':
      g_device->setLed(LED_BLINK_GREEN);
      break;
    case '5':
      g_device->setLed(LED_BLINK_RED_YELLOW);
      break;
    case '6':
      g_device->setLed(LED_BLINK_RED_YELLOW);
      break;
    default:
      break;
  }
}

void ResizeScene(int, int) {
  glViewport(0, 0, 1280, 560);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, 1280.0, 0.0, 560.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

}  // namespace

int main(int argc, char** argv) {
#ifndef __APPLE__
  std::cerr << "This demo currently targets macOS because it uses CoreGraphics for cursor control.\n";
  return 1;
#else
  std::signal(SIGINT, HandleSignal);

  const CGRect screen = CGDisplayBounds(CGMainDisplayID());
  g_screen_width = CGRectGetWidth(screen);
  g_screen_height = CGRectGetHeight(screen);
  g_smooth_x = g_screen_width / 2.0;
  g_smooth_y = g_screen_height / 2.0;

  Freenect::Freenect freenect;
  FrameGrabber& device = freenect.createDevice<FrameGrabber>(0);
  g_device = &device;
  device.setDepthFormat(FREENECT_DEPTH_11BIT);
  device.setVideoFormat(FREENECT_VIDEO_RGB);
  device.startDepth();
  device.startVideo();

  InitGamma();

  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
  glutInitWindowSize(1280, 560);
  glutInitWindowPosition(40, 40);
  g_window = glutCreateWindow("Kinect Cursor Debug");

  InitTextures();
  ResizeScene(1280, 560);

  glutDisplayFunc(DrawScene);
  glutIdleFunc(DrawScene);
  glutReshapeFunc(ResizeScene);
  glutKeyboardFunc(HandleKey);
  glutMainLoop();
  return 0;
#endif
}
