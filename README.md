## Requirements

- Visual Studio 2022 or Newer
- The **Desktop development with C++** workload
- The **C++ Clang tools for Windows** and **vcpkg** components
- An x64 processor with AVX2 support
- Internet access for the first dependency restore

FreeType is declared in `vcpkg.json` and is restored automatically by Visual Studio/MSBuild.

## Build

Open a **Developer PowerShell for Visual Studio** at the repository root and run:

```powershell
msbuild cs2\velocity-cs2\velocity-cs2.vcxproj /m /p:Configuration=Ship /p:Platform=x64
```

The Ship build is written to `cs2\bin\cs2.dll`. Use `Configuration=Development` to produce `cs2\bin\velocity-cs2-dev.dll` with development diagnostics.

If you use a standalone vcpkg installation instead of Visual Studio's bundled copy, set `VCPKG_ROOT` to its directory before building.


this is not a seperate project.
