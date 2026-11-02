# Copyright (c) 2023-present The Hypercoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

target_link_libraries(test_hypercoin hypercoin_ipc_test hypercoin_ipc)

add_boost_test(${CMAKE_CURRENT_SOURCE_DIR}/ipc_tests.cpp)
