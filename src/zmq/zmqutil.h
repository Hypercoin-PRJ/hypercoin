// Copyright (c) 2014-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_ZMQ_ZMQUTIL_H
#define HYPERCOIN_ZMQ_ZMQUTIL_H

#include <string>

void zmqError(const std::string& str);

/** Prefix for unix domain socket addresses (which are local filesystem paths) */
const std::string ADDR_PREFIX_IPC = "ipc://"; // used by libzmq, example "ipc:///root/path/to/file"

#endif // HYPERCOIN_ZMQ_ZMQUTIL_H
