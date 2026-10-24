// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <gio/gio.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace smallnet {
constexpr size_t maxBytes = 8 * 1024 * 1024;
struct Identity {
  std::string certificate, key, penguinStore, penguinId;
  bool empty() const { return certificate.empty() && penguinId.empty(); }
};
struct Url {
  std::string scheme, host, path, query;
  int port = 1965;
  std::string str() const;
  std::string peer() const;
  Url withScheme(const std::string &) const;
  Url reading() const { return withScheme(scheme == "titan" ? "gemini" : scheme); }
  Url writing() const { return withScheme(scheme == "gemini" ? "titan" : scheme); }
};
Url parse(const std::string &);
std::string hash(const std::string &);
std::string textMime(const std::string &metadata);
struct TrustRequired : std::runtime_error {
  std::string peer, fingerprint, previous, subject;
  TrustRequired(std::string, std::string, std::string, std::string);
};
struct Conflict : std::runtime_error { using std::runtime_error::runtime_error; };
struct Session {
  Identity identity;
  std::map<std::string, std::string> pins;
  GCancellable *cancel = nullptr;
  int timeoutSeconds = 20;
};
struct Response {
  int status = 0;
  std::string meta, body, url;
  bool success() const { return status == 2 || status / 10 == 2; }
};
struct Published { std::string readUrl, digest; Response response; };
Response fetch(const std::string &, const Session &);
Published push(const std::string &readUrl, const std::string &titanUrl,
               const std::string &baseline, const std::string &body,
               const std::string &mime, const std::string &token,
               const Session &, bool overwrite = false);
std::vector<std::pair<std::string,std::string>> identities(const std::string &store);
}
