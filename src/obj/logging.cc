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
#include <cctype>
#include <cinttypes>

#include <chrono>
#include <iostream>
#include <algorithm>

#include "logging.hh"
#include "logger.hh"
#include "fnexp.hh"

static std::string level_midname(const char *pfx,
                                 Logger::level_t base, Logger::level_t lvl)
{
  std::stringstream buf;
  buf << pfx << "+" << lvl - base;
  return buf.str();
}

static std::string level_name(Logger::level_t lvl)
{
  if (lvl > Logger::SILENT)
    return level_midname("SILENT", Logger::SILENT, lvl);
  if (lvl == Logger::SILENT) return "SILENT";
  if (lvl > Logger::CRITICAL)
    return level_midname("CRITICAL", Logger::CRITICAL, lvl);
  if (lvl == Logger::CRITICAL) return "CRITICAL";
  if (lvl > Logger::ERROR)
    return level_midname("ERROR", Logger::ERROR, lvl);
  if (lvl == Logger::ERROR) return "ERROR";
  if (lvl > Logger::WARN)
    return level_midname("WARN", Logger::WARN, lvl);
  if (lvl == Logger::WARN) return "WARN";
  if (lvl > Logger::INFO)
    return level_midname("INFO", Logger::INFO, lvl);
  if (lvl == Logger::INFO) return "INFO";
  if (lvl > Logger::DEBUG)
    return level_midname("DEBUG", Logger::DEBUG, lvl);
  if (lvl == Logger::DEBUG) return "DEBUG";
  if (lvl > Logger::TRACE)
    return level_midname("TRACE", Logger::TRACE, lvl);
  if (lvl == Logger::TRACE) return "TRACE";
  if (lvl > Logger::DETAIL)
    return level_midname("DETAIL", Logger::DETAIL, lvl);
  if (lvl == Logger::DETAIL) return "DETAIL";
  if (lvl > Logger::ALL)
    return level_midname("ALL", Logger::ALL, lvl);
  assert(lvl == Logger::ALL);
  return "ALL";
}

LoggingContext::LoggingContext() : level(Logger::INFO), out(&std::cerr) { }

static const std::map<std::string, Logger::level_t> level_names = {
  { "silent", Logger::SILENT },
  { "critical", Logger::CRITICAL },
  { "error", Logger::ERROR },
  { "warn", Logger::WARN },
  { "info", Logger::INFO },
  { "debug", Logger::DEBUG },
  { "trace", Logger::TRACE },
  { "detail", Logger::DETAIL },
  { "all", Logger::ALL },
};

static bool parse_level(Logger::level_t &lvl, const std::string &txt)
{
  std::string cp = txt;
  std::transform(cp.begin(), cp.end(), cp.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto pos = level_names.find(cp);
  if (pos == level_names.end()) return false;
  lvl = pos->second;
  return true;
}

void LoggingContext::set(const YAML::Node &cfg,
                         log_filemap_t &out_table,
                         LoggingContext *par)
{
  const auto &file_root = cfg["file"];
  if (file_root) {
    auto fn = file_root.as<std::string>();
    std::filesystem::path fp(expand_filename(fn)[0]);
    std::ofstream &out = out_table[fp];
    if (!out.is_open())
      out.open(fp, std::ios::app);
    this->out = &out;
  } else if (par) {
    this->out = par->out;
  } else {
    this->out = &std::cerr;
  }

  const auto &level_root = cfg["level"];
  if (level_root) {
    auto sval = level_root.as<std::string>();
    if (!parse_level(level, sval))
      level = level_root.as<Logger::level_t>();
  } else if (par) {
    level = par->level;
  } else {
    level = Logger::INFO;
  }

  subs.clear();
  const auto &children_root = cfg["children"];
  if (children_root) {
    for (auto iter = children_root.begin(); iter != children_root.end(); iter++) {
      auto k = iter->first.as<std::string>();
      auto &v = iter->second;
      subs[k].set(v, out_table, this);
    }
  }
}

Logging Logging::instance;

Logging::Logging() : serial(1) { }

void Logging::configure_in(const YAML::Node &cfg)
{
  /* Close existing files. */
  out_table.clear();

  /* Recursively set the hierarchy. */
  if (cfg)
    ctx_root.set(cfg, out_table, nullptr);

  serial++;
}

LoggingContext &Logging::find(const std::vector<std::string> &parts)
{
  LoggingContext *ptr = &ctx_root;
  for (auto &s : parts) {
    auto pos = ptr->subs.find(s);
    if (pos == ptr->subs.end()) break;
    ptr = &pos->second;
  }
  return *ptr;
}

static std::vector<std::string> split_name(const std::string &name)
{
  std::vector<std::string> result;
  std::size_t last = 0, pos;
  while ((pos = name.find(".", last)) != std::string::npos) {
    if (pos > last)
      result.push_back(name.substr(last, pos - last));
    last = pos + 1;
  }
  result.push_back(name.substr(last));
  return result;
}

Logger::Logger(const char *name, const std::string &comp)
  : name(split_name(name)),
    logging(Logging::instance), serial(0), comp(comp)
{
}

Logger::level_t Logger::level()
{
  if (!ctx || logging.serial > serial) {
    ctx = &logging.find(name);
    serial = logging.serial;
  }
  return ctx->level;
}

static void log_time(std::ostream &out)
{
  auto tp = std::chrono::system_clock::now();
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);

  /* We lost fractions of a second in the conversion, so convert back,
     and subtract. */
  auto tp0 = std::chrono::system_clock::from_time_t(tt);
  uint_fast32_t ms = std::chrono::duration<double, std::micro>(tp - tp0).count();

  /* Convert to a calendar time in UTC, then format, then append the
     microseconds. */
  std::tm mytm = *std::gmtime(&tt);
  char buf[150];
  auto rc = strftime(buf, sizeof buf, "%FT%T", &mytm);
  snprintf(buf + rc, sizeof buf - rc, ".%06" PRIuFAST32 "Z", ms);
  out << buf;
}

void Logger::report(level_t lvl, messenger_t msgr)
{
  if (lvl < level()) return;
  std::stringstream buf;
  log_time(buf);
  buf << " " << level_name(lvl) << " " << comp << " ";
  msgr(buf);
  *ctx->out << buf.str() << std::endl << std::flush;
}
