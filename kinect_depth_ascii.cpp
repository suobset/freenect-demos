#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "frame_grabber.hpp"

namespace {

volatile std::sig_atomic_t g_should_stop = 0;

void HandleSignal(int) {
  g_should_stop = 1;
}

}  // namespace

int main() {
  std::signal(SIGINT, HandleSignal);
  std::cout << "kinect_depth_ascii: terminal silhouette mode. Ctrl+C to quit.\n";

  Freenect::Freenect freenect;
  auto& device = freenect.createDevice<blog_demos::FrameGrabber>(0);
  device.setDepthFormat(FREENECT_DEPTH_11BIT);
  device.startDepth();

  constexpr int output_width = 80;
  constexpr int output_height = 30;
  const std::string palette = " .:-=+*#%@";
  std::vector<uint16_t> depth(blog_demos::kFrameWidth * blog_demos::kFrameHeight);

  while (!g_should_stop) {
    if (!device.GetLatestDepth(depth)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const auto blob = blog_demos::FindForegroundBlob(depth, 180, 2);
    const int near_depth = blob.found ? static_cast<int>(blob.nearest_depth) : 700;
    const int far_depth = std::min(2047, near_depth + 500);

    std::cout << "\x1b[2J\x1b[H";
    std::cout << "nearest depth: " << near_depth
              << "  active pixels: " << blob.active_pixels << "\n\n";

    for (int row = 0; row < output_height; ++row) {
      for (int col = 0; col < output_width; ++col) {
        const int x0 = col * blog_demos::kFrameWidth / output_width;
        const int x1 = (col + 1) * blog_demos::kFrameWidth / output_width;
        const int y0 = row * blog_demos::kFrameHeight / output_height;
        const int y1 = (row + 1) * blog_demos::kFrameHeight / output_height;

        int valid = 0;
        double total = 0.0;
        for (int y = y0; y < y1; ++y) {
          for (int x = x0; x < x1; ++x) {
            const uint16_t value = depth[y * blog_demos::kFrameWidth + x];
            if (value < near_depth || value > far_depth) {
              continue;
            }
            total += value;
            ++valid;
          }
        }

        if (valid == 0) {
          std::cout << ' ';
          continue;
        }

        const double average = total / valid;
        const double normalized =
            1.0 - ((average - near_depth) / std::max(1, far_depth - near_depth));
        const std::size_t palette_index = static_cast<std::size_t>(
            blog_demos::Clamp(normalized, 0.0, 1.0) * (palette.size() - 1));
        std::cout << palette[palette_index];
      }
      std::cout << '\n';
    }

    std::cout.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }

  device.stopDepth();
  return 0;
}
