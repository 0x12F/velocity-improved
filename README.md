## Information

Project has not been fully updated yet. There may be some crashes, and some features may not work properly. Please be patient while I complete the update.

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

Try to keep changes focused and easy to review. Match the existing style, use
clear names, avoid unnecessary rewrites, and prefer small practical improvements
over large unrelated changes.

## Why

I was bored.
