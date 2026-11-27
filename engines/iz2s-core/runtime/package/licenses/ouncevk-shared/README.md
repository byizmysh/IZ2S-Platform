<!-- SPDX-License-Identifier: MIT -->
# OunceVK for Nintendo Switch 2

OunceVK is NVK, Mesa's Vulkan driver for NVIDIA GPUs, for the Nintendo
Switch 2. It adds a Switch 2 backend to NVK. On top of it, Zink gives OpenGL
ES (3.1 at the time of writing) and OpenGL, with EGL on the default
`NWindow`. Homebrew programs
link the driver statically and run on libounce, a libnx-compatible library
for Switch 2.

## What you get

- Vulkan 1.4 through `VK_NN_vi_surface` on the default `NWindow`
  (`nwindowGetDefault`), as on Switch 1.
- OpenGL and OpenGL ES through Zink, with EGL (`eglGetDisplay(EGL_DEFAULT_DISPLAY)`
  and `eglCreateWindowSurface` with the `NWindow`).
- Static archives that need only libounce to link.

## Building

Everything builds in a container. The commands run from the repository root
and use `docker`; `podman` works the same, with `podman` in place of
`docker`. `LIBOUNCE` is the `nx/` directory of a libounce build made with
`SWITCH2=1`.

1. Build the container image:

   ```sh
   docker build -t ouncevk switch2/toolchain
   ```

2. Build the Rust standard library for the target and the host tools Mesa
   needs (`mesa_clc`, `vtn_bindgen2`):

   ```sh
   docker run --rm -v "$PWD:/work" -w /work ouncevk \
       bash switch2/toolchain/rust/build-sysroot.sh
   docker run --rm -v "$PWD:/work" -w /work -e SRC=/work ouncevk \
       bash switch2/toolchain/build-host-tools.sh
   ```

3. Configure and build. For Vulkan only:

   ```sh
   docker run --rm -v "$PWD:/work" -v "$LIBOUNCE:/ounce-nx:ro" -w /work \
       -e SRC=/work -e NVK_LIBOUNCE_NX=/ounce-nx ouncevk \
       bash switch2/build/configure-native-headless.sh
   ```

   The archives are in `build/native-headless`. For Vulkan with OpenGL, run
   `switch2/build/configure-gl.sh` the same way, then `ninja -C
   build/native-gl` with `build/host-tools/bin` first in `PATH`.

## Packaging

`switch2/build/package-portlib.sh` makes the tree for
`$DEVKITPRO/portlibs/switch2` from the two builds. Run it in the container
after both builds:

```sh
docker run --rm -v "$PWD:/work" -v "$OUT:/out" -w /work ouncevk \
    sh switch2/build/package-portlib.sh /out/portlibs/switch2
```

The tree has these libraries:

- `libvulkan.a`: Vulkan. Link `-lvulkan`.
- `libEGL.a`: EGL, OpenGL and OpenGL ES, and also Vulkan. Link `-lEGL`.
- Empty `libGLESv2.a`, `libglapi.a` and `libdrm_nouveau.a`. They hide the
  Switch 1 libraries of the same name, so a Switch 1 Makefile that links
  them still works.

Each library is one relocatable object. Only the API symbols are global:
`vk*`, `egl*`, `gl*` and the libounce exit hook. Mesa's other symbols are
local, so they cannot collide with the symbols of a program.

### Shared libraries

When `OUNCE_NX` names a libounce with `libounce.so` (libounce's package has
it), the tree also has `lib/shared`, for dynamically linked programs:

- `libouncevk.so.1`: Vulkan, the driver.
- `libouncevk-gl.so.1`: EGL, OpenGL and OpenGL ES. It has no driver of its
  own and needs `libouncevk.so.1`, so a process has one driver whichever
  of the two its modules use.
- `libvulkan.so`, `libEGL.so`, `libGLESv2.so` and `libGL.so`: linker scripts,
  so that `-lvulkan`, `-lEGL`, `-lGLESv2` and `-lGL` link the two libraries
  above.
- `libvulkan.so.1`, `libEGL.so.1`, `libGLESv2.so.2` and `libGL.so.1`: the
  names that code written for Linux loads with `dlopen`. Each is a library
  with no code that needs the real one, so `dlsym` on its handle finds the
  functions.
- `nro/`: all of those as NROs.

A program links them as on Linux, with libounce's flags for a dynamically
linked program, which put `lib/shared` before `lib` in the library path:

```make
ARCH    := $(SWITCH2_SHARED_ARCH)
LDFLAGS := -specs=$(SWITCH2_SHARED_SPECS) $(ARCH)
OVERLAY_LIBPATHS := $(SWITCH2_SHARED_LIBPATHS) $(OVERLAY_LIBPATHS)
LIBS    := -lvulkan        # or: -lEGL -lGLESv2
```

A CMake project gets the same with `-DOUNCE_SHARED=ON` on libounce's
toolchain (`aarch64-none-elf-cmake`), and a Meson or autotools project with
`OUNCE_SHARED=1` exported before `switch2vars.sh` is sourced.
`aarch64-none-elf-pkg-config` then answers from `lib/shared/pkgconfig`, so
`vulkan`, `egl`, `glesv2` and `gl` name the shared libraries.

The NROs that the program needs go on the SD card with libounce's
`libounce.so.1` and `libstdc++.so.6`: in the program's folder, or in
`sdmc:/kaihen/lib` for all programs. A program that loads a library by a
Linux name needs that file there too.

A module links either the static or the shared libraries, not both: the
static `libEGL.a` holds a driver of its own.

Mesa writes its log messages to `stderr`, as on other systems. To keep
them, redirect `stderr` to a file on the SD card, for example with
`freopen("sdmc:/log.txt", "w", stderr)`. `MESA_LOG_LEVEL` and the other
Mesa log options work through `setenv` before the first Vulkan or EGL call. A program links libounce (`-lnx` with libounce's `switch2_rules`)
after these libraries.

Horizon has no Vulkan loader. The driver defines `vkCreateInstance` and the
other commands of Vulkan 1.0 to 1.4, `VK_KHR_surface`, `VK_KHR_swapchain` and
`VK_NN_vi_surface`. Get the commands of other extensions with
`vkGetInstanceProcAddr` or `vkGetDeviceProcAddr`.

The driver opens the GPU when a program creates its first Vulkan instance
or EGL display. libounce's `__appExit` calls the driver's exit hook, which
closes the GPU even when the program leaked Vulkan or EGL objects.
