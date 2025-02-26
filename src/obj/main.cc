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

#include <cassert>
#include <csignal>
#include <cerrno>
#include <cstdlib>

#include <sys/epoll.h>

#include <functional>
#include <filesystem>
#include <map>
#include <string>
#include <memory>
#include <iostream>
#include <system_error>

#include <yaml-cpp/yaml.h>

#include "config.hh"
#include "periods.hh"
#include "realtime.hh"
#include "tunnels.hh"
#include "ears.hh"
#include "destinations.hh"
#include "exits.hh"
#include "idle.hh"
#include "scheduling.hh"
#include "idle.hh"
#include "timed.hh"
#include "quotas.hh"

static sig_atomic_t reload = 0, quit = 0;

static void on_reload(int sn)
{
  reload = 1;
}

static void on_quit(int sn)
{
  quit = 1;
}

template <class T>
static void populate(std::map<std::string, std::shared_ptr<T>> &dst,
                     const std::string &key,
                     const YAML::Node &root,
                     std::function<T *(const std::string &inst,
                                       const YAML::Node &cfg)> maker)
{
  auto end = root[key].end();
  for (auto iter = root[key].begin(); iter != end; iter++) {
    auto name = iter->first.as<std::string>();
    auto ref = std::shared_ptr<T>(maker(name, iter->second));
    if (ref) dst[name] = ref;
  }
}

template <class T>
static void populate(std::map<std::string, std::shared_ptr<T>> &dst,
                     const char *key, const YAML::Node &root,
                     std::function<T *(const std::string &inst,
                                       const YAML::Node &)> maker)
{
  populate(dst, std::string(key), root, maker);
}

int main(int argc, const char *const *argv)
{
  /* Arguments are just filename configurations. */
  std::vector<std::string> config_filenames(argc - 1);
  for (int i = 1; i < argc; i++)
    config_filenames.push_back(argv[i]);
  Config config(config_filenames);

  sigset_t okay_sigs;
  if (sigemptyset(&okay_sigs) < 0)
    throw std::system_error(errno, std::system_category(), "sigemptyset");
  if (sigaddset(&okay_sigs, SIGHUP) < 0)
    throw std::system_error(errno, std::system_category(), "sigaddset(HUP)");
  if (sigaddset(&okay_sigs, SIGINT) < 0)
    throw std::system_error(errno, std::system_category(), "sigaddset(INT)");
  if (sigaddset(&okay_sigs, SIGTERM) < 0)
    throw std::system_error(errno, std::system_category(), "sigaddset(TERM)");

  /* Block signals. */
  if (sigprocmask(SIG_BLOCK, &okay_sigs, nullptr) < 0)
    throw std::system_error(errno, std::system_category(), "sigprocmask");

  {
    extern const struct sigaction empty_sa;
    struct sigaction sa = empty_sa;
    if (sigfillset(&sa.sa_mask) < 0)
      throw std::system_error(errno, std::system_category(), "sigfillset");

    /* Set a handler for SIGHUP. */
    sa.sa_handler = &on_reload;
    if (sigaction(SIGHUP, &sa, NULL) < 0)
      throw std::system_error(errno, std::system_category(), "sigaction");

    /* Set a handler for SIGINT. */
    sa.sa_handler = &on_quit;
    if (sigaction(SIGINT, &sa, NULL) < 0)
      throw std::system_error(errno, std::system_category(), "sigaction");

    /* Set a handler for SIGTERM. */
    sa.sa_handler = &on_quit;
    if (sigaction(SIGTERM, &sa, NULL) < 0)
      throw std::system_error(errno, std::system_category(), "sigaction");
  }

  /* Express which signals we'll allow while polling. */
  sigset_t poll_sigs;
  if (sigemptyset(&poll_sigs) < 0)
    throw std::system_error(errno, std::system_category(), "sigemptyset");
  Scheduler sched;
  sched.signal_mask(poll_sigs);

  Quota quota;

  bool more;
  IdleEvent idle(sched, [&more]() { more = false; });
  idle.prio(10);
  TimedEvent quit_timeout(sched, [&more]() { more = false; });

  while (!quit) {
    /* Prepare to detect a new SIGHUP signal. */
    reload = 0;

    /* TODO: (Re-)load configuration. */
    std::cerr << "reading config" << std::endl;
    YAML::Node root = config.get();
    std::filesystem::path queuedir("/var/spool/udptun");
    if (root["state"] && root["state"]["queues"])
      queuedir = root["state"]["queues"].as<std::string>();

    /* Set the quota from configuration. */
    // TODO

    std::filesystem::path exit_queuedir = queuedir / "exits";
    std::filesystem::path tunnel_queuedir = queuedir / "tunnels";

    //std::map<std::string, std::unique_ptr<Tunnel>> tunnels;


    std::map<std::string, std::shared_ptr<Ear>> ears;
    {
      std::map<std::string, std::shared_ptr<Destination>> destinations;
      populate<Destination>(destinations, "destinations", root,
                            [](const std::string &inst,
                               const YAML::Node &cfg) {
                              return new Destination(cfg);
                            });
      std::map<std::string, std::shared_ptr<Exit>> exits;
      if (root["exits"]) {
        for (auto iter = root["exits"].begin();
             iter != root["exits"].end(); iter++) {
          make_exits(sched, quota, exit_queuedir, *iter,
                     [&dmap = destinations](const std::string &dname) {
                       auto pos = dmap.find(dname);
                       if (pos == dmap.end())
                         return std::shared_ptr<Destination>();
                       return pos->second;
                     },
                     exits);
        }
      }
      populate<Ear>(ears, "ears", root,
                    std::bind(&make_ear, sched,
                              std::placeholders::_1,
                              exits, std::placeholders::_2));
    }

    more = true;
    while (more) {
      sched.poll();

      /* If we've received SIGHUP, exit this loop as soon as we're
         idle. */
      if (reload) {
        reload = 0;
        idle.set();
      }

      /* If we've received SIGINT or SIGTERM, exit this loop as soon
         as we're idle.  Set a timer so we will quit anyway after a
         short time. */
      if (quit) {
        idle.set();
        quit_timeout.set(10, TimePeriod::SECOND);
      }
    }
  }

  std::cerr << "terminating" << std::endl;
  return EXIT_SUCCESS;
}
