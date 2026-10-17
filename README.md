## Requirements

- Visual Studio 2022 or newer
- Desktop development with C++ workload
- C++ Clang tools for Windows
- vcpkg component for Visual Studio
- x64 CPU with AVX2 support
- Internet access for the first dependency restore

FreeType is declared in `cs2/velocity-cs2/vcpkg.json` and is restored
automatically by Visual Studio/MSBuild.

## Build Guide

Open a Developer PowerShell for Visual Studio at the repository root.

Build the Ship configuration:

```powershell
msbuild cs2\velocity-cs2\velocity-cs2.vcxproj /m /p:Configuration=Ship /p:Platform=x64
```

The Ship build output is written to:

```text
cs2\bin\cs2.dll
```

Build the Development configuration:

```powershell
msbuild cs2\velocity-cs2\velocity-cs2.vcxproj /m /p:Configuration=Development /p:Platform=x64
```

The Development build output is written to:

```text
cs2\bin\velocity-cs2-dev.dll
```

If you use a standalone vcpkg installation instead of Visual Studio's bundled
copy, set `VCPKG_ROOT` to that installation directory before building.

## Contributing

Contributions are welcome. If you want to help, open a pull request with a clear
description of what changed and why.

Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
for first-party code. Use `.cc` source files, `.h` headers, lowercase filenames
with underscores, and path-based `VELOCITY_..._H_` header guards. Include project
headers using quoted paths relative to `project/`.
Both build configurations target C++20.

Bundled code under `project/external/` retains its upstream style and filenames.
Windows ABI declarations, engine-facing names in strings, and third-party API
names must retain their required spelling. The existing precompiled-header
build requires `pch/pch.h` first in translation units that use it. Keep these
platform integration constraints when making style changes.

## Why

I was bored.

![image](media/menu.jpg)
