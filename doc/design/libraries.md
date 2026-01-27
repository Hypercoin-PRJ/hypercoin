# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libhypercoin_cli*         | RPC client functionality used by *hypercoin-cli* executable |
| *libhypercoin_common*      | Home for common functionality shared by different executables and libraries. Similar to *libhypercoin_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libhypercoin_consensus*   | Consensus functionality used by *libhypercoin_node* and *libhypercoin_wallet*. |
| *libhypercoin_crypto*      | Hardware-optimized functions for data encryption, hashing, message authentication, and key derivation. |
| *libhypercoin_kernel*      | Consensus engine and support library used for validation by *libhypercoin_node*. |
| *libhypercoinqt*           | GUI functionality used by *hypercoin-qt* and *hypercoin-gui* executables. |
| *libhypercoin_ipc*         | IPC functionality used by *hypercoin-node* and *hypercoin-gui* executables to communicate when [`-DENABLE_IPC=ON`](multiprocess.md) is used. |
| *libhypercoin_node*        | P2P and RPC server functionality used by *hypercoind* and *hypercoin-qt* executables. |
| *libhypercoin_util*        | Home for common functionality shared by different executables and libraries. Similar to *libhypercoin_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libhypercoin_wallet*      | Wallet functionality used by *hypercoind* and *hypercoin-wallet* executables. |
| *libhypercoin_wallet_tool* | Lower-level wallet functionality used by *hypercoin-wallet* executable. |
| *libhypercoin_zmq*         | [ZeroMQ](../zmq.md) functionality used by *hypercoind* and *hypercoin-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. An exception is *libhypercoin_kernel*, which, at some future point, will have a documented external interface.

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`add_library(hypercoin_* ...)`](../../src/CMakeLists.txt) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libhypercoin_node* code lives in `src/node/` in the `node::` namespace
  - *libhypercoin_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libhypercoin_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libhypercoin_util* code lives in `src/util/` in the `util::` namespace
  - *libhypercoin_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

hypercoin-cli[hypercoin-cli]-->libhypercoin_cli;

hypercoind[hypercoind]-->libhypercoin_node;
hypercoind[hypercoind]-->libhypercoin_wallet;

hypercoin-qt[hypercoin-qt]-->libhypercoin_node;
hypercoin-qt[hypercoin-qt]-->libhypercoinqt;
hypercoin-qt[hypercoin-qt]-->libhypercoin_wallet;

hypercoin-wallet[hypercoin-wallet]-->libhypercoin_wallet;
hypercoin-wallet[hypercoin-wallet]-->libhypercoin_wallet_tool;

libhypercoin_cli-->libhypercoin_util;
libhypercoin_cli-->libhypercoin_common;

libhypercoin_consensus-->libhypercoin_crypto;

libhypercoin_common-->libhypercoin_consensus;
libhypercoin_common-->libhypercoin_crypto;
libhypercoin_common-->libhypercoin_util;

libhypercoin_kernel-->libhypercoin_consensus;
libhypercoin_kernel-->libhypercoin_crypto;
libhypercoin_kernel-->libhypercoin_util;

libhypercoin_node-->libhypercoin_consensus;
libhypercoin_node-->libhypercoin_crypto;
libhypercoin_node-->libhypercoin_kernel;
libhypercoin_node-->libhypercoin_common;
libhypercoin_node-->libhypercoin_util;

libhypercoinqt-->libhypercoin_common;
libhypercoinqt-->libhypercoin_util;

libhypercoin_util-->libhypercoin_crypto;

libhypercoin_wallet-->libhypercoin_common;
libhypercoin_wallet-->libhypercoin_crypto;
libhypercoin_wallet-->libhypercoin_util;

libhypercoin_wallet_tool-->libhypercoin_wallet;
libhypercoin_wallet_tool-->libhypercoin_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class hypercoin-qt,hypercoind,hypercoin-cli,hypercoin-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Crypto* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus, crypto, and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libhypercoin_wallet* and *libhypercoin_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libhypercoin_crypto* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libhypercoin_consensus* should only depend on *libhypercoin_crypto*, and all other libraries besides *libhypercoin_crypto* should be allowed to depend on it.

- *libhypercoin_util* should be a standalone dependency that any library can depend on, and it should not depend on other libraries except *libhypercoin_crypto*. It provides basic utilities that fill in gaps in the C++ standard library and provide lightweight abstractions over platform-specific features. Since the util library is distributed with the kernel and is usable by kernel applications, it shouldn't contain functions that external code shouldn't call, like higher level code targeted at the node or wallet. (*libhypercoin_common* is a better place for higher level code, or code that is meant to be used by internal applications only.)

- *libhypercoin_common* is a home for miscellaneous shared code used by different Hypercoin Core applications. It should not depend on anything other than *libhypercoin_util*, *libhypercoin_consensus*, and *libhypercoin_crypto*.

- *libhypercoin_kernel* should only depend on *libhypercoin_util*, *libhypercoin_consensus*, and *libhypercoin_crypto*.

- The only thing that should depend on *libhypercoin_kernel* internally should be *libhypercoin_node*. GUI and wallet libraries *libhypercoinqt* and *libhypercoin_wallet* in particular should not depend on *libhypercoin_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be able to get it from *libhypercoin_consensus*, *libhypercoin_common*, *libhypercoin_crypto*, and *libhypercoin_util*, instead of *libhypercoin_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libhypercoinqt*, *libhypercoin_node*, *libhypercoin_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](../../src/interfaces/) abstract interfaces.

## Work in progress

- Validation code is moving from *libhypercoin_node* to *libhypercoin_kernel* as part of [The libhypercoinkernel Project #27587](https://github.com/hypercoin/hypercoin/issues/27587)
