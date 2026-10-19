#!/usr/bin/env python3
"""Run the real demuxer with a minimal Kodi host shim and Unix sockets.

The normal build matrix separately checks compatibility with the Kodi SDK.
Run on Linux/macOS: python3 tests/run_regressions.py
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SHIM = r'''
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#define ATTR_DLL_LOCAL
#define ADDONCREATOR(x)
constexpr int ADDON_LOG_ERROR=1, ADDON_LOG_INFO=2;
constexpr int INPUTSTREAM_SUPPORTS_IDEMUX=1, INPUTSTREAM_SUPPORTS_IDISPLAYTIME=2;
constexpr int INPUTSTREAM_TYPE_VIDEO=1, INPUTSTREAM_TYPE_AUDIO=2;
constexpr int DEMUX_SPECIALID_STREAMCHANGE=-11, ADDON_INSTANCE_INPUTSTREAM=1;
constexpr double STREAM_NOPTS_VALUE=-1e300;
using KODI_ADDON_INSTANCE_HDL=void*;
enum ADDON_STATUS { ADDON_STATUS_OK, ADDON_STATUS_NOT_IMPLEMENTED };
struct DEMUX_PACKET {
  uint8_t* pData{}; int iSize{}, iStreamId{}; double pts{}, dts{}, duration{};
};
namespace kodi {
template<class... T> void Log(int, const char*, T...) {}
namespace vfs { inline std::string TranslateSpecialProtocol(const char* s) { return s; } }
namespace addon {
inline bool failAllocation=false;
inline bool GetSettingBoolean(const char*, bool fallback) { return fallback; }
struct IInstanceInfo { bool IsType(int) const { return true; } };
struct InputstreamProperty {};
struct InputstreamCapabilities { void SetMask(uint32_t) {} };
struct InputstreamInfo {
  template<class T> void SetStreamType(T) {} template<class T> void SetFeatures(T) {}
  template<class T> void SetFlags(T) {} template<class T> void SetCodecName(T) {}
  template<class T> void SetPhysicalIndex(T) {} template<class T> void SetWidth(T) {}
  template<class T> void SetHeight(T) {} template<class T> void SetFpsScale(T) {}
  template<class T> void SetFpsRate(T) {} template<class T> void SetSampleRate(T) {}
  template<class T> void SetChannels(T) {} template<class T> void SetBitsPerSample(T) {}
  void SetExtraData(const uint8_t*, unsigned int) {}
};
class CInstanceInputStream {
public:
  explicit CInstanceInputStream(const IInstanceInfo&) {}
  DEMUX_PACKET* AllocateDemuxPacket(int size) {
    if (failAllocation) return nullptr;
    auto* p = new DEMUX_PACKET; p->pData = new uint8_t[size]; return p;
  }
  void FreeDemuxPacket(DEMUX_PACKET* p) { if (p) { delete[] p->pData; delete p; } }
};
class CAddonBase {};
}}
'''

with tempfile.TemporaryDirectory(prefix="airplay-tests-") as temporary:
    work = Path(temporary)
    (work / "kodi/addon-instance").mkdir(parents=True)
    (work / "kodi/AddonBase.h").write_text(SHIM)
    for header in ("Filesystem.h", "addon-instance/Inputstream.h"):
        (work / "kodi" / header).write_text('#include <kodi/AddonBase.h>\n')
    executable = work / "regressions"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-pthread", "-Doverride=",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(work), "-I", str(ROOT / "src/addon"),
        str(ROOT / "tests/regressions.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
