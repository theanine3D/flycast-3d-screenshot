This is a fork of Flycast that adds a "3D Screenshot" feature, allowing users to capture 3D geometry currently being rendered on the screen, along with any textures.

To use this feature, you must assign a hotkey to the "Save 3D Screenshot" keymap in your controller mapping settings. I suggest "Ctrl + F12", but you can choose anything.
- <img width="632" height="230" alt="image" src="https://github.com/user-attachments/assets/36eb2660-bf03-45a7-99f3-1eed91366418" />

Captures are saved in .gltf format, to a "3D" subfolder inside the "Screenshots" folder where Flycast saves screenshots by default. On Windows, that folder is:
`%USERPROFILE%\Pictures\Screenshots\3D\`

## Previews
<img width="854" height="459" alt="image" src="https://github.com/user-attachments/assets/a0aba179-25ea-4e71-9548-782499950d90" />

- Jet Set Radio capture, imported into Blender


<img width="856" height="453" alt="image" src="https://github.com/user-attachments/assets/8bd287a2-422b-468c-b434-8b97002781e3" />

- Super Magnetic Neo capture, imported into Blender


(Original Flycast readme below:)

---
# Flycast

[![Android CI](https://github.com/flyinghead/flycast/actions/workflows/android.yml/badge.svg)](https://github.com/flyinghead/flycast/actions/workflows/android.yml)
[![C/C++ CI](https://github.com/flyinghead/flycast/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/flyinghead/flycast/actions/workflows/c-cpp.yml)
[![Nintendo Switch CI](https://github.com/flyinghead/flycast/actions/workflows/switch.yml/badge.svg)](https://github.com/flyinghead/flycast/actions/workflows/switch.yml)
[![Windows UWP CI](https://github.com/flyinghead/flycast/actions/workflows/uwp.yml/badge.svg)](https://github.com/flyinghead/flycast/actions/workflows/uwp.yml)
[![BSD CI](https://github.com/flyinghead/flycast/actions/workflows/bsd.yml/badge.svg)](https://github.com/flyinghead/flycast/actions/workflows/bsd.yml)

<img src="shell/linux/flycast.png" alt="flycast logo" width="150"/>

**Flycast** is a multi-platform Sega Dreamcast, Naomi, Naomi 2, and Atomiswave emulator derived from [**reicast**](https://github.com/skmp/reicast-emulator).

Information about configuration and supported features can be found on [**TheArcadeStriker's flycast wiki**](https://github.com/TheArcadeStriker/flycast-wiki/wiki).

Join us on our [**Discord server**](https://discord.gg/X8YWP8w) for a chat.

## Downloads ![android](https://flyinghead.github.io/flycast-builds/android.jpg) ![windows](https://flyinghead.github.io/flycast-builds/windows.png) ![linux](https://flyinghead.github.io/flycast-builds/ubuntu.png) ![apple](https://flyinghead.github.io/flycast-builds/apple.png) ![switch](https://flyinghead.github.io/flycast-builds/switch.png) ![xbox](https://flyinghead.github.io/flycast-builds/xbox.png)

Get builds for your system from the [**builds page**](https://flyinghead.github.io/flycast-builds/) or [**GitHub Releases**](https://github.com/flyinghead/flycast/releases).

- **Latest master builds:** regular builds from the `master` branch with recent fixes and updates.
- **Nightly dev builds:** experimental builds with the latest features and changes.
- **Stable tagged releases:** versioned release builds published on GitHub Releases.

Automated test results are available from the builds page as well.

## Install

### Android ![android](https://flyinghead.github.io/flycast-builds/android.jpg)

Install Flycast from [**Google Play**](https://play.google.com/store/apps/details?id=com.flycast.emulator).

### Flatpak (Linux ![ubuntu logo](https://flyinghead.github.io/flycast-builds/ubuntu.png))

1. [Set up Flatpak](https://www.flatpak.org/setup/).

2. Install Flycast from [Flathub](https://flathub.org/apps/details/org.flycast.Flycast):

`flatpak install -y org.flycast.Flycast`

3. Run Flycast:

`flatpak run org.flycast.Flycast`

### Homebrew (macOS ![apple logo](https://flyinghead.github.io/flycast-builds/apple.png))

1. [Set up Homebrew](https://brew.sh).

2. Install Flycast via Homebrew:

`brew install --cask flycast`

### iOS

Due to persistent harassment from an iOS user, support for this platform has been dropped.

### Xbox One/Series ![xbox logo](https://flyinghead.github.io/flycast-builds/xbox.png)

Grab the latest build from [**the builds page**](https://flyinghead.github.io/flycast-builds/), or the [**GitHub Actions**](https://github.com/flyinghead/flycast/actions/workflows/uwp.yml). Then install it using the **Xbox Device Portal**.

## Build from source

### macOS

Right-click the bootstrap script and choose **Open**:

`shell/apple/generate_xcode_project.command`

### Windows

Double-click the bootstrap script:

`shell\windows\generate_vs_project.bat`

### Linux

#### Dependencies

- **C/C++ compiler toolchain** (e.g. `gcc`/`g++`)
- **CMake**
- **make**
- **libcurl** (development headers)
- **libudev** (development headers)
- **SDL2** (development headers)
- **Graphics API**: Vulkan, OpenGL

#### Build

```
$ git clone --recursive https://github.com/flyinghead/flycast.git
$ cd flycast
$ mkdir build && cd build
$ cmake ..
$ make
```

## Packaging status

[![Packaging status](https://repology.org/badge/vertical-allrepos/flycast.svg)](https://repology.org/project/flycast/versions)
