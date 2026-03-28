# How To Build Your Own Kinect Demo

This is the version I would want if I came back to this repo in six months and had forgotten everything.

## The shortest path

1. Reuse `wrappers/cpp/libfreenect.hpp`.
2. Link against the Homebrew-installed `libfreenect` instead of building the whole repo.
3. Start with depth only.
4. Add tracking or rendering after you can reliably read frames.

That is the whole game.

## The mental model

The Kinect is giving you two useful streams:

- RGB frames from the camera.
- Depth frames from the IR projector / sensor pair.

For toy demos, you usually do not need a sophisticated computer vision stack. A lot of fun stuff can come from:

- finding the nearest blob in the depth frame,
- smoothing its position,
- mapping that to something on your machine,
- and rendering enough debug information that you can tell when your assumptions are wrong.

That last part matters more than people expect.

## The files in this demo kit

- `frame_grabber.hpp`
  - Wraps the C++ freenect device class.
  - Copies RGB and depth frames into local buffers.
  - Exposes `GetLatestRgb()` and `GetLatestDepth()`.
  - Includes a very simple nearest-foreground-blob detector.

- `kinect_cursor.cpp`
  - Smallest possible cursor-control demo.
  - Good when you want to test tracking and air-tap clicking without building a full UI.

- `kinect_cursor_debug.cpp`
  - Better everyday driver while iterating.
  - Opens a debug window with depth, RGB, crosshairs, tracking state, click state, tilt, accelerometer values, and cursor state.

- `kinect_depth_ascii.cpp`
  - Low-friction way to sanity-check depth without touching OpenGL.

- `kinect_pointcloud_snapshot.cpp`
  - Good for making blog assets and exporting something you can inspect in MeshLab or Blender.

## Step 1: get frames

The repo already has the hard part: `wrappers/cpp/libfreenect.hpp`.

The pattern is:

1. Subclass `Freenect::FreenectDevice`.
2. Override `VideoCallback()` and `DepthCallback()`.
3. Copy frame data into your own buffers.
4. Start video and/or depth streams.

That is what [frame_grabber.hpp](fleet-file://vmc1r0s49o083s1oc4g4/Users/suobset/Documents/libfreenect/wrappers/cpp/blog_demos/frame_grabber.hpp?type=file&root=%252F) does.

## Step 2: pick a representation

Raw depth is not directly convenient. For quick hacks, I use one of three representations:

- Raw `uint16_t` depth values for logic.
- False-color RGB depth for visual debugging.
- A collapsed scalar like “nearest blob center” for control.

If your demo is misbehaving, the fastest fix is usually to render more intermediate state, not to guess harder.

## Step 3: find the thing you care about

In this demo kit, the blob detector is intentionally dumb:

1. Estimate a near-depth threshold from the current frame.
2. Keep only pixels in a shallow depth band near that threshold.
3. Compute a weighted centroid.

This works because blog demos do not need to survive every room, every lighting condition, and every second person walking through frame.

The tradeoff is obvious:

- It is pleasantly small.
- It is not robust hand tracking.

That is fine as long as the demo is honest about it.

## Step 4: map it to the outside world

Cursor control is just coordinate mapping plus smoothing.

- Normalize the blob center into `[0, 1]`.
- Scale by the current screen size.
- Smooth with a lerp so the pointer stops looking possessed.
- Use CoreGraphics on macOS to move the cursor.

Clicking is the harder part. A true “tap fingers together” pinch detector is not a great fit for raw Kinect depth alone without doing more segmentation work. So these demos use an `air tap` instead:

- keep a slow-moving baseline of the hand depth,
- watch for a quick forward jab toward the sensor,
- fire a click when that jab crosses a threshold,
- require the hand to pull back before re-arming.

It feels closer to a mid-air button press than to a literal pinch, which is honestly a better match for this hardware.

That logic lives in [kinect_cursor.cpp](fleet-file://vmc1r0s49o083s1oc4g4/Users/suobset/Documents/libfreenect/wrappers/cpp/blog_demos/kinect_cursor.cpp?type=file&root=%252F) and [kinect_cursor_debug.cpp](fleet-file://vmc1r0s49o083s1oc4g4/Users/suobset/Documents/libfreenect/wrappers/cpp/blog_demos/kinect_cursor_debug.cpp?type=file&root=%252F).

On macOS, remember the boring but important part:

- the process launching the app may need Accessibility permission before the system cursor will move.

## Step 5: always have a debug view

This is the main lesson.

The cursor demo by itself is cute. The debug window is what makes it buildable.

When the debug window is open, you can answer:

- Is the Kinect stream alive?
- Is the depth image sensible?
- Is the blob detector locking onto the wrong object?
- Is the tilt angle making the active region weird?
- Is the cursor code wrong, or is the tracking wrong?

Without that window, all bad behavior looks the same.

## Building just these demos

From the repo root:

```bash
cmake -S wrappers/cpp/blog_demos -B build-blog-demos \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build-blog-demos -j
```

This keeps everything local to `build-blog-demos/` and does not install or overwrite anything.

## Running

```bash
./build-blog-demos/kinect_cursor
./build-blog-demos/kinect_cursor_debug
./build-blog-demos/kinect_depth_ascii
./build-blog-demos/kinect_pointcloud_snapshot hand_scan.ply
```

## Ideas for next steps

- Add a calibration mode that maps a smaller physical volume to the full screen.
- Add “zones” on the screen so you can use the Kinect like a giant physical hotkey board.
- Save RGB/depth snapshots automatically whenever tracking quality looks especially good.

## If I were doing this from scratch again

I would do it in this order:

1. Depth ASCII.
2. Cursor with no window.
3. Cursor with a debug window.
4. Point cloud export.
5. Only then start trying more ambitious interaction ideas.

Because once you skip the instrumentation step, you end up writing a lot of folklore and calling it debugging.
