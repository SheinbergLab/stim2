/*
 * framelog.cpp -- per-swap frame record published to dserv as "stim/frame"
 *
 * See framelog.h for the record layout and the contract.  Two threads:
 *
 *   render thread  frameLogPush(): copies six integers into a fixed ring
 *                  (single producer) and notifies.  No lock, no allocation,
 *                  no I/O.  Ring full -> record dropped and counted.
 *
 *   writer thread  pops records (single consumer), numbers them, packs each
 *                  into dserv's fixed 128-byte '>' datapoint frame and
 *                  write()s it over its own TCP connection to <host>:4620.
 *                  A failed write or an EOF from dserv closes the socket;
 *                  reconnects are attempted no faster than once a second
 *                  and records are dropped (and counted) meanwhile.
 *
 * Wire frame ('>' datapoint, as qpcs::dsSocketSendBytes and dserv's
 * DPOINT_BINARY_MSG_CHAR parser spell it; integers little-endian native):
 *   byte 0        '>'
 *   bytes 1-2     uint16 varlen (10)
 *   bytes 3-12    "stim/frame"
 *   bytes 13-20   uint64 timestamp (0: dserv stamps it on receipt)
 *   bytes 21-24   uint32 datatype (DSERV_INT64 = 16)
 *   bytes 25-28   uint32 datalen (56)
 *   bytes 29-84   7 x int64
 *   bytes 85-127  zero padding
 */

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cerrno>

#ifndef _MSC_VER
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
typedef int fl_socket_t;
#define FL_INVALID_SOCKET (-1)
#define fl_close_socket(s) ::close(s)
#else
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET fl_socket_t;
#define FL_INVALID_SOCKET INVALID_SOCKET
#define fl_close_socket(s) ::closesocket(s)
#endif

#include "framelog.h"

/* ---- render-thread counters (plain; render thread only) -------------- */
long long FrameLogCmdsApplied = 0;
long long FrameLogDsDispatched = 0;

namespace {

/* ---- the record as the render thread hands it over -------------------- */
struct FrameRec {
  long long swapcount;
  long long flip_us;
  long long ticks_us;
  long long missed;
  long long cmds_applied;
  long long ds_dispatched;
};

/* ---- single-producer / single-consumer ring --------------------------- */
constexpr unsigned RING_CAP = 256;            /* power of two */
constexpr unsigned RING_MASK = RING_CAP - 1;

FrameRec ring[RING_CAP];
std::atomic<unsigned> ring_head{0};   /* producer: next slot to write */
std::atomic<unsigned> ring_tail{0};   /* consumer: next slot to read  */

inline bool ring_push(const FrameRec &r)
{
  unsigned h = ring_head.load(std::memory_order_relaxed);
  unsigned t = ring_tail.load(std::memory_order_acquire);
  if (h - t >= RING_CAP) return false;        /* full */
  ring[h & RING_MASK] = r;
  ring_head.store(h + 1, std::memory_order_release);
  return true;
}

inline bool ring_pop(FrameRec &r)
{
  unsigned t = ring_tail.load(std::memory_order_relaxed);
  unsigned h = ring_head.load(std::memory_order_acquire);
  if (t == h) return false;                   /* empty */
  r = ring[t & RING_MASK];
  ring_tail.store(t + 1, std::memory_order_release);
  return true;
}

inline bool ring_empty()
{
  return ring_tail.load(std::memory_order_relaxed) ==
         ring_head.load(std::memory_order_acquire);
}

inline void ring_reset()
{
  ring_head.store(0, std::memory_order_relaxed);
  ring_tail.store(0, std::memory_order_relaxed);
}

/* ---- writer state ----------------------------------------------------- */
std::atomic<bool> enabled{false};     /* writer thread running */
std::atomic<bool> stop_req{false};
std::atomic<bool> connected{false};
std::atomic<long long> sent{0};
std::atomic<long long> dropped{0};

std::mutex wake_mx;                   /* only for the condvar's timed wait */
std::condition_variable wake_cv;

std::thread *writer = nullptr;        /* heap: never destroyed at static
                                         teardown with a joinable thread */
std::string hostspec;                 /* as given to stimFrameLog */
std::string host;                     /* resolved pieces of hostspec */
int port = 4620;
bool atexit_registered = false;

/* management calls (start/stop/status) all run on the render thread, but
   a mutex keeps them safe should that ever change */
std::mutex mgmt_mx;

constexpr const char *VARNAME = "stim/frame";
constexpr uint16_t VARLEN = 10;
constexpr uint32_t DSERV_INT64_TYPE = 16;
constexpr uint32_t NVALS = 7;
constexpr uint32_t DATALEN = NVALS * 8;
constexpr size_t FRAME_BYTES = 128;
constexpr size_t DATA_OFF = 1 + 2 + VARLEN + 8 + 4 + 4;   /* 29 */
static_assert(DATA_OFF + DATALEN <= FRAME_BYTES, "frame overflow");

void pack_header(unsigned char *buf)
{
  memset(buf, 0, FRAME_BYTES);
  buf[0] = '>';
  memcpy(buf + 1, &VARLEN, 2);
  memcpy(buf + 3, VARNAME, VARLEN);
  uint64_t ts = 0;
  memcpy(buf + 13, &ts, 8);
  memcpy(buf + 21, &DSERV_INT64_TYPE, 4);
  memcpy(buf + 25, &DATALEN, 4);
}

/* ---- socket helpers --------------------------------------------------- */
bool set_nonblocking(fl_socket_t s, bool on)
{
#ifndef _MSC_VER
  int fl = fcntl(s, F_GETFL, 0);
  if (fl < 0) return false;
  fl = on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK);
  return fcntl(s, F_SETFL, fl) == 0;
#else
  u_long mode = on ? 1 : 0;
  return ioctlsocket(s, FIONBIO, &mode) == 0;
#endif
}

