# Copyright (c) 2023-present The Hypercoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(CLIENT_TARNAME "hypercoin")
  set(HYPERCOIN_WRAPPER_NAME "hypercoin")
  set(HYPERCOIN_GUI_NAME "hypercoin-qt")
  set(HYPERCOIN_DAEMON_NAME "hypercoind")
  set(HYPERCOIN_CLI_NAME "hypercoin-cli")
  set(HYPERCOIN_TX_NAME "hypercoin-tx")
  set(HYPERCOIN_WALLET_TOOL_NAME "hypercoin-wallet")
  set(HYPERCOIN_TEST_NAME "test_hypercoin")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/hypercoin-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
