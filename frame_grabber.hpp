#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../libfreenect.hpp"

/// The blog_demos namespace abstracts the libfreenect API for the purposes of our demos
namespace blog_demos {

constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 480;

// Small helper that owns the latest RGB and depth frames so the rest of the
// demos can think in terms of "current image" instead of raw device callbacks.
class FrameGrabber : public Freenect::FreenectDevice {
 public:
  FrameGrabber(freenect_context* context, int index)
      : Freenect::FreenectDevice(context, index),
        rgb_buffer_(freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB).bytes),
        depth_buffer_(freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT).bytes / 2),
        has_rgb_(false),
        has_depth_(false) {
    setVideoFormat(FREENECT_VIDEO_RGB);
    setDepthFormat(FREENECT_DEPTH_11BIT);
  }

  void VideoCallback(void* rgb, uint32_t) override {
    // libfreenect gives us a pointer into its callback-owned buffer. Copy it so
    // the demo code can safely read the latest full frame at its own pace.
    std::lock_guard<std::mutex> lock(rgb_mutex_);
    auto* src = static_cast<uint8_t*>(rgb);
    std::copy(src, src + getVideoBufferSize(), rgb_buffer_.begin());
    has_rgb_ = true;
  }

  void DepthCallback(void* depth, uint32_t) override {
    // Depth comes in as 11-bit samples packed into 16-bit integers. We copy the
    // raw values so later stages can decide whether to visualize, threshold, or
    // project them into 3D.
    std::lock_guard<std::mutex> lock(depth_mutex_);
    auto* src = static_cast<uint16_t*>(depth);
    std::copy(src, src + (getDepthBufferSize() / 2), depth_buffer_.begin());
    has_depth_ = true;
  }

  bool GetLatestRgb(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(rgb_mutex_);
    if (!has_rgb_) {
      return false;
    }
    out = rgb_buffer_;
    has_rgb_ = false;
    return true;
  }

  bool GetLatestDepth(std::vector<uint16_t>& out) {
    std::lock_guard<std::mutex> lock(depth_mutex_);
    if (!has_depth_) {
      return false;
    }
    out = depth_buffer_;
    has_depth_ = false;
    return true;
  }

 private:
  std::mutex rgb_mutex_;
  std::mutex depth_mutex_;
  std::vector<uint8_t> rgb_buffer_;
  std::vector<uint16_t> depth_buffer_;
  bool has_rgb_;
  bool has_depth_;
};

// This is the single "control signal" struct shared by the interactive demos:
// if we can estimate one stable foreground blob, we have enough information to
// drive cursor motion, gesture detection, and debug overlays.
struct BlobStats {
  bool found = false;
  double center_x = 0.0;
  double center_y = 0.0;
  double nearest_depth = 0.0;
  std::size_t active_pixels = 0;
};

inline int EstimateNearDepth(const std::vector<uint16_t>& depth) {
  // Build a lightweight depth histogram from every other pixel. The demos do
  // not need exact statistics here; they just need a stable guess for "what is
  // the nearest coherent thing in front of the sensor right now?"
  std::vector<int> histogram(2048, 0);
  int samples = 0;

  for (int y = 0; y < kFrameHeight; y += 2) {
    for (int x = 0; x < kFrameWidth; x += 2) {
      const uint16_t value = depth[y * kFrameWidth + x];
      if (value >= 350 && value < 1900) {
        ++histogram[value];
        ++samples;
      }
    }
  }

  if (samples < 400) {
    return 0;
  }

  // Use a very low percentile instead of the absolute minimum so a few noisy
  // pixels do not suddenly become "the hand".
  const int percentile = std::max(1, samples / 200);
  int running = 0;
  for (int depth_value = 350; depth_value < 1900; ++depth_value) {
    running += histogram[depth_value];
    if (running >= percentile) {
      return depth_value;
    }
  }

  return 0;
}

inline BlobStats FindForegroundBlob(const std::vector<uint16_t>& depth,
                                    int band_size = 140,
                                    int step = 2) {
  BlobStats result;
  const int nearest = EstimateNearDepth(depth);
  if (nearest == 0) {
    return result;
  }

  // Keep only a shallow band near the nearest depth estimate. This discards
  // most of the background and turns "a whole depth image" into "the closest
  // blob we care about right now."
  const int upper = std::min(2047, nearest + band_size);
  double weighted_x = 0.0;
  double weighted_y = 0.0;
  double weight_sum = 0.0;

  for (int y = 0; y < kFrameHeight; y += step) {
    for (int x = 0; x < kFrameWidth; x += step) {
      const uint16_t value = depth[y * kFrameWidth + x];
      if (value < nearest || value > upper) {
        continue;
      }
      // Closer points get a larger weight, so the centroid is biased toward the
      // most foreground part of the blob instead of the plain geometric center.
      const double closeness = static_cast<double>(upper - value + 1);
      weighted_x += x * closeness;
      weighted_y += y * closeness;
      weight_sum += closeness;
      ++result.active_pixels;
    }
  }

  if (result.active_pixels < 250 || weight_sum <= 0.0) {
    return BlobStats{};
  }

  result.found = true;
  result.center_x = weighted_x / weight_sum;
  result.center_y = weighted_y / weight_sum;
  result.nearest_depth = static_cast<double>(nearest);
  return result;
}

inline double Clamp(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

// Tiny helper for all the "move a little toward the target" behavior in the
// demos: cursor smoothing, click baselines, and any other low-pass filtering.
inline double Lerp(double a, double b, double t) {
  return a + (b - a) * t;
}

}  // namespace blog_demos
