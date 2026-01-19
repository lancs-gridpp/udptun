// -*- c-basic-offset: 2; indent-tabs-mode: nil -*-

/*
 * Copyright (c) 2025, Lancaster University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>

#include <cassert>
#include <cstring>

#include <sstream>
#include <string>
#include <algorithm>
#include <functional>

#include "tcp_egress.hh"
#include "destruction.hh"
#include "emitters.hh"
#include "exits.hh"
#include "destinations.hh"
#include "network.hh"
#include "marshaling.hh"
#include "addrent.hh"
#include "destbank.hh"
#include "cfghelp.hh"

void TCPEgress::Listener::handle_fd(uint32_t)
{
  Logger &log = parent.log;

  /* A connection has been requested.  Accept it to get the file
     descriptor, and make a Connection object out of it. */
  do {
    union {
      struct sockaddr addr;
      unsigned char buf[256];
    } space;
    socklen_t addrlen = sizeof space;
    int clsock = ::accept(sock, &space.addr, &addrlen);
    if (clsock < 0) {
      int ec = errno;
      switch (ec) {
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
      case ENONET:
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
        continue;

      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        break;

      default:
        fdev.cancel();
        close(sock), sock = -1;
        parent.log.warn([this, ec](auto &out) {
          out << "accept() for " << addr.str() << " => "
              << ec << " (" << strerror(ec) << ")";
        });
        rbev.set(std::chrono::seconds(30));
        return;
      }
    }

    switch (errno) {
      case ECONNABORTED:
      case EINTR:
      case ENOBUFS:
      case ENOMEM:
        /* There's nothing to do in these cases.  Wait for another
           connection. */
        break;

    default:
      /* A connection was established.  Match the peer's address
         against our table, to map it to a peer name and a 'hash
         code'.  The hash code can be set by user configuration, but
         defaults to a genuine hash of the peer name. */
      assert(clsock >= 0);
      std::string pname;
      unsigned hash;
      if (parent.peers.seek(pname, hash, &space.addr, addrlen)) {
        /* Create and retain a new Connection object to handle packets
           received on the socket. */
        parent.conns.emplace_back(parent, pname, hash,
                                  clsock, &space.addr, addrlen);
        log.info([this, &space, addrlen, pname, hash](std::ostream &out) {
          out << sock << ": new peer " << to_str(&space.addr, addrlen)
              << ' ' << pname << " hash " << hash;
        });
      } else {
        /* The peer is not recognized, so close the socket. */
        log.warn([this, &space, addrlen](auto &out) {
          out << sock << ": unknown peer " << to_str(&space.addr, addrlen);
        });
        ::close(clsock);
      }
      break;
    }

    /* Get ready to accept another connection. */
    assert(sock >= 0);
    fdev.set(sock, EPOLLIN);
    return;
  } while (true);
}

void TCPEgress::Listener::activate()
{
  if (sock < 0) rebind();
}

void TCPEgress::Listener::rebind()
{
  parent.log.debug([this](auto &out) {
    out << "starting " << addr.str();
  });

  /* Try creating a socket of the right type. */
  assert(sock < 0);
  sock = ::socket(family, SOCK_STREAM, protocol);
  if (sock < 0) {
    int ec = errno;
    /* Socket creation failed.  Log the error, and try again later. */
    parent.log.warn([this, ec](auto &out) {
      out << "socket(" << af_to_str(family) << ", STREAM"
          << proto_to_str(protocol) << ") for " << addr.str()
          << " => " << ec << " (" << strerror(ec) << ")";
    });
    assert(sock < 0);
    rbev.set(std::chrono::seconds(30));
    return;
  }

  /* Socket creation was successful.  Try binding it. */
  if (::bind(sock, addr.addr(), addr.len()) != 0) {
    /* Binding failed.  Close the socket, log the error, and try
       again. */
    int ec = errno;
    ::close(sock), sock = -1;
    parent.log.warn([this, ec](auto &out) {
      out << "bind(" << addr.str() << ") => "
          << ec << " (" << strerror(ec) << ")";
    });
    assert(sock < 0);
    rbev.set(std::chrono::seconds(65));
    return;
  }

  /* Make the socket accept connections. */
  if (::listen(sock, 5) != 0) {
    /* Accepting connections failed.  Close the socket, log the error,
       and try again later. */
    int ec = errno;
    ::close(sock), sock = -1;
    parent.log.warn([this, ec](auto &out) {
      out << "listen() for " << addr.str() << " => "
          << ec << " (" << strerror(ec) << ")";
    });
    assert(sock < 0);
    rbev.set(std::chrono::seconds(30));
    return;
  }

  /* Socket is ready.  Make sure we are notified when accept() can be
     called on it. */
  assert(sock >= 0);
  rbev.cancel();
  fdev.set(sock, EPOLLIN);
  parent.log.debug([this](auto &out) {
    out << addr.str() << " ready sock=" << sock;
  });
}

