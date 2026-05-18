# Third-Party Notices

This project includes or depends on the following third-party software.

---

## Ultimate ASI Loader

- **Version:** v9.7.1 (commit 2155f2177733d673a3eb783141ceedd564a0a0e2)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads the mod's .asi plugin into Skyrim SE via the dinput8.dll proxy.
- **Bundled:** yes. Bundled in release ZIP as fallback; fetched latest within range at install time.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** bundled source copy (extern/minhook)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** API hooking for intercepting Skyrim's camera and input functions at runtime.
- **Bundled:** yes. Compiled directly into the ASI module.

Copyright (C) 2009-2017 Tsuda Kageyu. All rights reserved.

---

## inih

- **Version:** bundled source copy (extern/ini.c, extern/ini.h)
- **License:** BSD-3-Clause
- **Upstream:** https://github.com/benhoyt/inih
- **Usage:** INI file parsing for HeadTracking.ini configuration.
- **Bundled:** yes. Compiled directly into the ASI module.

Copyright (c) 2009-2024 Ben Hoyt. All rights reserved.

---

## OpenTrack

- **Version:** n/a (UDP wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** Head tracking data is received over the OpenTrack UDP protocol on port 4242. No OpenTrack code is bundled.
- **Bundled:** no. Runtime data source; users install OpenTrack separately.
