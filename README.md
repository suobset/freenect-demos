# Kinect blog demos

These demos are deliberately small and slightly mischievous.

They do not install anything, do not rebuild the whole project, and do not try to compete with your Homebrew `libfreenect`. They reuse the C++ wrapper header in `wrappers/cpp/libfreenect.hpp` and link against the library you already have on the machine.

## What is here

- `kinect_cursor`: tracks the closest blob in the depth frame, moves the macOS cursor, and supports a depth-based air-tap click.
- `kinect_cursor_debug`: opens a `glview`-style window with depth, RGB, crosshairs, live tracking stats, and the same air-tap click gesture.
- `kinect_depth_ascii`: turns the nearest depth slice into a live ASCII silhouette in the terminal.
- `kinect_pointcloud_snapshot`: grabs one registered RGB + depth frame and writes a colored `.ply` point cloud.

## Build

From the repo root:

```bash
cmake -S wrappers/cpp/blog_demos -B build-blog-demos
cmake --build build-blog-demos -j
```

If CMake cannot find Homebrew packages automatically, point it at Homebrew explicitly:

```bash
cmake -S wrappers/cpp/blog_demos -B build-blog-demos \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
```

## Run

```bash
./build-blog-demos/kinect_depth_ascii
./build-blog-demos/kinect_cursor
./build-blog-demos/kinect_cursor_debug
./build-blog-demos/kinect_pointcloud_snapshot hand_scan.ply
```

## Notes

- `kinect_cursor` is macOS-only because it uses `ApplicationServices` to move the system pointer.
- `kinect_cursor_debug` needs OpenGL and GLUT, like the existing viewer demos in this repo.
- Cursor control is intentionally simple: it follows the nearest depth blob with smoothing, which works surprisingly well for blog-demo purposes.
- Clicking uses an air-tap gesture: move your hand forward toward the Kinect briefly, then pull back to re-arm the click detector.
- For the cursor demos on macOS, you may need to grant Accessibility permissions to the terminal app that launches them.
- The point cloud exporter writes plain ASCII PLY so you can open it in MeshLab, CloudCompare, or Blender.
- If the cursor demo feels twitchy, stand farther back and keep only one hand in the active depth band.