TCPEgress::Listener::Listener(TCPEgress &parent, int family, int protocol,
                              const struct sockaddr *addr, socklen_t addrlen)
  : parent(parent), family(family), protocol(protocol),
    addr(addr, addrlen), sock(-1),
    fdev(parent.sched,
         std::bind(&Listener::handle_fd, this, std::placeholders::_1)),
    rbev(parent.sched, std::bind(&Listener::rebind, this))
{
  fdev.name(sformat("egress:%s:listen:%d", parent.name.c_str(), sock));
}

TCPEgress::Listener::~Listener()
{
  /* Cancel any outstanding expectation before closing the socket. */
  if (sock >= 0) {
    parent.log.debug([this](auto &out) {
      out << addr.str() << " destroyed sock=" << sock;
    });
    fdev.cancel();
    close(sock);
  }
}

TCPEgress::Connection::Connection(TCPEgress &parent, const std::string &name,
                                  unsigned hash, int sock,
                                  const struct sockaddr *addr, socklen_t addrlen)
  : parent(parent),
    name(name),
    hash(hash),
    sock(sock), len(0),
    fdev(parent.sched,
         std::bind(&Connection::handle_fd, this, std::placeholders::_1)),
    peeraddr(addr, addrlen)
{
  /* Get ready to receive immediately. */
  assert(sock >= 0);
  fdev.name(sformat("egress:%s:connection:%d", parent.name.c_str(), sock));
  fdev.set(sock, EPOLLIN);
}

TCPEgress::Connection::~Connection()
{
  /* Cancel any outstanding expectation before closing the socket. */
  fdev.cancel();
  if (sock >= 0)
    close(sock);
}

TCPEgress::Connection::ClientState::ClientState(const std::string &ename,
                                                const std::string &pname,
                                                clid_t clid,
                                                unsigned hash,
                                                channelmap_t &chmap,
                                                DestinationBank &dbank,
                                                Scheduler &sched)
  : log("udptun.egress.tunnel.tcp.client",
        ename + ':' + pname + ':' + std::to_string(clid)),
    last_used(std::chrono::system_clock::now())
{
  /* Get a full set of destinations that we need to talk to, and
     ensure they are activated.  Also keep a reverse map from
     destination to name, for diagnostics. */
  std::set<std::shared_ptr<Destination>> required_dests;
  std::map<std::shared_ptr<Destination>, std::string> dest_names;
  std::map<std::string, std::shared_ptr<Destination>> name_dests;
  for (auto &ent : chmap) {
    auto &dnames = ent.second;
    for (auto &dname : dnames) {
      std::shared_ptr<Destination> dest = dbank.seek(dname, hash);
      if (!dest) {
        log.error([&dname](auto &out) {
          out << "unknown name " << dname;
        });
        continue;
      }
      required_dests.insert(dest);
      dest_names[dest] = dname;
      name_dests[dname] = dest;
      dest->activate();
    }
  }

  /* Erode the set of required destinations, populating dest_em with
     an emitter per destination.  Several destinations may use the
     same emitter. */
  std::map<std::shared_ptr<Destination>, std::shared_ptr<Emitter>> dest_em;
  while (!required_dests.empty()) {
    EmitterMaker mkr(required_dests);
    if (mkr.matched().empty())
      // TODO: Provide more context in the message.
      throw std::runtime_error("could not make emitters for all destinations");

    // TODO: Choose a name for the emitter.

    /* Create the emitter, map the matched destinations to it, and
       eliminate those destinations from the required set. */
    mkr.make("dummy", dest_em);
  }

  /* Populate the mapping from label to <destination, emitter> pair. */
  for (auto &ent : chmap) {
    label_t label = ent.first;
    auto &dnames = ent.second;
    for (auto &dname : dnames) {
      std::shared_ptr<Destination> dest = name_dests[dname];
      if (!dest) continue;
      std::shared_ptr<Emitter> em = dest_em[dest];
      if (!em) continue;
      outlets[label][dest] = em;
    }
  }
}

