// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <kodi/AddonBase.h>
#define private public
#include "InputStreamAirPlay.h"
#undef private
#include "InputStreamAirPlay.cpp"

#include <cassert>
#include <thread>

using namespace std::chrono_literals;

struct Session
{
  CInputStreamAirPlay input{kodi::addon::IInstanceInfo{}};
  int peer{-1};
  Session()
  {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    input.m_fd = sockets[0];
    peer = sockets[1];
  }
  ~Session() { close(peer); }
  void Send(const void* data, size_t size) { assert(send(peer, data, size, 0) == ssize_t(size)); }
};

int main()
{
  {
    Session s;
    apx_hdr header{};
    assert(!s.input.ReadExact(&header, sizeof(header), 20, true));
    assert(!s.input.m_eof); // An idle socket is safe to retry.
    s.Send(&header, 3);
    assert(!s.input.ReadExact(&header, sizeof(header), 20, true));
    assert(s.input.m_eof); // A partial header must never be retried as a new one.
  }
  {
    Session s;
    char data[5]{};
    std::thread sender(
        [&]
        {
          for (int i = 0; i < 5; ++i)
          {
            s.Send("x", 1);
            std::this_thread::sleep_for(30ms);
          }
        });
    const auto start = Clock::now();
    assert(!s.input.ReadExact(data, sizeof(data), 70));
    assert(Clock::now() - start < 140ms);
    assert(s.input.m_eof);
    sender.join();
  }
  {
    Session s;
    s.Send("x", 1);
    assert(!s.input.SkipPayload(2, 20));
    assert(s.input.m_eof);
  }
  {
    apx_streaminfo info{};
    assert(!CInputStreamAirPlay::ValidateStreamInfo(info));
    info.video_codec = APX_VCODEC_H264;
    info.video_extradata_size = APX_MAX_VIDEO_EXTRADATA;
    info.audio_extradata_size = APX_MAX_EXTRADATA;
    assert(CInputStreamAirPlay::ValidateStreamInfo(info));
    ++info.video_extradata_size;
    assert(!CInputStreamAirPlay::ValidateStreamInfo(info));
    info.video_extradata_size = 0;
    ++info.audio_extradata_size;
    assert(!CInputStreamAirPlay::ValidateStreamInfo(info));
    info.audio_extradata_size = 0;
    info.audio_ct = 123;
    assert(!CInputStreamAirPlay::ValidateStreamInfo(info));
    info.audio_ct = APX_ACT_PCM;
    info.video_codec = 123;
    assert(!CInputStreamAirPlay::ValidateStreamInfo(info));
  }
  {
    Session s;
    s.input.m_info.video_codec = APX_VCODEC_H264;
    s.input.m_info.width = 1920;
    apx_hdr hdr{APX_MAGIC, APX_MSG_STREAMINFO, sizeof(apx_streaminfo), 0, 0};
    apx_streaminfo replacement{};
    replacement.video_codec = APX_VCODEC_HEVC;
    s.Send(&hdr, sizeof(hdr));
    s.Send(&replacement, 8);
    shutdown(s.peer, SHUT_WR);
    auto* packet = s.input.DemuxRead();
    assert(packet);
    s.input.FreeDemuxPacket(packet);
    assert(s.input.m_info.width == 1920);
    assert(s.input.m_info.video_codec == APX_VCODEC_H264);
  }
  {
    Session s;
    apx_hdr hdr{APX_MAGIC, APX_MSG_STREAMINFO, sizeof(apx_streaminfo), 0, 0};
    apx_streaminfo info{};
    info.audio_ct = APX_ACT_PCM;
    s.Send(&hdr, sizeof(hdr));
    s.Send(&info, sizeof(info));
    kodi::addon::failAllocation = true;
    assert(s.input.DemuxRead() == nullptr);
    kodi::addon::failAllocation = false;
    assert(!s.input.m_hasAudio); // Allocation failure cannot publish half a change.
  }
  {
    Session s;
    s.input.m_positionMs = 100;
    s.input.m_durationMs = 200;
    s.input.m_positionAnchorMs = 300;
    s.input.m_lastAudioMs = 400;
    s.input.m_packetsRead = 5;
    s.input.Close();
    assert(s.input.GetTime() == 0 && s.input.GetTotalTime() == 0);
    assert(s.input.m_lastAudioMs == 0 && s.input.m_packetsRead == 0);
  }
  {
    Session s;
    s.input.Close();
    s.input.m_socketPath = "/tmp/airplay-absent-" + std::to_string(getpid());
    auto* packet = s.input.DemuxRead();
    assert(packet && s.input.m_fd == -1 && s.input.m_reconnectAttempts == 1);
    s.input.FreeDemuxPacket(packet);
    s.input.m_nextReconnect = {};
    packet = s.input.DemuxRead();
    assert(packet && s.input.m_reconnectAttempts == 2);
    s.input.FreeDemuxPacket(packet);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", s.input.m_socketPath.c_str());
    assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(listen(server, 1) == 0);
    s.input.m_nextReconnect = {};
    packet = s.input.DemuxRead();
    assert(packet && s.input.m_fd >= 0 && s.input.m_reconnectAttempts == 0);
    s.input.FreeDemuxPacket(packet);
    close(server);
    unlink(address.sun_path);
    s.input.DemuxAbort();
    assert(s.input.DemuxRead() == nullptr);
  }
  {
    Session s;
    s.input.Close();
    s.input.m_socketPath = std::string(300, 'x');
    assert(!s.input.Connect() && errno == ENAMETOOLONG);
    s.input.m_reconnectAttempts = 50;
    assert(s.input.DemuxRead() == nullptr);
  }
  std::puts("All demuxer regression tests passed");
}