/* Connect with a bounded wait so a dead host never parks the writer (and
   therefore frameLogStop's join) for the kernel's 75 s default. */
fl_socket_t connect_timeout(const std::string &h, int p, int timeout_ms)
{
  struct addrinfo hints, *res = nullptr;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char pstr[16];
  snprintf(pstr, sizeof(pstr), "%d", p);
  if (getaddrinfo(h.c_str(), pstr, &hints, &res) != 0 || !res)
    return FL_INVALID_SOCKET;

  fl_socket_t s = FL_INVALID_SOCKET;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == FL_INVALID_SOCKET) continue;
    set_nonblocking(s, true);
    int rc = connect(s, ai->ai_addr, (socklen_t) ai->ai_addrlen);
    bool ok = false;
    if (rc == 0) ok = true;
#ifndef _MSC_VER
    else if (errno == EINPROGRESS) {
      struct pollfd pfd = { s, POLLOUT, 0 };
      if (poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLOUT)) {
        int err = 0; socklen_t len = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &err, &len) == 0
            && err == 0)
          ok = true;
      }
    }
#else
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
      fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
      struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
      if (select(0, nullptr, &wf, nullptr, &tv) > 0) {
        int err = 0; int len = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &err, &len) == 0
            && err == 0)
          ok = true;
      }
    }
#endif
    if (ok) break;
    fl_close_socket(s);
    s = FL_INVALID_SOCKET;
  }
  freeaddrinfo(res);
  if (s == FL_INVALID_SOCKET) return s;

  set_nonblocking(s, false);
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
#ifdef SO_NOSIGPIPE
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *) &one, sizeof(one));
#endif
#ifndef _MSC_VER
  struct timeval tv = { 1, 0 };          /* a stalled peer costs <= 1 s/write */
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *) &tv, sizeof(tv));
#else
  DWORD to = 1000;
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *) &to, sizeof(to));
#endif
  return s;
}

/* dserv never writes back on this link, so anything readable means it
   closed (EOF) or is confused: treat both as a dead link.  Catching the FIN
   here, before the next send, avoids losing one record into a socket the
   peer has already shut. */
bool peer_gone(fl_socket_t s)
{
#ifndef _MSC_VER
  struct pollfd pfd = { s, POLLIN, 0 };
  if (poll(&pfd, 1, 0) <= 0) return false;
  if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return true;
  char c;
  ssize_t n = recv(s, &c, 1, MSG_PEEK);
  return n <= 0;
#else
  fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
  struct timeval tv = { 0, 0 };
  if (select(0, &rf, nullptr, nullptr, &tv) <= 0) return false;
  char c;
  return recv(s, &c, 1, MSG_PEEK) <= 0;
#endif
}

bool send_all(fl_socket_t s, const unsigned char *buf, size_t len)
{
  int flags = 0;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  size_t off = 0;
  while (off < len) {
    auto n = send(s, (const char *) buf + off, len - off, flags);
    if (n < 0) {
#ifndef _MSC_VER
      if (errno == EINTR) continue;
#endif
      return false;
    }
    if (n == 0) return false;
    off += (size_t) n;
  }
  return true;
}

