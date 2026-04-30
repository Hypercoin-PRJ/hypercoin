v30.2 Release Notes
===================

Hypercoin Core version v30.2 is now available from:

  <https://hypercoincore.org/bin/hypercoin-core-30.2/>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/hypercoin/hypercoin/issues>

To receive security and update notifications, please subscribe to:

  <https://hypercoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases), then run the
installer (on Windows) or just copy over `/Applications/Hypercoin-Qt` (on macOS)
or `hypercoind`/`hypercoin-qt` (on Linux).

Upgrading directly from a version of Hypercoin Core that has reached its EOL is
possible, but it might take some time if the data directory needs to be migrated. Old
wallet versions of Hypercoin Core are generally supported.

Compatibility
==============

Hypercoin Core is supported and tested on operating systems using the
Linux Kernel 3.17+, macOS 13+, and Windows 10+. Hypercoin
Core should also work on most other Unix-like systems but is not as
frequently tested on them. It is not recommended to use Hypercoin Core on
unsupported systems.

Notable changes
===============

### Wallet

- #34156 wallet: fix unnamed legacy wallet migration failure
- #34215 wallettool: fix unnamed createfromdump failure walletsdir deletion
- #34221 test: migration, avoid backup name mismatch in default_wallet_failure

### IPC

- #33511 init: Fix Ctrl-C shutdown hangs during wait calls

### Build

- #33950 guix: reduce allowed exported symbols
- #34107 build: Update minimum required Boost version
- #34227 guix: Fix osslsigncode tests

### Test

- #34137 test: Avoid hard time.sleep(1) in feature_init.py
- #34226 wallet: test: Relative wallet failed migration cleanup

### Fuzz

- #34091 fuzz: doc: remove any mention to address_deserialize_v2

### Doc

- #34182 doc: Update OpenBSD Build Guide

### Misc

- #34174 doc: update copyright year to 2026

Credits
=======

Thanks to everyone who directly contributed to this release:

- Ava Chow
- brunoerg
- davidgumberg
- fanquake
- furszy
- Hennadii Stepanov
- MarcoFalke
- Ryan Ofsky

As well as to everyone that helped with translations on
[Transifex](https://explore.transifex.com/hypercoin/hypercoin/).