void TCPEgress::Connection::ClientState::deliver(label_t label,
                                                 const unsigned char *base,
                                                 std::size_t len)
{
  /* Ignore unknown labels. */
  auto pos = outlets.find(label);
  if (pos == outlets.end()) return;

  /* Send the message to each destination, using an appropriate
     emitter. */
  log.detail([&pos](auto &out) {
    out << "sending to " << pos->second.size() << " dests";
  });
  for (auto &ent : pos->second)
    ent.second->send(base, len, *ent.first.get(), 0);

  last_used = std::chrono::system_clock::now();
}

bool TCPEgress::Connection::process()
{
  /* Do we have a full packet? */
  clid_t clid;
  label_t label;
  payloadlen_t pktlen;
  parent.log.all([this](auto &out) {
    out << name << ": buffer (" << len << "):";
    auto lim = std::max(len, size_t(20));
    for (size_t i = 0; i < lim; i++)
      out << ' ' << sformat("%02X", buf[i]);
    if (len > lim)
      out << "...";
  });
  const unsigned char *base = decode_message(clid, label, pktlen, buf, len);
  if (!base) return false; // Packet is incomplete.

  parent.log.all([this, clid, label, pktlen, base](auto &out) {
    out << name << ": clid=" << clid << " label=" << label
        << " len=" << pktlen << " payload=";
    auto lim = std::min(payloadlen_t(20), pktlen);
    for (size_t i = 0; i < lim; i++)
      out << ' ' << sformat("%02X", base[i]);
    if (pktlen > lim)
      out << "...";
  });
  auto [ pos, ins ] = clstats.try_emplace(clid,
                                          parent.name,
                                          name,
                                          clid,
                                          hash,
                                          parent.channels, parent.dbank,
                                          parent.sched);
  if (ins)
    parent.log.detail([this, clid](auto &out) {
      out << name << ": new entry for " << clid << '/' << hash;
    });
  auto &clstat = pos->second;
  clstat.deliver(label, base, pktlen);

  /* Consume the header and payload. */
  len = (buf + len) - (base + pktlen);
  memmove(buf, base + pktlen, len);
  return true;
}

void TCPEgress::Connection::handle_fd(uint32_t)
{
  Logger &log = parent.log;
  assert(sock >= 0);
  ssize_t rc = recv(sock, buf + len, sizeof buf - len, 0);
  if (rc <= 0) {
    /* The client has closed the connection, or it has timed out. */
    if (rc == 0) {
      log.trace([this](auto &out) {
        out << name << ": client closed";
      });
    } else {
      int ec = errno;
      log.trace([this, ec](auto &out) {
        out << name << ": client error: " << ec
            << " (" << ::strerror(ec) << ")";
      });
    }
    fdev.cancel();
    close(sock), sock = -1;

    /* Make sure this entry gets cleaned out. */
    parent.idev.set();
    return;
  } else {
    typeof(len) nl = len + rc;
    log.detail([rc, this, nl](auto &out) {
      out << name << ": " << rc
          << "=recv(" << sock << ", *, "<< len << ") now " << nl;
    });
    /* Be ready to receive more. */
    fdev.set(sock, EPOLLIN);

    len = nl;

    /* Deliver any complete payloads to the exits, and clear the data
       out. */
    while (process())
      ;
  }
}

