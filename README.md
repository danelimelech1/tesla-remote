# Tesla Remote

A TeamViewer-like screen sharing application written in C++, featuring a Tesla-inspired dark UI.

![License](https://img.shields.io/badge/license-MIT-red) ![C++](https://img.shields.io/badge/C%2B%2B-20-red) ![Platform](https://img.shields.io/badge/platform-Windows-red)

---

## Overview

Tesla Remote consists of two executables:

| Executable | Role |
|---|---|
| **TeslaServer.exe** | Runs on **your** machine — receives and displays the remote screen |
| **TeslaClient.exe** | Runs on the **remote** machine — captures and streams the screen |

The Client connects to the Server over a direct TCP connection. The Server listens on port **7788** and displays incoming frames in real time.

---

## Features

- Real-time screen capture and streaming at up to 60 FPS
- JPEG compression with adjustable quality (20–100%) and FPS limit, changeable live
- Tesla-themed dark UI — deep black background, red accents, Segoe UI font
- Animated spinner while waiting for a connection
- Live stats panel: FPS history graph, bandwidth (KB/s), frame count, resolution
- TCP_NODELAY for minimal latency
- Multithreaded: separate network and render threads with lock-free frame handoff

---

## Tech Stack

| Component | Library |
|---|---|
| Networking | [Boost.Asio](https://www.boost.org/doc/libs/release/libs/asio/) 1.91 |
| Windowing | [GLFW](https://www.glfw.org/) 3.4 |
| UI | [Dear ImGui](https://github.com/ocornut/imgui) 1.92 |
| Rendering | OpenGL 3.0 |
| Screen capture | Windows GDI `BitBlt` |
| JPEG encode | [stb_image_write](https://github.com/nothings/stb) |
| JPEG decode | [stb_image](https://github.com/nothings/stb) |
| Package manager | [vcpkg](https://vcpkg.io/) |
| Build system | CMake 3.20+ / Visual Studio 2022 |

---

## Requirements

- Windows 10 or 11 (x64)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++** workload
- [CMake 3.20+](https://cmake.org/download/)
- [Git](https://git-scm.com/)
- Internet access (vcpkg downloads dependencies automatically)

---

## Building

```powershell
# 1. Clone the repo
git clone https://github.com/danelimelech1/tesla-remote.git
cd tesla-remote

# 2. Install vcpkg (skip if already installed)
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\vcpkg"

# 3. Build
.\build.ps1
```

The script configures CMake, downloads all dependencies via vcpkg, and builds both executables in Release mode.

**Output:**
```
build\server\Release\TeslaServer.exe
build\client\Release\TeslaClient.exe
```

---

## Usage

### 1 — Start the Server (your machine)
```
build\server\Release\TeslaServer.exe
```
The viewer window opens and waits for a connection. Note your local IP address.

### 2 — Distribute the Client
Copy the entire `build\client\Release\` folder (EXE + DLLs) to the remote machine.

### 3 — Connect
The remote user launches `TeslaClient.exe`, enters your IP address and clicks **CONNECT**.

### 4 — View
Your Server window shows the remote screen. The side panel displays:
- Live FPS graph
- Bandwidth usage
- Remote resolution
- Total frames received

Both sides can adjust quality and FPS in real time. Click **End Session** to disconnect.

---

## Project Structure

```
tesla-remote/
├── build.ps1               # One-shot build script
├── CMakeLists.txt          # Root CMake config
├── vcpkg.json              # Dependency manifest
├── common/
│   ├── CMakeLists.txt
│   └── protocol.hpp        # Binary packet protocol (21-byte header)
├── client/
│   ├── CMakeLists.txt
│   └── main.cpp            # Screen capture + TCP send + GLFW/ImGui UI
└── server/
    ├── CMakeLists.txt
    └── main.cpp            # TCP receive + OpenGL texture + GLFW/ImGui UI
```

### Protocol

Every frame is preceded by a 21-byte packed header:

```cpp
struct PacketHeader {
    uint32_t magic;         // 0x54564552 ("TVER")
    uint8_t  type;          // PacketType enum (0x02 = FrameData)
    uint32_t payload_size;  // JPEG byte count
    uint32_t frame_width;   // pixels
    uint32_t frame_height;  // pixels
    uint32_t frame_number;  // monotonic counter
};
```

---

## License

MIT — see [LICENSE](LICENSE) for details.
