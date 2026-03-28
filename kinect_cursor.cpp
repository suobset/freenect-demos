#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>

#include "frame_grabber.hpp"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace {

volatile std::sig_atomic_t g_should_stop = 0;

#ifdef __APPLE__
struct ClickGestureState {
  double baseline_depth = 0.0;
  double last_delta = 0.0;
  bool initialized = false;
  bool armed = true;
  int click_count = 0;
  std::chrono::steady_clock::time_point last_click_at = std::chrono::steady_clock::now();
};

void PerformLeftClick(double x, double y) {
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
}

bool MaybeTriggerAirTap(ClickGestureState& state, const blog_demos::BlobStats& blob, double x, double y) {
  if (!blob.found) {
    state.initialized = false;
    state.armed = true;
    state.last_delta = 0.0;
    return false;
  }

  if (!state.initialized) {
    state.baseline_depth = blob.nearest_depth;
    state.initialized = true;
  }

  state.baseline_depth = blog_demos::Lerp(state.baseline_depth, blob.nearest_depth, state.armed ? 0.04 : 0.18);
  state.last_delta = state.baseline_depth - blob.nearest_depth;

  const auto now = std::chrono::steady_clock::now();
  const bool cooled_down = (now - state.last_click_at) > std::chrono::milliseconds(350);

  if (state.armed && cooled_down && state.last_delta > 95.0 && blob.active_pixels > 300) {
    PerformLeftClick(x, y);
    state.armed = false;
    state.last_click_at = now;
    ++state.click_count;
    return true;
  }

  if (!state.armed && state.last_delta < 35.0) {
    state.armed = true;
  }

  return false;
}
#endif

void HandleSignal(int) {
  g_should_stop = 1;
}

}  // namespace

int main() {
#ifndef __APPLE__
  std::cerr << "This demo uses CoreGraphics and currently only targets macOS.\n";
  return 1;
#else
  std::signal(SIGINT, HandleSignal);
  std::cout << "kinect_cursor: move your hand to move the cursor, air-tap forward to click, Ctrl+C to quit.\n";
  std::cout << "Tip: keep one hand roughly 40-90cm from the sensor.\n";

  Freenect::Freenect freenect;
  auto& device = freenect.createDevice<blog_demos::FrameGrabber>(0);
  device.setDepthFormat(FREENECT_DEPTH_11BIT);
  device.startDepth();

  const CGRect screen = CGDisplayBounds(CGMainDisplayID());
  const double screen_width = CGRectGetWidth(screen);
  const double screen_height = CGRectGetHeight(screen);

  std::vector<uint16_t> depth(blog_demos::kFrameWidth * blog_demos::kFrameHeight);
  double smooth_x = screen_width / 2.0;
  double smooth_y = screen_height / 2.0;
  auto last_log = std::chrono::steady_clock::now();
  ClickGestureState click_state;

  while (!g_should_stop) {
    if (!device.GetLatestDepth(depth)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    const auto blob = blog_demos::FindForegroundBlob(depth, 120, 2);
    if (!blob.found) {
      MaybeTriggerAirTap(click_state, blob, smooth_x, smooth_y);
      continue;
    }

    const double normalized_x =
        blog_demos::Clamp(blob.center_x / (blog_demos::kFrameWidth - 1.0), 0.0, 1.0);
    const double normalized_y =
        blog_demos::Clamp(blob.center_y / (blog_demos::kFrameHeight - 1.0), 0.0, 1.0);

    const double target_x = normalized_x * screen_width;
    const double target_y = normalized_y * screen_height;

    smooth_x = blog_demos::Lerp(smooth_x, target_x, 0.18);
    smooth_y = blog_demos::Lerp(smooth_y, target_y, 0.18);

    CGWarpMouseCursorPosition(CGPointMake(smooth_x, smooth_y));
    const bool clicked = MaybeTriggerAirTap(click_state, blob, smooth_x, smooth_y);

    const auto now = std::chrono::steady_clock::now();
    if (clicked || now - last_log > std::chrono::milliseconds(600)) {
      std::cout << "cursor x=" << static_cast<int>(smooth_x)
                << " y=" << static_cast<int>(smooth_y)
                << " depth=" << static_cast<int>(blob.nearest_depth)
                << " delta=" << static_cast<int>(click_state.last_delta)
                << " clicks=" << click_state.click_count
                << (clicked ? " [click]" : "") << "\n";
      last_log = now;
    }
  }

  device.stopDepth();
  return 0;
#endif
}