TCPEgress::TCPEgress(const std::string &name,
                     Scheduler &sched,
                     DestinationBank &dbank,
                     PeerTable &peers,
                     const channelmap_t &channels,
                     const YAML::Node &cfg)
  : name(name),
    log("udptun.egress.tunnel.tcp", std::string("egress:") + name),
    sched(sched),
    idev(sched, std::bind(&TCPEgress::flush, this)),
    ipv4(cfg["ipv4"].as<bool>("true")),
    ipv6(cfg["ipv6"].as<bool>("true")),
    host(cfg["host"].as<std::string>("localhost")),
    srv(cfg["port"].as<std::string>("9999")),
    client_timeout(duration(cfg["clid_timeout"].as<std::string>("1h"))),
    channels(channels), dbank(dbank),
    peers(&peers) { }

bool TCPEgress::Connection::flushable()
{
  return sock < 0;
}

bool TCPEgress::Listener::flushable()
{
  return sock < 0 && !rbev;
}

void TCPEgress::Connection::flush(std::chrono::system_clock::time_point epoch)
{
  /* Ask each client state if it has been used since the given epoch.
     If not, remove it. */
  for (auto iter = clstats.begin(); iter != clstats.end(); ) {
    if (iter->second.expired(epoch)) {
      auto clid = iter->first;
      parent.log.detail([this, clid](auto &out) {
        out << name << ": dropping " << clid << '/' << hash;
      });
      iter = clstats.erase(iter);
    } else {
      ++iter;
    }
  }
}

void TCPEgress::flush()
{
  /* Go through all connections, deleting those which are closed. */
  conns.remove_if([](Connection &c) { return c.flushable(); });

  /* Give each remaining connection a chance to clear out old client
     states. */
  auto now = std::chrono::system_clock::now();
  std::for_each(conns.begin(), conns.end(),
                std::bind(&Connection::flush, std::placeholders::_1,
                          now - client_timeout));

  /* Go through all listeners, deleting those which are closed and are
     not expected to re-open. */
  listeners.remove_if([](Listener &c) { return c.flushable(); });
}

void TCPEgress::activate()
{
  log.debug("activating");

  /* For our own sockets, restrict what we're looking for. */
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv4 ? ipv6 ? AF_UNSPEC : AF_INET : AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;

  /* Resolve the host. */
  struct addrinfo *info = nullptr;
  get_address_info(info, host.c_str(), srv.c_str(),
                   &hints, sformat("egress:%s", name.c_str()).c_str());
  log.detail([this](auto &out) {
    out << "gai(" << host << ":" << srv << ")";
  });

  std::set<AddressEntry> addrs;
  for (auto iter = info; iter; iter = iter->ai_next) {
    auto f = addrs.emplace(*iter);
    log.detail([&f](std::ostream &out) {
      if (f.second) out << "new ";
      out << "gai result: " << std::string(*f.first);
    });
  }

  for (auto iter = addrs.begin(); iter != addrs.end(); iter++)
    listeners.emplace_back(*this, iter->family, iter->protocol,
                           iter->addr(), iter->addrlen).activate();
}

bool TCPEgress::busy()
{
  log.debug("flush");
  flush();
  return !conns.empty();
}

void TCPEgress::deactivate()
{
  /* Stop listening for new connections. */
  listeners.clear();

  /* Tell existing connections to shutdown(WR). */
  for (auto &conn : conns)
    conn.deactivate();
}

void TCPEgress::Connection::deactivate()
{
  if (sock >= 0) {
    parent.log.debug([this](auto &out) {
      out << name << ": telling client to stop";
    });

    unsigned char b = 0;
    ::send(sock, &b, sizeof b, 0);
  }
}
