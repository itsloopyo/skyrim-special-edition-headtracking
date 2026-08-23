# Third-Party Notices

SkyrimSEHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

No Skyrim Special Edition code, no extracted game assets and no game data files
are contained in this repository, and none are redistributed in any release ZIP.
The one piece of game-derived material here is the README demo clip, which is
recorded gameplay footage; it is described in full under "Skyrim Special Edition
footage and screenshots" below.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.2 | MIT | Bundled verbatim in the installer ZIP |
| MinHook | v1.3.4 (`c3fcafd`), modified | BSD-2-Clause | Compiled into `SkyrimSEHeadTracking.asi` |
| inih | r55, modified | BSD-3-Clause | Compiled into `SkyrimSEHeadTracking.asi` |
| cameraunlock-core | 3465659888b2270addac9de0b2a728f59a00360c | MIT | Compiled into `SkyrimSEHeadTracking.asi` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |
| CommonLibSSE-NG | n/a | GPL-3.0-or-later | Neither bundled nor linked; credited as a reverse-engineering cross-reference |

---

## Ultimate ASI Loader

Vendored at `vendor/ultimate-asi-loader/`, shipped in the installer ZIP and used as the
install-time source. Taken from the upstream release asset untouched; the
upstream licence file ships beside it at `vendor/ultimate-asi-loader/LICENSE`.

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Version: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- SHA-256: `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## MinHook

Source committed at `extern/minhook/` and compiled into `SkyrimSEHeadTracking.asi`. The
committed tree is the authoritative record of exactly what is built.

- Upstream: https://github.com/TsudaKageyu/minhook
- Version: `v1.3.4`
- Commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them.

This copy is modified: `MH_Initialize` uses `GetProcessHeap()` rather than
standing up a private heap with `HeapCreate`, and `MH_Uninitialize` skips the
matching `HeapDestroy`. BSD-2-Clause permits the change; it is recorded here so
the attribution is not mistaken for a claim of an unmodified copy.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## inih

Source at `extern/ini.c` and `extern/ini.h`, compiled into
`SkyrimSEHeadTracking.asi`. The committed files are the authoritative record of
exactly what is built; the upstream licence also ships beside them at
`extern/LICENSE.inih`.

- Upstream: https://github.com/benhoyt/inih
- Version: `r55`

This copy is modified: the `INI_API` visibility macros and the `INI_API`
qualifiers on the four public `ini_parse*` declarations are removed, since the
parser is compiled straight into the `.asi` and never exported from a shared
library. A handful of upstream comments are also worded differently. The
BSD-3-Clause licence permits the change; it is recorded here so the attribution
is not mistaken for a claim of an unmodified copy. Ben Hoyt's copyright notice,
conditions and disclaimer are reproduced verbatim below and are retained at the
top of both source files.

```
inih -- simple .INI file parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2009-2024, Ben Hoyt

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Ben Hoyt nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY BEN HOYT ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL BEN HOYT BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `SkyrimSEHeadTracking.asi`.
This is our own shared code, but it is MIT under a different copyright holder
(CameraUnlock) than this mod's own `LICENSE` (itsloopyo), so that licence does
not cover it and its notice has to travel with the binary in its own right. It
ships as `licenses/cameraunlock-core-LICENSE.txt` in both release ZIPs and is
reproduced here as well.

- Pinned commit: `3465659888b2270addac9de0b2a728f59a00360c`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## CommonLibSSE-NG

Not bundled, not linked, and no part of it is compiled into
`SkyrimSEHeadTracking.asi`. It is credited because it is the public
reverse-engineering reference this mod's authors cross-checked their own
findings against.

- Upstream: https://github.com/alandtse/CommonLibVR
- Licence: GPL-3.0-or-later, with a Modding Exception and a GPL-3.0 Linking
  Exception (the project was MIT until it relicensed; the MIT file kept in its
  `licenses/` directory is historical and no longer governs it)

What this mod took from it is a short list of numbers: structure field offsets
such as `NiAVObject::WorldTransform`, and the Address Library relocation IDs
naming two functions. Those are factual measurements of a third-party binary,
independently confirmed at runtime against a legitimately owned copy of the game
before being relied on, and they are recorded in this repository as bare
integer constants inside this project's own namespaces. No CommonLibSSE-NG
source, headers, type definitions, macros or build output are copied, adapted,
included or distributed here, and this mod does not build against it.

Because nothing of its expression is used, its copyleft terms attach nothing to
this project, and this notice is an acknowledgement rather than a licence
obligation. The credit is owed regardless: the offsets were far quicker to
confirm because that project had already published its own findings.

---

## SKSE Address Library

Not bundled, not linked, and not required at runtime by this mod. The Address
Library database (by meh321) is referenced in `src/core/sse_addresses.cpp` only
as one of the two ways a maintainer can look up the RVAs for a newly shipped
game build, the other being independent analysis in Ghidra. This mod ships the
resulting integers in its own build table and reads no Address Library file.

---

## Skyrim Special Edition footage and screenshots

- **Files:** `assets/readme-clip.gif`
- **Rights holder:** Bethesda Game Studios (developer) and Bethesda Softworks
  (publisher), together with the rights holders of any third-party marks
  visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a
  legitimately purchased copy, shown so a reader can see what the mod does
  before installing it.
- **Bundled:** `assets/readme-clip.gif`: kept in this repository only. The packaging scripts
  ship no part of `assets/`, so these are in neither release ZIP nor
  anything the launcher deploys.
- **Licence:** none is granted or implied by this repository. This material is
  not covered by the MIT licence in `LICENSE`, and nothing here permits reuse
  of it. Rights holders who would rather it were not published: open an issue
  or reach us on Discord and it comes down.

---

## Skyrim Special Edition

Skyrim Special Edition and all related names, logos, characters and marks are
trademarks of Bethesda Softworks LLC, ZeniMax Media Inc., and their respective
owners. They are used here only to identify the
game this mod applies to, which is nominative use and not a claim of any right
in them. This project is an unofficial, fan-made modification. It is not
affiliated with, endorsed by, or sponsored by the game's developers, its
publishers, its engine vendor, or any other rights holder. It redistributes no
game code, no game assets and no proprietary DLLs, and it requires a
legitimately purchased copy of the game. Any engine structure offsets,
function addresses or byte patterns referenced in the source were derived by
the authors through independent analysis of a legitimately owned copy. They
are factual measurements recorded as numbers; no decompiled or disassembled
game code is stored in this repository.
