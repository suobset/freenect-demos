#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../libfreenect.hpp"

namespace blog_demos {

constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 480;

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
    std::lock_guard<std::mutex> lock(rgb_mutex_);
    auto* src = static_cast<uint8_t*>(rgb);
    std::copy(src, src + getVideoBufferSize(), rgb_buffer_.begin());
    has_rgb_ = true;
  }

  void DepthCallback(void* depth, uint32_t) override {
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

struct BlobStats {
  bool found = false;
  double center_x = 0.0;
  double center_y = 0.0;
  double nearest_depth = 0.0;
  std::size_t active_pixels = 0;
};

inline int EstimateNearDepth(const std::vector<uint16_t>& depth) {
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

inline double Lerp(double a, double b, double t) {
  return a + (b - a) * t;
}

}  // namespace blog_demos