/* ---- the writer thread ------------------------------------------------ */
void writer_main(std::string h, int p)
{
  using clock = std::chrono::steady_clock;
  unsigned char frame[FRAME_BYTES];
  pack_header(frame);

  fl_socket_t sock = FL_INVALID_SOCKET;
  const auto backoff = std::chrono::seconds(1);
  /* "one backoff ago" so the first attempt is immediate (time_point::min()
     would overflow the subtraction below) */
  clock::time_point last_attempt = clock::now() - backoff;

  auto drop_link = [&]() {
    if (sock != FL_INVALID_SOCKET) { fl_close_socket(sock); sock = FL_INVALID_SOCKET; }
    connected.store(false, std::memory_order_relaxed);
    last_attempt = clock::now();
  };

  /* One pass over whatever is queued: send on a live link, else drop.
     Returns when the ring is empty. */
  auto drain = [&]() {
    FrameRec r;
    bool checked_peer = false;
    while (ring_pop(r)) {
      if (sock != FL_INVALID_SOCKET && !checked_peer) {
        checked_peer = true;
        if (peer_gone(sock)) drop_link();
      }
      if (sock == FL_INVALID_SOCKET) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      long long vals[NVALS] = {
        sent.load(std::memory_order_relaxed) + 1,   /* frame: 1-based */
        r.swapcount, r.flip_us, r.ticks_us, r.missed,
        r.cmds_applied, r.ds_dispatched
      };
      memcpy(frame + DATA_OFF, vals, DATALEN);
      if (send_all(sock, frame, FRAME_BYTES)) {
        sent.fetch_add(1, std::memory_order_relaxed);
      } else {
        dropped.fetch_add(1, std::memory_order_relaxed);
        drop_link();
      }
    }
  };

  while (!stop_req.load(std::memory_order_acquire)) {
    if (sock == FL_INVALID_SOCKET &&
        clock::now() - last_attempt >= backoff) {
      last_attempt = clock::now();
      sock = connect_timeout(h, p, 1000);
      connected.store(sock != FL_INVALID_SOCKET, std::memory_order_relaxed);
    }

    drain();

    /* Sleep until the producer notifies or a short backstop elapses (the
       producer notifies without holding wake_mx, so a wakeup can be lost;
       the backstop bounds that to a few ms and also paces reconnects). */
    std::unique_lock<std::mutex> lk(wake_mx);
    wake_cv.wait_for(lk, std::chrono::milliseconds(5), [] {
      return stop_req.load(std::memory_order_acquire) || !ring_empty();
    });
  }

  /* Stop requested: the render thread stops pushing once `enabled` drops,
     which stop_locked() does after this join, so anything still queued was
     a real swap -- flush it before closing rather than losing the tail. */
  drain();
  if (sock != FL_INVALID_SOCKET) fl_close_socket(sock);
  connected.store(false, std::memory_order_relaxed);
}

/* caller holds mgmt_mx */
void stop_locked()
{
  if (!writer) return;
  stop_req.store(true, std::memory_order_release);
  wake_cv.notify_all();
  if (writer->joinable()) writer->join();
  delete writer;
  writer = nullptr;
  enabled.store(false, std::memory_order_release);
  connected.store(false, std::memory_order_relaxed);
}

/* "host" or "host:port"; a trailing ":digits" is the port */
bool parse_hostspec(const char *spec, std::string &h, int &p)
{
  if (!spec || !*spec) return false;
  std::string s(spec);
  h = s; p = 4620;
  size_t colon = s.rfind(':');
  if (colon != std::string::npos && colon + 1 < s.size()) {
    bool digits = true;
    for (size_t i = colon + 1; i < s.size(); i++)
      if (s[i] < '0' || s[i] > '9') { digits = false; break; }
    if (digits) {
      long v = strtol(s.c_str() + colon + 1, nullptr, 10);
      if (v < 1 || v > 65535) return false;
      p = (int) v;
      h = s.substr(0, colon);
    }
  }
  return !h.empty();
}

} // namespace

/* ---- C API ------------------------------------------------------------ */

int frameLogEnabled(void)
{
  return enabled.load(std::memory_order_relaxed) ? 1 : 0;
}

void frameLogPush(int swapcount, long long flip_us, long long ticks_us,
                  long missed, long long cmds_applied,
                  long long ds_dispatched)
{
  if (!enabled.load(std::memory_order_relaxed)) return;
  FrameRec r;
  r.swapcount = swapcount;
  r.flip_us = flip_us;
  r.ticks_us = ticks_us;
  r.missed = missed;
  r.cmds_applied = cmds_applied;
  r.ds_dispatched = ds_dispatched;
  if (!ring_push(r)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  wake_cv.notify_one();       /* no lock: never blocks the render thread */
}

int frameLogStart(const char *spec, const char **err)
{
  std::string h; int p;
  if (!parse_hostspec(spec, h, p)) {
    if (err) *err = "stimFrameLog: expected host or host:port";
    return -1;
  }
  std::lock_guard<std::mutex> g(mgmt_mx);
  if (writer && h == host && p == port) return 0;     /* already streaming */
  stop_locked();

  host = h; port = p; hostspec = spec;
  sent.store(0, std::memory_order_relaxed);
  dropped.store(0, std::memory_order_relaxed);
  connected.store(false, std::memory_order_relaxed);
  ring_reset();
  stop_req.store(false, std::memory_order_release);
  if (!atexit_registered) {
    atexit(frameLogStop);
    atexit_registered = true;
  }
  writer = new std::thread(writer_main, h, p);
  enabled.store(true, std::memory_order_release);
  return 0;
}

void frameLogStop(void)
{
  std::lock_guard<std::mutex> g(mgmt_mx);
  stop_locked();
}

void frameLogStatus(framelog_status_t *s)
{
  std::lock_guard<std::mutex> g(mgmt_mx);
  s->enabled = writer ? 1 : 0;
  s->host = hostspec.c_str();
  s->sent = sent.load(std::memory_order_relaxed);
  s->dropped = dropped.load(std::memory_order_relaxed);
  s->connected = connected.load(std::memory_order_relaxed) ? 1 : 0;
}
