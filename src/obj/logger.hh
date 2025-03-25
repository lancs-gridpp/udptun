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

#ifndef logger_included
#define logger_included

#include <string>
#include <functional>
#include <sstream>
#include <vector>

#include "formatting.hh"

class Logging;
class LoggingContext;

struct Logger {
  typedef unsigned level_t;

  static constexpr level_t SILENT = 500;
  static constexpr level_t CRITICAL = 400;
  static constexpr level_t ERROR = 300;
  static constexpr level_t WARN = 200;
  static constexpr level_t INFO = 100;
  static constexpr level_t DEBUG = 0;

  Logger(const char *name, const std::string &comp);

  level_t level();

  typedef std::function<void(std::stringstream &)> messenger_t;
  void report(level_t, messenger_t);
  void critical(messenger_t m) { report(CRITICAL, m); }
  void error(messenger_t m) { report(ERROR, m); }
  void warn(messenger_t m) { report(WARN, m); }
  void info(messenger_t m) { report(INFO, m); }
  void debug(messenger_t m) { report(DEBUG, m); }

  void report(level_t lvl, const char *fmt) {
    report(lvl, [&fmt](std::stringstream &out) { out << fmt; });
  }

  void critical(const char *fmt) { report(CRITICAL, fmt); }
  void error(const char *fmt) { report(ERROR, fmt); }
  void warn(const char *fmt) { report(WARN, fmt); }
  void info(const char *fmt) { report(INFO, fmt); }
  void debug(const char *fmt) { report(DEBUG, fmt); }

  template <typename ...Args>
  void report(level_t lvl, const char *fmt, Args... args) {
    report(lvl, [&fmt, &args...](std::stringstream &out) {
      out << sformat(fmt, args...);
    });
  }

  template <typename ...Args>
  void critical(const char *fmt, Args... args) {
    report(CRITICAL, fmt, args...);
  }

  template <typename ...Args>
  void error(const char *fmt, Args... args) {
    report(ERROR, fmt, args...);
  }

  template <typename ...Args>
  void warn(const char *fmt, Args... args) {
    report(WARN, fmt, args...);
  }

  template <typename ...Args>
  void info(const char *fmt, Args... args) {
    report(INFO, fmt, args...);
  }

  template <typename ...Args>
  void debug(const char *fmt, Args... args) {
    report(DEBUG, fmt, args...);
  }

private:
  std::vector<std::string> name;
  Logging &logging;
  LoggingContext *ctx;
  unsigned serial;
  std::string comp;
};

#endif
