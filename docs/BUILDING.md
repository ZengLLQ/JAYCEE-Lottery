# Build JAYCEE Lottery from Source

## Requirements

- 64-bit Windows 10 or Windows 11
- CMake 3.20 or newer
- One supported C++ toolchain:
  - MinGW-w64 with `g++`, or
  - Visual Studio 2022 with the Desktop development with C++ workload
- PowerShell 5.1 or newer

The project uses only Windows system APIs and does not download third-party source dependencies.

## Automated release build

Open PowerShell in the project directory and run:

```powershell
./build-release.ps1
```

The script:

1. detects MinGW-w64 or Visual Studio;
2. configures and builds the application with CMake;
3. embeds the current executable, README, and CSV template in the custom installer;
4. copies the portable executable and installer to `dist/`; and
5. creates the portable ZIP package.

Expected output:

```text
dist/
├── JAYCEE Lottery.exe
├── JAYCEE-Lottery-Setup.exe
├── JAYCEE-Lottery-portable.zip
├── participants-template.csv
└── README.md
```

## Manual CMake build

### MinGW-w64

```powershell
cmake -S . -B build-v2 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-v2 --parallel
```

### Visual Studio

```powershell
cmake -S . -B build-v2 -A x64
cmake --build build-v2 --config Release --parallel
```

The application and installer are written to `build-v2/bin` or `build-v2/bin/Release`, depending on the generator.

## Runtime portability

Release configuration uses a static C/C++ runtime:

- MSVC uses the multithreaded static runtime (`/MT`).
- MinGW links `libgcc` and `libstdc++` statically.

The executable still uses standard Windows libraries such as Direct2D, DirectWrite, DWM, Shell, and common dialogs. These are included with supported Windows versions. No interpreter or separate compiler runtime package is required on the destination computer.

## Project structure

```text
assets/                 CSV template embedded in releases
docs/                   User, CSV, and build documentation
installer/              Native custom installer source and resources
resources/              Application icon, manifest, and resource script
src/main.cpp             Win32/Direct2D application source
build-release.ps1        Release packaging script
CMakeLists.txt           Application and installer build definition
dist/                    Ready-to-distribute release files
```

## Clean rebuild

The `build/` and `build-v2/` directories contain generated files and are ignored by Git. To force a clean configuration, remove the specific build directory, recreate it, and run the release script again. Do not remove `dist/` unless you also intend to regenerate the distributable files.

## Versioning a release

Before publishing a new version:

1. update the version in `CMakeLists.txt`;
2. update `CHANGELOG.md`;
3. run `build-release.ps1`;
4. test the portable executable and installer on Windows; and
5. attach the files from `dist/` to a GitHub Release.
