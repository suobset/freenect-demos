# libfreenect demos

I got an Xbox 360 Kinect from Salvation Army for $3 a while ago. Cool hardware!!

These demos are deliberately small and slightly mischievous (if that's the right word).

https://github.com/user-attachments/assets/861a2729-086f-4885-a1bf-c13652979904

They do not install anything, do not rebuild the whole project, and do not try to compete with your Homebrew `libfreenect`. They reuse the C++ wrapper header in `wrappers/cpp/libfreenect.hpp` and link against the library you already have on the machine.

<img width="2880" height="2160" alt="image" src="https://github.com/user-attachments/assets/473cce0f-bceb-4d89-8ad3-1a0ad8d4d77e" />

## What's needed

These demos use [libfreenect](https://github.com/OpenKinect/libfreenect). On macOS, I downloaded it from [Homebrew](https://brew.sh/)

```
brew install libfreenect
```

Python may seem like the easier way here, but 3.14 breaks a lot of dependencies. I also didn't want to fight with Python in general, and am way more comfortable with C++. This project requires CMake 3.16+ and C++ 17.

Libfreenect gives IR/Camera/Accl/Mics access as a C++ library that you can then use for whatever. There's a Python wrapper, but it needs debugging. 

<img width="1392" height="624" alt="Screenshot 2026-03-27 at 3 00 27 PM" src="https://github.com/user-attachments/assets/fd134c82-6c67-4436-8f26-c4f710aa039d" />

https://github.com/user-attachments/assets/87d2295c-0cb3-4de6-b041-c6bf803fd4b5

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

I'll be writing a blog post on this soon and attach it here.
