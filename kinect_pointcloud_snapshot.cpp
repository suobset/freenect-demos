#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "frame_grabber.hpp"

namespace {

bool WritePointCloud(const std::string& output_path,
                     const std::vector<uint16_t>& depth,
                     const std::vector<uint8_t>& rgb) {
  std::ofstream out(output_path);
  if (!out) {
    return false;
  }

  std::size_t point_count = 0;
  for (std::size_t index = 0; index < depth.size(); ++index) {
    if (depth[index] > 0 && depth[index] < 2047) {
      ++point_count;
    }
  }

  out << "ply\n";
  out << "format ascii 1.0\n";
  out << "element vertex " << point_count << "\n";
  out << "property float x\n";
  out << "property float y\n";
  out << "property float z\n";
  out << "property uchar red\n";
  out << "property uchar green\n";
  out << "property uchar blue\n";
  out << "end_header\n";

  constexpr float fx = 594.21f;
  constexpr float fy = 591.04f;
  constexpr float cx = 339.5f;
  constexpr float cy = 242.7f;

  for (int y = 0; y < blog_demos::kFrameHeight; ++y) {
    for (int x = 0; x < blog_demos::kFrameWidth; ++x) {
      const std::size_t index = static_cast<std::size_t>(y) * blog_demos::kFrameWidth + x;
      const uint16_t raw_depth = depth[index];
      if (raw_depth == 0 || raw_depth >= 2047) {
        continue;
      }

      const float z = raw_depth * 0.001f;
      const float world_x = (static_cast<float>(x) - cx) * z / fx;
      const float world_y = (static_cast<float>(y) - cy) * z / fy;
      const std::size_t rgb_index = index * 3;
      out << world_x << ' ' << world_y << ' ' << z << ' '
          << static_cast<int>(rgb[rgb_index + 0]) << ' '
          << static_cast<int>(rgb[rgb_index + 1]) << ' '
          << static_cast<int>(rgb[rgb_index + 2]) << '\n';
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string output_path = argc > 1 ? argv[1] : "snapshot.ply";
  std::cout << "kinect_pointcloud_snapshot: waiting for fresh RGB + depth frames...\n";

  Freenect::Freenect freenect;
  auto& device = freenect.createDevice<blog_demos::FrameGrabber>(0);
  device.setDepthFormat(FREENECT_DEPTH_REGISTERED);
  device.setVideoFormat(FREENECT_VIDEO_RGB);
  device.startDepth();
  device.startVideo();

  std::vector<uint16_t> depth(blog_demos::kFrameWidth * blog_demos::kFrameHeight);
  std::vector<uint8_t> rgb(blog_demos::kFrameWidth * blog_demos::kFrameHeight * 3);

  bool has_depth = false;
  bool has_rgb = false;
  for (int attempts = 0; attempts < 300; ++attempts) {
    has_depth = device.GetLatestDepth(depth) || has_depth;
    has_rgb = device.GetLatestRgb(rgb) || has_rgb;
    if (has_depth && has_rgb) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  device.stopVideo();
  device.stopDepth();

  if (!has_depth || !has_rgb) {
    std::cerr << "Timed out waiting for frames from the Kinect.\n";
    return 1;
  }

  if (!WritePointCloud(output_path, depth, rgb)) {
    std::cerr << "Could not write point cloud to " << output_path << "\n";
    return 1;
  }

  std::cout << "Wrote " << output_path << "\n";
  std::cout << "Open it in MeshLab, CloudCompare, or Blender for the fun part.\n";
  return 0;
}
