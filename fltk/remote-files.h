// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "remote.h"
namespace smallnet {
Response fileFetch(const Url &, const Session &);
Published filePush(const Url &, const Url &, const std::string &, const std::string &,
                   const std::string &, const Session &, bool);
}
