// Copyright (c) The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef HYPERCOIN_COMMON_LICENSE_INFO_H
#define HYPERCOIN_COMMON_LICENSE_INFO_H

#include <string>

std::string CopyrightHolders(const std::string& strPrefix);

/** Returns licensing information (for -version) */
std::string LicenseInfo();

#endif // HYPERCOIN_COMMON_LICENSE_INFO_H
