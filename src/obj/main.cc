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
#include <cstring>

#include <sys/epoll.h>

#include <functional>
#include <filesystem>
#include <map>
#include <string>
#include <memory>
#include <iostream>
#include <system_error>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#include "config.hh"
#include "streamer.hh"
#include "egress.hh"
#include "destinations.hh"
#include "exits.hh"
#include "idle.hh"
#include "scheduling.hh"
#include "emitters.hh"
#include "absorbers.hh"
#include "timed.hh"
#include "quotas.hh"
#include "addressing.hh"
#include "channels.hh"
#include "formatting.hh"
#include "fnexp.hh"
#include "logging.hh"

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

static int trapped_main(Logger &, Config &);

int main(int argc, const char *const *argv)
{
  Logger log("udptun.main", "main");
  /* Arguments are just filename configurations. */
  std::vector<std::string> config_filenames;
  config_filenames.reserve(argc - 1);
  for (int i = 1; i < argc; i++)
    config_filenames.push_back(argv[i]);
  Config config(config_filenames);

  try {
    /* Block a bunch of signals.  These should include the ones we
       handle outside the polling, and the ones in too. */
    sigset_t okay_sigs;
    if (sigemptyset(&okay_sigs) < 0)
      throw std::system_error(errno, std::system_category(), "sigemptyset");
    if (sigaddset(&okay_sigs, SIGHUP) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(HUP)");
    if (sigaddset(&okay_sigs, SIGINT) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(INT)");
    if (sigaddset(&okay_sigs, SIGTERM) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(TERM)");
    if (sigaddset(&okay_sigs, SIGUSR2) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(USR2)");
    if (sigprocmask(SIG_BLOCK, &okay_sigs, nullptr) < 0)
      throw std::system_error(errno, std::system_category(), "sigprocmask");

    trapped_main(log, config);
  } catch (const std::system_error &e) {
    log.critical([&e](std::ostream &out) {
      out << "system error: (" << e.code();
      if (e.code().category() == std::system_category())
        out << "; " << strerrorname_np(e.code().value());
      out << ") " << e.what() << std::endl;
    });
  } catch (const std::runtime_error &e) {
    log.critical([&e](std::ostream &out) {
      out << "runtime error: " << e.what() << std::endl;
    });
  } catch (const std::exception &e) {
    log.critical([&e](std::ostream &out) {
      out << "unknown exception: " << e.what() << std::endl;
    });
  }
}

static int trapped_main(Logger &log, Config &config)
{
  Scheduler sched;
  bool reload = false, quit = false;
  SignalEvent on_sighup(sched, [&reload]() { reload = true; });
  SignalEvent on_sigint(sched, [&quit]() { quit = true; });
  SignalEvent on_sigterm(sched, [&quit]() { quit = true; });
  on_sigint.set(SIGINT);
  on_sigterm.set(SIGTERM);

  {
    /* Express which signals are going to be blocked while polling.
       We need only need to block ones that the scheduler manages, as
       signalfd() requires them to be blocked. */
    sigset_t poll_sigs;
    if (sigemptyset(&poll_sigs) < 0)
      throw std::system_error(errno, std::system_category(), "sigemptyset");
    if (sigaddset(&poll_sigs, SIGUSR2) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(USR2)");
    if (sigaddset(&poll_sigs, SIGINT) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(INT)");
    if (sigaddset(&poll_sigs, SIGTERM) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(TERM)");
    if (sigaddset(&poll_sigs, SIGHUP) < 0)
      throw std::system_error(errno, std::system_category(), "sigaddset(HUP)");
    sched.signal_mask(poll_sigs);
  }

  AddressManager addrmgr(sched, SIGUSR2);

  Quota quota;

  bool more;
  IdleEvent idle(sched, [&more]() { more = false; });
  idle.prio(10);
  TimedEvent quit_timeout(sched, [&more]() { more = false; });

  while (!quit) {
    /* Prepare to detect a new SIGHUP signal. */
    reload = 0;
    on_sighup.set(SIGHUP);

    /* (Re-)load configuration. */
    log.info("reading config");
    YAML::Node root = config.get();

    Logging::configure(root["logging"]);

    std::filesystem::path queuedir("/var/spool/udptun");
    if (root["queues"]) {
      const auto &queues_root = root["queues"];
      if (queues_root["path"]) {
        auto raw = queues_root["path"].as<std::string>();
        auto opts = expand_filename(raw);
        if (opts.size() != 1)
          throw std::runtime_error(sformat("bad expansion for queue path: %s",
                                           raw.c_str()));
        queuedir = opts[0];
      }
    }

    /* Set the quota from configuration. */
    // TODO

    log.info("starting");

    std::filesystem::path egress_qdir = queuedir / "egress";
    std::filesystem::path ingress_qdir = queuedir / "ingress";
    std::filesystem::create_directory(egress_qdir);
    std::filesystem::create_directory(ingress_qdir);

    /* Prepare to create indices of egresses and absorber.  These are
       retained within a reset iteration, but discarded before the
       next. */
    std::map<std::string, std::shared_ptr<Egress>> egress_index;
    std::map<std::string, std::shared_ptr<Absorber>> absorber_index;
    {
      if (root["ingress"]) {
        const auto &ingress_root = root["ingress"];

        /* Create an index of named tunnel egresses.  Each will
           form a stream connection to a tunnel ingress on another
           host.  Ingresses unused by any channel are quietly
           destroyed on exit from this block. */
        std::map<std::string, std::shared_ptr<Ingress>> ingress_index;
        populate<Ingress>(ingress_index, "tunnels", ingress_root,
                          [&sched, &addrmgr]
                          (const std::string &inst,
                           const YAML::Node &cfg) {
                            return make_ingress(sched, addrmgr, inst, cfg);
                          });

        /* Create an index of named channels.  Each channel identifies
           a tunnel ingress to send datagrams through, and label set
           to send them with.  It also maintains a named message
           queue.  Channels unused by any absorber are quietly
           destroyed on exit from this block. */
        std::map<std::string, std::shared_ptr<Channel>> channel_index;
        populate<Channel>(channel_index, "channels", ingress_root,
                          [&sched, &ingress_index, &quota, &ingress_qdir]
                          (const std::string &inst,
                           const YAML::Node &cfg) {
                            auto tun = cfg["tunnel"].as<std::string>();
                            auto pos = ingress_index.find(tun);
                            if (pos == ingress_index.end())
                              throw std::runtime_error
                                (sformat("ingress channel %s has no tunnel %s",
                                         inst.c_str(), tun.c_str()));

                            /* Get the label set by OR-ing the label
                               numbers. */
                            labelset_t labels = 0;
                            for (auto iter = cfg["labels"].begin();
                                 iter != cfg["labels"].end(); iter++) {
                              auto lbl = iter->as<unsigned>();
                              labels |= 1u << lbl;
                            }

                            return new Channel(inst, sched, pos->second,
                                               labels, quota,
                                               ingress_qdir / inst);
                          });
        auto find_channel =
          [&cidx = channel_index](const std::string &cn) {
            auto pos = cidx.find(cn);
            if (pos == cidx.end())
              return std::shared_ptr<Channel>();
            return pos->second;
          };

        /* Populate the table of absorbers.  Each will create a
           datagram socket, and anything it receives will be sent to
           each of its channels, which it retains references to. */
        populate<Absorber>(absorber_index, "sockets", ingress_root,
                           [&sched, &find_channel]
                           (const std::string &inst,
                            const YAML::Node &cfg) {
                             return make_absorber(sched, inst, cfg, find_channel);
                           });
      }

      if (root["egress"]) {
        const auto &egress_root = root["egress"];

        /* Create an index of named destinations.  Exits will refer to
           these by name.  Any not used after the block exits will be
           quietly destroyed. */
        std::map<std::string, std::shared_ptr<Destination>> destinations;
        populate<Destination>(destinations, "destinations", egress_root,
                              [](const std::string &inst,
                                 const YAML::Node &cfg) {
                                return new Destination(inst, cfg);
                              });
        auto find_dest = [&didx = destinations](const std::string &dname) {
          auto pos = didx.find(dname);
          if (pos == didx.end())
            return std::shared_ptr<Destination>();
          return pos->second;
        };

        /* Create an index of named exits, and the emitters they
           share.  Each exit uses exactly one emitter, and keeps a
           shared reference to it.  Each exit also uses exactly one
           named destination, preventing it from being destroyed.  Any
           resources not used after the block exits will be quietly
           destroyed.  An exit retains a message queue, indexed by its
           name. */
        std::map<std::string, std::shared_ptr<Exit>> exit_index;
        if (egress_root["sockets"]) {
          const auto &socket_root = egress_root["sockets"];
          for (auto iter = egress_root["sockets"].begin();
               iter != egress_root["sockets"].end(); iter++) {
            make_exits(sched, quota, egress_qdir, *iter, find_dest, exit_index);
          }
        }

        /* Create the configured egresses, using the available exits.
           Sockets are not created at this stage; only dependencies
           are established, so that missing dependencies will fail the
           configuration phase. */
        populate<Egress>(egress_index, "tunnels", egress_root,
                         [&sched, &exit_index](const std::string &inst,
                                               const YAML::Node &cfg) {
                           return make_egress(sched, inst, exit_index, cfg);
                         });
      }
    }

    /* Activate all egresses and their dependencies.  This creates the
       necessary sockets, and enables quota enforcement on the
       queues. */
    for (auto &egress : egress_index)
      egress.second->activate();

    /* Activate all absorbers and their dependencies.  This creates
       the necessary sockets, and enables quota enforcement on the
       queues. */
    for (auto &absorber : absorber_index)
      absorber.second->activate();

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
        quit_timeout.set(std::chrono::seconds(10));
      }
    }
  }

  log.info("terminating");
  return EXIT_SUCCESS;
}
