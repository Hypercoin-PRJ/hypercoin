// Copyright (c) 2022-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define HYPERCOINKERNEL_BUILD

#include <kernel/hypercoinkernel.h>

#include <chain.h>
#include <coins.h>
#include <consensus/validation.h>
#include <dbwrapper.h>
#include <kernel/caches.h>
#include <kernel/chainparams.h>
#include <kernel/checks.h>
#include <kernel/context.h>
#include <kernel/notifications_interface.h>
#include <kernel/warning.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <node/chainstate.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <undo.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/task_runner.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>

#include <cstddef>
#include <cstring>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using kernel::ChainstateRole;
using util::ImmediateTaskRunner;

// Define G_TRANSLATION_FUN symbol in libhypercoinkernel library so users of the
// library aren't required to export this symbol
extern const TranslateFn G_TRANSLATION_FUN{nullptr};

static const kernel::Context hrck_context_static{};

namespace {

bool is_valid_flag_combination(script_verify_flags flags)
{
    if (flags & SCRIPT_VERIFY_CLEANSTACK && ~flags & (SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS)) return false;
    if (flags & SCRIPT_VERIFY_WITNESS && ~flags & SCRIPT_VERIFY_P2SH) return false;
    return true;
}

class WriterStream
{
private:
    hrck_WriteBytes m_writer;
    void* m_user_data;

public:
    WriterStream(hrck_WriteBytes writer, void* user_data)
        : m_writer{writer}, m_user_data{user_data} {}

    //
    // Stream subset
    //
    void write(std::span<const std::byte> src)
    {
        if (m_writer(src.data(), src.size(), m_user_data) != 0) {
            throw std::runtime_error("Failed to write serialization data");
        }
    }

    template <typename T>
    WriterStream& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return *this;
    }
};

template <typename C, typename CPP>
struct Handle {
    static C* ref(CPP* cpp_type)
    {
        return reinterpret_cast<C*>(cpp_type);
    }

    static const C* ref(const CPP* cpp_type)
    {
        return reinterpret_cast<const C*>(cpp_type);
    }

    template <typename... Args>
    static C* create(Args&&... args)
    {
        auto cpp_obj{std::make_unique<CPP>(std::forward<Args>(args)...)};
        return ref(cpp_obj.release());
    }

    static C* copy(const C* ptr)
    {
        auto cpp_obj{std::make_unique<CPP>(get(ptr))};
        return ref(cpp_obj.release());
    }

    static const CPP& get(const C* ptr)
    {
        return *reinterpret_cast<const CPP*>(ptr);
    }

    static CPP& get(C* ptr)
    {
        return *reinterpret_cast<CPP*>(ptr);
    }

    static void operator delete(void* ptr)
    {
        delete reinterpret_cast<CPP*>(ptr);
    }
};

} // namespace

struct hrck_BlockTreeEntry: Handle<hrck_BlockTreeEntry, CBlockIndex> {};
struct hrck_Block : Handle<hrck_Block, std::shared_ptr<const CBlock>> {};
struct hrck_BlockValidationState : Handle<hrck_BlockValidationState, BlockValidationState> {};

namespace {

BCLog::Level get_bclog_level(hrck_LogLevel level)
{
    switch (level) {
    case hrck_LogLevel_INFO: {
        return BCLog::Level::Info;
    }
    case hrck_LogLevel_DEBUG: {
        return BCLog::Level::Debug;
    }
    case hrck_LogLevel_TRACE: {
        return BCLog::Level::Trace;
    }
    }
    assert(false);
}

BCLog::LogFlags get_bclog_flag(hrck_LogCategory category)
{
    switch (category) {
    case hrck_LogCategory_BENCH: {
        return BCLog::LogFlags::BENCH;
    }
    case hrck_LogCategory_BLOCKSTORAGE: {
        return BCLog::LogFlags::BLOCKSTORAGE;
    }
    case hrck_LogCategory_COINDB: {
        return BCLog::LogFlags::COINDB;
    }
    case hrck_LogCategory_LEVELDB: {
        return BCLog::LogFlags::LEVELDB;
    }
    case hrck_LogCategory_MEMPOOL: {
        return BCLog::LogFlags::MEMPOOL;
    }
    case hrck_LogCategory_PRUNE: {
        return BCLog::LogFlags::PRUNE;
    }
    case hrck_LogCategory_RAND: {
        return BCLog::LogFlags::RAND;
    }
    case hrck_LogCategory_REINDEX: {
        return BCLog::LogFlags::REINDEX;
    }
    case hrck_LogCategory_VALIDATION: {
        return BCLog::LogFlags::VALIDATION;
    }
    case hrck_LogCategory_KERNEL: {
        return BCLog::LogFlags::KERNEL;
    }
    case hrck_LogCategory_ALL: {
        return BCLog::LogFlags::ALL;
    }
    }
    assert(false);
}

hrck_SynchronizationState cast_state(SynchronizationState state)
{
    switch (state) {
    case SynchronizationState::INIT_REINDEX:
        return hrck_SynchronizationState_INIT_REINDEX;
    case SynchronizationState::INIT_DOWNLOAD:
        return hrck_SynchronizationState_INIT_DOWNLOAD;
    case SynchronizationState::POST_INIT:
        return hrck_SynchronizationState_POST_INIT;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

hrck_Warning cast_hrck_warning(kernel::Warning warning)
{
    switch (warning) {
    case kernel::Warning::UNKNOWN_NEW_RULES_ACTIVATED:
        return hrck_Warning_UNKNOWN_NEW_RULES_ACTIVATED;
    case kernel::Warning::LARGE_WORK_INVALID_CHAIN:
        return hrck_Warning_LARGE_WORK_INVALID_CHAIN;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

struct LoggingConnection {
    std::unique_ptr<std::list<std::function<void(const std::string&)>>::iterator> m_connection;
    void* m_user_data;
    std::function<void(void* user_data)> m_deleter;

    LoggingConnection(hrck_LogCallback callback, void* user_data, hrck_DestroyCallback user_data_destroy_callback)
    {
        LOCK(cs_main);

        auto connection{LogInstance().PushBackCallback([callback, user_data](const std::string& str) { callback(user_data, str.c_str(), str.length()); })};

        // Only start logging if we just added the connection.
        if (LogInstance().NumConnections() == 1 && !LogInstance().StartLogging()) {
            LogError("Logger start failed.");
            LogInstance().DeleteCallback(connection);
            if (user_data && user_data_destroy_callback) {
                user_data_destroy_callback(user_data);
            }
            throw std::runtime_error("Failed to start logging");
        }

        m_connection = std::make_unique<std::list<std::function<void(const std::string&)>>::iterator>(connection);
        m_user_data = user_data;
        m_deleter = user_data_destroy_callback;

        LogDebug(BCLog::KERNEL, "Logger connected.");
    }

    ~LoggingConnection()
    {
        LOCK(cs_main);
        LogDebug(BCLog::KERNEL, "Logger disconnecting.");

        // Switch back to buffering by calling DisconnectTestLogger if the
        // connection that we are about to remove is the last one.
        if (LogInstance().NumConnections() == 1) {
            LogInstance().DisconnectTestLogger();
        } else {
            LogInstance().DeleteCallback(*m_connection);
        }

        m_connection.reset();
        if (m_user_data && m_deleter) {
            m_deleter(m_user_data);
        }
    }
};

class KernelNotifications final : public kernel::Notifications
{
private:
    hrck_NotificationInterfaceCallbacks m_cbs;

public:
    KernelNotifications(hrck_NotificationInterfaceCallbacks cbs)
        : m_cbs{cbs}
    {
    }

    ~KernelNotifications()
    {
        if (m_cbs.user_data && m_cbs.user_data_destroy) {
            m_cbs.user_data_destroy(m_cbs.user_data);
        }
        m_cbs.user_data_destroy = nullptr;
        m_cbs.user_data = nullptr;
    }

    kernel::InterruptResult blockTip(SynchronizationState state, const CBlockIndex& index, double verification_progress) override
    {
        if (m_cbs.block_tip) m_cbs.block_tip(m_cbs.user_data, cast_state(state), hrck_BlockTreeEntry::ref(&index), verification_progress);
        return {};
    }
    void headerTip(SynchronizationState state, int64_t height, int64_t timestamp, bool presync) override
    {
        if (m_cbs.header_tip) m_cbs.header_tip(m_cbs.user_data, cast_state(state), height, timestamp, presync ? 1 : 0);
    }
    void progress(const bilingual_str& title, int progress_percent, bool resume_possible) override
    {
        if (m_cbs.progress) m_cbs.progress(m_cbs.user_data, title.original.c_str(), title.original.length(), progress_percent, resume_possible ? 1 : 0);
    }
    void warningSet(kernel::Warning id, const bilingual_str& message) override
    {
        if (m_cbs.warning_set) m_cbs.warning_set(m_cbs.user_data, cast_hrck_warning(id), message.original.c_str(), message.original.length());
    }
    void warningUnset(kernel::Warning id) override
    {
        if (m_cbs.warning_unset) m_cbs.warning_unset(m_cbs.user_data, cast_hrck_warning(id));
    }
    void flushError(const bilingual_str& message) override
    {
        if (m_cbs.flush_error) m_cbs.flush_error(m_cbs.user_data, message.original.c_str(), message.original.length());
    }
    void fatalError(const bilingual_str& message) override
    {
        if (m_cbs.fatal_error) m_cbs.fatal_error(m_cbs.user_data, message.original.c_str(), message.original.length());
    }
};

class KernelValidationInterface final : public CValidationInterface
{
public:
    hrck_ValidationInterfaceCallbacks m_cbs;

    explicit KernelValidationInterface(const hrck_ValidationInterfaceCallbacks vi_cbs) : m_cbs{vi_cbs} {}

    ~KernelValidationInterface()
    {
        if (m_cbs.user_data && m_cbs.user_data_destroy) {
            m_cbs.user_data_destroy(m_cbs.user_data);
        }
        m_cbs.user_data = nullptr;
        m_cbs.user_data_destroy = nullptr;
    }

protected:
    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& stateIn) override
    {
        if (m_cbs.block_checked) {
            m_cbs.block_checked(m_cbs.user_data,
                                hrck_Block::copy(hrck_Block::ref(&block)),
                                hrck_BlockValidationState::ref(&stateIn));
        }
    }

    void NewPoWValidBlock(const CBlockIndex* pindex, const std::shared_ptr<const CBlock>& block) override
    {
        if (m_cbs.pow_valid_block) {
            m_cbs.pow_valid_block(m_cbs.user_data,
                                  hrck_Block::copy(hrck_Block::ref(&block)),
                                  hrck_BlockTreeEntry::ref(pindex));
        }
    }

    void BlockConnected(const ChainstateRole& role, const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        if (m_cbs.block_connected) {
            m_cbs.block_connected(m_cbs.user_data,
                                  hrck_Block::copy(hrck_Block::ref(&block)),
                                  hrck_BlockTreeEntry::ref(pindex));
        }
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override
    {
        if (m_cbs.block_disconnected) {
            m_cbs.block_disconnected(m_cbs.user_data,
                                     hrck_Block::copy(hrck_Block::ref(&block)),
                                     hrck_BlockTreeEntry::ref(pindex));
        }
    }
};

struct ContextOptions {
    mutable Mutex m_mutex;
    std::unique_ptr<const CChainParams> m_chainparams GUARDED_BY(m_mutex);
    std::shared_ptr<KernelNotifications> m_notifications GUARDED_BY(m_mutex);
    std::shared_ptr<KernelValidationInterface> m_validation_interface GUARDED_BY(m_mutex);
};

class Context
{
public:
    std::unique_ptr<kernel::Context> m_context;

    std::shared_ptr<KernelNotifications> m_notifications;

    std::unique_ptr<util::SignalInterrupt> m_interrupt;

    std::unique_ptr<ValidationSignals> m_signals;

    std::unique_ptr<const CChainParams> m_chainparams;

    std::shared_ptr<KernelValidationInterface> m_validation_interface;

    Context(const ContextOptions* options, bool& sane)
        : m_context{std::make_unique<kernel::Context>()},
          m_interrupt{std::make_unique<util::SignalInterrupt>()}
    {
        if (options) {
            LOCK(options->m_mutex);
            if (options->m_chainparams) {
                m_chainparams = std::make_unique<const CChainParams>(*options->m_chainparams);
            }
            if (options->m_notifications) {
                m_notifications = options->m_notifications;
            }
            if (options->m_validation_interface) {
                m_signals = std::make_unique<ValidationSignals>(std::make_unique<ImmediateTaskRunner>());
                m_validation_interface = options->m_validation_interface;
                m_signals->RegisterSharedValidationInterface(m_validation_interface);
            }
        }

        if (!m_chainparams) {
            m_chainparams = CChainParams::Main();
        }
        if (!m_notifications) {
            m_notifications = std::make_shared<KernelNotifications>(hrck_NotificationInterfaceCallbacks{
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
        }

        if (!kernel::SanityChecks(*m_context)) {
            sane = false;
        }
    }

    ~Context()
    {
        if (m_signals) {
            m_signals->UnregisterSharedValidationInterface(m_validation_interface);
        }
    }
};

//! Helper struct to wrap the ChainstateManager-related Options
struct ChainstateManagerOptions {
    mutable Mutex m_mutex;
    ChainstateManager::Options m_chainman_options GUARDED_BY(m_mutex);
    node::BlockManager::Options m_blockman_options GUARDED_BY(m_mutex);
    std::shared_ptr<const Context> m_context;
    node::ChainstateLoadOptions m_chainstate_load_options GUARDED_BY(m_mutex);

    ChainstateManagerOptions(const std::shared_ptr<const Context>& context, const fs::path& data_dir, const fs::path& blocks_dir)
        : m_chainman_options{ChainstateManager::Options{
              .chainparams = *context->m_chainparams,
              .datadir = data_dir,
              .notifications = *context->m_notifications,
              .signals = context->m_signals.get()}},
          m_blockman_options{node::BlockManager::Options{
              .chainparams = *context->m_chainparams,
              .blocks_dir = blocks_dir,
              .notifications = *context->m_notifications,
              .block_tree_db_params = DBParams{
                  .path = data_dir / "blocks" / "index",
                  .cache_bytes = kernel::CacheSizes{DEFAULT_KERNEL_CACHE}.block_tree_db,
              }}},
          m_context{context}, m_chainstate_load_options{node::ChainstateLoadOptions{}}
    {
    }
};

struct ChainMan {
    std::unique_ptr<ChainstateManager> m_chainman;
    std::shared_ptr<const Context> m_context;

    ChainMan(std::unique_ptr<ChainstateManager> chainman, std::shared_ptr<const Context> context)
        : m_chainman(std::move(chainman)), m_context(std::move(context)) {}
};

} // namespace

struct hrck_Transaction : Handle<hrck_Transaction, std::shared_ptr<const CTransaction>> {};
struct hrck_TransactionOutput : Handle<hrck_TransactionOutput, CTxOut> {};
struct hrck_ScriptPubkey : Handle<hrck_ScriptPubkey, CScript> {};
struct hrck_LoggingConnection : Handle<hrck_LoggingConnection, LoggingConnection> {};
struct hrck_ContextOptions : Handle<hrck_ContextOptions, ContextOptions> {};
struct hrck_Context : Handle<hrck_Context, std::shared_ptr<const Context>> {};
struct hrck_ChainParameters : Handle<hrck_ChainParameters, CChainParams> {};
struct hrck_ChainstateManagerOptions : Handle<hrck_ChainstateManagerOptions, ChainstateManagerOptions> {};
struct hrck_ChainstateManager : Handle<hrck_ChainstateManager, ChainMan> {};
struct hrck_Chain : Handle<hrck_Chain, CChain> {};
struct hrck_BlockSpentOutputs : Handle<hrck_BlockSpentOutputs, std::shared_ptr<CBlockUndo>> {};
struct hrck_TransactionSpentOutputs : Handle<hrck_TransactionSpentOutputs, CTxUndo> {};
struct hrck_Coin : Handle<hrck_Coin, Coin> {};
struct hrck_BlockHash : Handle<hrck_BlockHash, uint256> {};
struct hrck_TransactionInput : Handle<hrck_TransactionInput, CTxIn> {};
struct hrck_TransactionOutPoint: Handle<hrck_TransactionOutPoint, COutPoint> {};
struct hrck_Txid: Handle<hrck_Txid, Txid> {};
struct hrck_PrecomputedTransactionData : Handle<hrck_PrecomputedTransactionData, PrecomputedTransactionData> {};
struct hrck_BlockHeader: Handle<hrck_BlockHeader, CBlockHeader> {};

hrck_Transaction* hrck_transaction_create(const void* raw_transaction, size_t raw_transaction_len)
{
    if (raw_transaction == nullptr && raw_transaction_len != 0) {
        return nullptr;
    }
    try {
        SpanReader stream{std::span{reinterpret_cast<const std::byte*>(raw_transaction), raw_transaction_len}};
        return hrck_Transaction::create(std::make_shared<const CTransaction>(deserialize, TX_WITH_WITNESS, stream));
    } catch (...) {
        return nullptr;
    }
}

size_t hrck_transaction_count_outputs(const hrck_Transaction* transaction)
{
    return hrck_Transaction::get(transaction)->vout.size();
}

const hrck_TransactionOutput* hrck_transaction_get_output_at(const hrck_Transaction* transaction, size_t output_index)
{
    const CTransaction& tx = *hrck_Transaction::get(transaction);
    assert(output_index < tx.vout.size());
    return hrck_TransactionOutput::ref(&tx.vout[output_index]);
}

size_t hrck_transaction_count_inputs(const hrck_Transaction* transaction)
{
    return hrck_Transaction::get(transaction)->vin.size();
}

const hrck_TransactionInput* hrck_transaction_get_input_at(const hrck_Transaction* transaction, size_t input_index)
{
    assert(input_index < hrck_Transaction::get(transaction)->vin.size());
    return hrck_TransactionInput::ref(&hrck_Transaction::get(transaction)->vin[input_index]);
}

const hrck_Txid* hrck_transaction_get_txid(const hrck_Transaction* transaction)
{
    return hrck_Txid::ref(&hrck_Transaction::get(transaction)->GetHash());
}

hrck_Transaction* hrck_transaction_copy(const hrck_Transaction* transaction)
{
    return hrck_Transaction::copy(transaction);
}

int hrck_transaction_to_bytes(const hrck_Transaction* transaction, hrck_WriteBytes writer, void* user_data)
{
    try {
        WriterStream ws{writer, user_data};
        ws << TX_WITH_WITNESS(hrck_Transaction::get(transaction));
        return 0;
    } catch (...) {
        return -1;
    }
}

void hrck_transaction_destroy(hrck_Transaction* transaction)
{
    delete transaction;
}

hrck_ScriptPubkey* hrck_script_pubkey_create(const void* script_pubkey, size_t script_pubkey_len)
{
    if (script_pubkey == nullptr && script_pubkey_len != 0) {
        return nullptr;
    }
    auto data = std::span{reinterpret_cast<const uint8_t*>(script_pubkey), script_pubkey_len};
    return hrck_ScriptPubkey::create(data.begin(), data.end());
}

int hrck_script_pubkey_to_bytes(const hrck_ScriptPubkey* script_pubkey_, hrck_WriteBytes writer, void* user_data)
{
    const auto& script_pubkey{hrck_ScriptPubkey::get(script_pubkey_)};
    return writer(script_pubkey.data(), script_pubkey.size(), user_data);
}

hrck_ScriptPubkey* hrck_script_pubkey_copy(const hrck_ScriptPubkey* script_pubkey)
{
    return hrck_ScriptPubkey::copy(script_pubkey);
}

void hrck_script_pubkey_destroy(hrck_ScriptPubkey* script_pubkey)
{
    delete script_pubkey;
}

hrck_TransactionOutput* hrck_transaction_output_create(const hrck_ScriptPubkey* script_pubkey, int64_t amount)
{
    return hrck_TransactionOutput::create(amount, hrck_ScriptPubkey::get(script_pubkey));
}

hrck_TransactionOutput* hrck_transaction_output_copy(const hrck_TransactionOutput* output)
{
    return hrck_TransactionOutput::copy(output);
}

const hrck_ScriptPubkey* hrck_transaction_output_get_script_pubkey(const hrck_TransactionOutput* output)
{
    return hrck_ScriptPubkey::ref(&hrck_TransactionOutput::get(output).scriptPubKey);
}

int64_t hrck_transaction_output_get_amount(const hrck_TransactionOutput* output)
{
    return hrck_TransactionOutput::get(output).nValue;
}

void hrck_transaction_output_destroy(hrck_TransactionOutput* output)
{
    delete output;
}

hrck_PrecomputedTransactionData* hrck_precomputed_transaction_data_create(
    const hrck_Transaction* tx_to,
    const hrck_TransactionOutput** spent_outputs_, size_t spent_outputs_len)
{
    try {
        const CTransaction& tx{*hrck_Transaction::get(tx_to)};
        auto txdata{hrck_PrecomputedTransactionData::create()};
        if (spent_outputs_ != nullptr && spent_outputs_len > 0) {
            assert(spent_outputs_len == tx.vin.size());
            std::vector<CTxOut> spent_outputs;
            spent_outputs.reserve(spent_outputs_len);
            for (size_t i = 0; i < spent_outputs_len; i++) {
                const CTxOut& tx_out{hrck_TransactionOutput::get(spent_outputs_[i])};
                spent_outputs.push_back(tx_out);
            }
            hrck_PrecomputedTransactionData::get(txdata).Init(tx, std::move(spent_outputs));
        } else {
            hrck_PrecomputedTransactionData::get(txdata).Init(tx, {});
        }

        return txdata;
    } catch (...) {
        return nullptr;
    }
}

hrck_PrecomputedTransactionData* hrck_precomputed_transaction_data_copy(const hrck_PrecomputedTransactionData* precomputed_txdata)
{
    return hrck_PrecomputedTransactionData::copy(precomputed_txdata);
}

void hrck_precomputed_transaction_data_destroy(hrck_PrecomputedTransactionData* precomputed_txdata)
{
    delete precomputed_txdata;
}

int hrck_script_pubkey_verify(const hrck_ScriptPubkey* script_pubkey,
                              const int64_t amount,
                              const hrck_Transaction* tx_to,
                              const hrck_PrecomputedTransactionData* precomputed_txdata,
                              const unsigned int input_index,
                              const hrck_ScriptVerificationFlags flags,
                              hrck_ScriptVerifyStatus* status)
{
    // Assert that all specified flags are part of the interface before continuing
    assert((flags & ~hrck_ScriptVerificationFlags_ALL) == 0);

    if (!is_valid_flag_combination(script_verify_flags::from_int(flags))) {
        if (status) *status = hrck_ScriptVerifyStatus_ERROR_INVALID_FLAGS_COMBINATION;
        return 0;
    }

    const CTransaction& tx{*hrck_Transaction::get(tx_to)};
    assert(input_index < tx.vin.size());

    const PrecomputedTransactionData& txdata{precomputed_txdata ? hrck_PrecomputedTransactionData::get(precomputed_txdata) : PrecomputedTransactionData(tx)};

    if (flags & hrck_ScriptVerificationFlags_TAPROOT && txdata.m_spent_outputs.empty()) {
        if (status) *status = hrck_ScriptVerifyStatus_ERROR_SPENT_OUTPUTS_REQUIRED;
        return 0;
    }

    if (status) *status = hrck_ScriptVerifyStatus_OK;

    bool result = VerifyScript(tx.vin[input_index].scriptSig,
                               hrck_ScriptPubkey::get(script_pubkey),
                               &tx.vin[input_index].scriptWitness,
                               script_verify_flags::from_int(flags),
                               TransactionSignatureChecker(&tx, input_index, amount, txdata, MissingDataBehavior::FAIL),
                               nullptr);
    return result ? 1 : 0;
}

hrck_TransactionInput* hrck_transaction_input_copy(const hrck_TransactionInput* input)
{
    return hrck_TransactionInput::copy(input);
}

const hrck_TransactionOutPoint* hrck_transaction_input_get_out_point(const hrck_TransactionInput* input)
{
    return hrck_TransactionOutPoint::ref(&hrck_TransactionInput::get(input).prevout);
}

void hrck_transaction_input_destroy(hrck_TransactionInput* input)
{
    delete input;
}

hrck_TransactionOutPoint* hrck_transaction_out_point_copy(const hrck_TransactionOutPoint* out_point)
{
    return hrck_TransactionOutPoint::copy(out_point);
}

uint32_t hrck_transaction_out_point_get_index(const hrck_TransactionOutPoint* out_point)
{
    return hrck_TransactionOutPoint::get(out_point).n;
}

const hrck_Txid* hrck_transaction_out_point_get_txid(const hrck_TransactionOutPoint* out_point)
{
    return hrck_Txid::ref(&hrck_TransactionOutPoint::get(out_point).hash);
}

void hrck_transaction_out_point_destroy(hrck_TransactionOutPoint* out_point)
{
    delete out_point;
}

hrck_Txid* hrck_txid_copy(const hrck_Txid* txid)
{
    return hrck_Txid::copy(txid);
}

void hrck_txid_to_bytes(const hrck_Txid* txid, unsigned char output[32])
{
    std::memcpy(output, hrck_Txid::get(txid).begin(), 32);
}

int hrck_txid_equals(const hrck_Txid* txid1, const hrck_Txid* txid2)
{
    return hrck_Txid::get(txid1) == hrck_Txid::get(txid2);
}

void hrck_txid_destroy(hrck_Txid* txid)
{
    delete txid;
}

void hrck_logging_set_options(const hrck_LoggingOptions options)
{
    LOCK(cs_main);
    LogInstance().m_log_timestamps = options.log_timestamps;
    LogInstance().m_log_time_micros = options.log_time_micros;
    LogInstance().m_log_threadnames = options.log_threadnames;
    LogInstance().m_log_sourcelocations = options.log_sourcelocations;
    LogInstance().m_always_print_category_level = options.always_print_category_levels;
}

void hrck_logging_set_level_category(hrck_LogCategory category, hrck_LogLevel level)
{
    LOCK(cs_main);
    if (category == hrck_LogCategory_ALL) {
        LogInstance().SetLogLevel(get_bclog_level(level));
    }

    LogInstance().AddCategoryLogLevel(get_bclog_flag(category), get_bclog_level(level));
}

void hrck_logging_enable_category(hrck_LogCategory category)
{
    LogInstance().EnableCategory(get_bclog_flag(category));
}

void hrck_logging_disable_category(hrck_LogCategory category)
{
    LogInstance().DisableCategory(get_bclog_flag(category));
}

void hrck_logging_disable()
{
    LogInstance().DisableLogging();
}

hrck_LoggingConnection* hrck_logging_connection_create(hrck_LogCallback callback, void* user_data, hrck_DestroyCallback user_data_destroy_callback)
{
    try {
        return hrck_LoggingConnection::create(callback, user_data, user_data_destroy_callback);
    } catch (const std::exception&) {
        return nullptr;
    }
}

void hrck_logging_connection_destroy(hrck_LoggingConnection* connection)
{
    delete connection;
}

hrck_ChainParameters* hrck_chain_parameters_create(const hrck_ChainType chain_type)
{
    switch (chain_type) {
    case hrck_ChainType_MAINNET: {
        return hrck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::Main().release()));
    }
    case hrck_ChainType_TESTNET: {
        return hrck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet().release()));
    }
    case hrck_ChainType_TESTNET_4: {
        return hrck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::TestNet4().release()));
    }
    case hrck_ChainType_SIGNET: {
        return hrck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::SigNet({}).release()));
    }
    case hrck_ChainType_REGTEST: {
        return hrck_ChainParameters::ref(const_cast<CChainParams*>(CChainParams::RegTest({}).release()));
    }
    }
    assert(false);
}

hrck_ChainParameters* hrck_chain_parameters_copy(const hrck_ChainParameters* chain_parameters)
{
    return hrck_ChainParameters::copy(chain_parameters);
}

void hrck_chain_parameters_destroy(hrck_ChainParameters* chain_parameters)
{
    delete chain_parameters;
}

hrck_ContextOptions* hrck_context_options_create()
{
    return hrck_ContextOptions::create();
}

void hrck_context_options_set_chainparams(hrck_ContextOptions* options, const hrck_ChainParameters* chain_parameters)
{
    // Copy the chainparams, so the caller can free it again
    LOCK(hrck_ContextOptions::get(options).m_mutex);
    hrck_ContextOptions::get(options).m_chainparams = std::make_unique<const CChainParams>(hrck_ChainParameters::get(chain_parameters));
}

void hrck_context_options_set_notifications(hrck_ContextOptions* options, hrck_NotificationInterfaceCallbacks notifications)
{
    // The KernelNotifications are copy-initialized, so the caller can free them again.
    LOCK(hrck_ContextOptions::get(options).m_mutex);
    hrck_ContextOptions::get(options).m_notifications = std::make_shared<KernelNotifications>(notifications);
}

void hrck_context_options_set_validation_interface(hrck_ContextOptions* options, hrck_ValidationInterfaceCallbacks vi_cbs)
{
    LOCK(hrck_ContextOptions::get(options).m_mutex);
    hrck_ContextOptions::get(options).m_validation_interface = std::make_shared<KernelValidationInterface>(vi_cbs);
}

void hrck_context_options_destroy(hrck_ContextOptions* options)
{
    delete options;
}

hrck_Context* hrck_context_create(const hrck_ContextOptions* options)
{
    bool sane{true};
    const ContextOptions* opts = options ? &hrck_ContextOptions::get(options) : nullptr;
    auto context{std::make_shared<const Context>(opts, sane)};
    if (!sane) {
        LogError("Kernel context sanity check failed.");
        return nullptr;
    }
    return hrck_Context::create(context);
}

hrck_Context* hrck_context_copy(const hrck_Context* context)
{
    return hrck_Context::copy(context);
}

int hrck_context_interrupt(hrck_Context* context)
{
    return (*hrck_Context::get(context)->m_interrupt)() ? 0 : -1;
}

void hrck_context_destroy(hrck_Context* context)
{
    delete context;
}

const hrck_BlockTreeEntry* hrck_block_tree_entry_get_previous(const hrck_BlockTreeEntry* entry)
{
    if (!hrck_BlockTreeEntry::get(entry).pprev) {
        LogInfo("Genesis block has no previous.");
        return nullptr;
    }

    return hrck_BlockTreeEntry::ref(hrck_BlockTreeEntry::get(entry).pprev);
}

hrck_BlockValidationState* hrck_block_validation_state_create()
{
    return hrck_BlockValidationState::create();
}

hrck_BlockValidationState* hrck_block_validation_state_copy(const hrck_BlockValidationState* state)
{
    return hrck_BlockValidationState::copy(state);
}

void hrck_block_validation_state_destroy(hrck_BlockValidationState* state)
{
    delete state;
}

hrck_ValidationMode hrck_block_validation_state_get_validation_mode(const hrck_BlockValidationState* block_validation_state_)
{
    auto& block_validation_state = hrck_BlockValidationState::get(block_validation_state_);
    if (block_validation_state.IsValid()) return hrck_ValidationMode_VALID;
    if (block_validation_state.IsInvalid()) return hrck_ValidationMode_INVALID;
    return hrck_ValidationMode_INTERNAL_ERROR;
}

hrck_BlockValidationResult hrck_block_validation_state_get_block_validation_result(const hrck_BlockValidationState* block_validation_state_)
{
    auto& block_validation_state = hrck_BlockValidationState::get(block_validation_state_);
    switch (block_validation_state.GetResult()) {
    case BlockValidationResult::BLOCK_RESULT_UNSET:
        return hrck_BlockValidationResult_UNSET;
    case BlockValidationResult::BLOCK_CONSENSUS:
        return hrck_BlockValidationResult_CONSENSUS;
    case BlockValidationResult::BLOCK_CACHED_INVALID:
        return hrck_BlockValidationResult_CACHED_INVALID;
    case BlockValidationResult::BLOCK_INVALID_HEADER:
        return hrck_BlockValidationResult_INVALID_HEADER;
    case BlockValidationResult::BLOCK_MUTATED:
        return hrck_BlockValidationResult_MUTATED;
    case BlockValidationResult::BLOCK_MISSING_PREV:
        return hrck_BlockValidationResult_MISSING_PREV;
    case BlockValidationResult::BLOCK_INVALID_PREV:
        return hrck_BlockValidationResult_INVALID_PREV;
    case BlockValidationResult::BLOCK_TIME_FUTURE:
        return hrck_BlockValidationResult_TIME_FUTURE;
    case BlockValidationResult::BLOCK_HEADER_LOW_WORK:
        return hrck_BlockValidationResult_HEADER_LOW_WORK;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

hrck_ChainstateManagerOptions* hrck_chainstate_manager_options_create(const hrck_Context* context, const char* data_dir, size_t data_dir_len, const char* blocks_dir, size_t blocks_dir_len)
{
    if (data_dir == nullptr || data_dir_len == 0 || blocks_dir == nullptr || blocks_dir_len == 0) {
        LogError("Failed to create chainstate manager options: dir must be non-null and non-empty");
        return nullptr;
    }
    try {
        fs::path abs_data_dir{fs::absolute(fs::PathFromString({data_dir, data_dir_len}))};
        fs::create_directories(abs_data_dir);
        fs::path abs_blocks_dir{fs::absolute(fs::PathFromString({blocks_dir, blocks_dir_len}))};
        fs::create_directories(abs_blocks_dir);
        return hrck_ChainstateManagerOptions::create(hrck_Context::get(context), abs_data_dir, abs_blocks_dir);
    } catch (const std::exception& e) {
        LogError("Failed to create chainstate manager options: %s", e.what());
        return nullptr;
    }
}

void hrck_chainstate_manager_options_set_worker_threads_num(hrck_ChainstateManagerOptions* opts, int worker_threads)
{
    LOCK(hrck_ChainstateManagerOptions::get(opts).m_mutex);
    hrck_ChainstateManagerOptions::get(opts).m_chainman_options.worker_threads_num = worker_threads;
}

void hrck_chainstate_manager_options_destroy(hrck_ChainstateManagerOptions* options)
{
    delete options;
}

int hrck_chainstate_manager_options_set_wipe_dbs(hrck_ChainstateManagerOptions* chainman_opts, int wipe_block_tree_db, int wipe_chainstate_db)
{
    if (wipe_block_tree_db == 1 && wipe_chainstate_db != 1) {
        LogError("Wiping the block tree db without also wiping the chainstate db is currently unsupported.");
        return -1;
    }
    auto& opts{hrck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_blockman_options.block_tree_db_params.wipe_data = wipe_block_tree_db == 1;
    opts.m_chainstate_load_options.wipe_chainstate_db = wipe_chainstate_db == 1;
    return 0;
}

void hrck_chainstate_manager_options_update_block_tree_db_in_memory(
    hrck_ChainstateManagerOptions* chainman_opts,
    int block_tree_db_in_memory)
{
    auto& opts{hrck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_blockman_options.block_tree_db_params.memory_only = block_tree_db_in_memory == 1;
}

void hrck_chainstate_manager_options_update_chainstate_db_in_memory(
    hrck_ChainstateManagerOptions* chainman_opts,
    int chainstate_db_in_memory)
{
    auto& opts{hrck_ChainstateManagerOptions::get(chainman_opts)};
    LOCK(opts.m_mutex);
    opts.m_chainstate_load_options.coins_db_in_memory = chainstate_db_in_memory == 1;
}

hrck_ChainstateManager* hrck_chainstate_manager_create(
    const hrck_ChainstateManagerOptions* chainman_opts)
{
    auto& opts{hrck_ChainstateManagerOptions::get(chainman_opts)};
    std::unique_ptr<ChainstateManager> chainman;
    try {
        LOCK(opts.m_mutex);
        chainman = std::make_unique<ChainstateManager>(*opts.m_context->m_interrupt, opts.m_chainman_options, opts.m_blockman_options);
    } catch (const std::exception& e) {
        LogError("Failed to create chainstate manager: %s", e.what());
        return nullptr;
    }

    try {
        const auto chainstate_load_opts{WITH_LOCK(opts.m_mutex, return opts.m_chainstate_load_options)};

        kernel::CacheSizes cache_sizes{DEFAULT_KERNEL_CACHE};
        auto [status, chainstate_err]{node::LoadChainstate(*chainman, cache_sizes, chainstate_load_opts)};
        if (status != node::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to load chain state from your data directory: %s", chainstate_err.original);
            return nullptr;
        }
        std::tie(status, chainstate_err) = node::VerifyLoadedChainstate(*chainman, chainstate_load_opts);
        if (status != node::ChainstateLoadStatus::SUCCESS) {
            LogError("Failed to verify loaded chain state from your datadir: %s", chainstate_err.original);
            return nullptr;
        }
        if (auto result = chainman->ActivateBestChains(); !result) {
            LogError("%s", util::ErrorString(result).original);
            return nullptr;
        }
    } catch (const std::exception& e) {
        LogError("Failed to load chainstate: %s", e.what());
        return nullptr;
    }

    return hrck_ChainstateManager::create(std::move(chainman), opts.m_context);
}

const hrck_BlockTreeEntry* hrck_chainstate_manager_get_block_tree_entry_by_hash(const hrck_ChainstateManager* chainman, const hrck_BlockHash* block_hash)
{
    auto block_index = WITH_LOCK(hrck_ChainstateManager::get(chainman).m_chainman->GetMutex(),
                                 return hrck_ChainstateManager::get(chainman).m_chainman->m_blockman.LookupBlockIndex(hrck_BlockHash::get(block_hash)));
    if (!block_index) {
        LogDebug(BCLog::KERNEL, "A block with the given hash is not indexed.");
        return nullptr;
    }
    return hrck_BlockTreeEntry::ref(block_index);
}

const hrck_BlockTreeEntry* hrck_chainstate_manager_get_best_entry(const hrck_ChainstateManager* chainstate_manager)
{
    auto& chainman = *hrck_ChainstateManager::get(chainstate_manager).m_chainman;
    return hrck_BlockTreeEntry::ref(WITH_LOCK(chainman.GetMutex(), return chainman.m_best_header));
}

void hrck_chainstate_manager_destroy(hrck_ChainstateManager* chainman)
{
    {
        LOCK(hrck_ChainstateManager::get(chainman).m_chainman->GetMutex());
        for (const auto& chainstate : hrck_ChainstateManager::get(chainman).m_chainman->m_chainstates) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
                chainstate->ResetCoinsViews();
            }
        }
    }

    delete chainman;
}

int hrck_chainstate_manager_import_blocks(hrck_ChainstateManager* chainman, const char** block_file_paths_data, size_t* block_file_paths_lens, size_t block_file_paths_data_len)
{
    try {
        std::vector<fs::path> import_files;
        import_files.reserve(block_file_paths_data_len);
        for (uint32_t i = 0; i < block_file_paths_data_len; i++) {
            if (block_file_paths_data[i] != nullptr) {
                import_files.emplace_back(std::string{block_file_paths_data[i], block_file_paths_lens[i]}.c_str());
            }
        }
        auto& chainman_ref{*hrck_ChainstateManager::get(chainman).m_chainman};
        node::ImportBlocks(chainman_ref, import_files);
        WITH_LOCK(::cs_main, chainman_ref.UpdateIBDStatus());
    } catch (const std::exception& e) {
        LogError("Failed to import blocks: %s", e.what());
        return -1;
    }
    return 0;
}

hrck_Block* hrck_block_create(const void* raw_block, size_t raw_block_length)
{
    if (raw_block == nullptr && raw_block_length != 0) {
        return nullptr;
    }
    auto block{std::make_shared<CBlock>()};

    SpanReader stream{std::span{reinterpret_cast<const std::byte*>(raw_block), raw_block_length}};

    try {
        stream >> TX_WITH_WITNESS(*block);
    } catch (...) {
        LogDebug(BCLog::KERNEL, "Block decode failed.");
        return nullptr;
    }

    return hrck_Block::create(block);
}

hrck_Block* hrck_block_copy(const hrck_Block* block)
{
    return hrck_Block::copy(block);
}

size_t hrck_block_count_transactions(const hrck_Block* block)
{
    return hrck_Block::get(block)->vtx.size();
}

const hrck_Transaction* hrck_block_get_transaction_at(const hrck_Block* block, size_t index)
{
    assert(index < hrck_Block::get(block)->vtx.size());
    return hrck_Transaction::ref(&hrck_Block::get(block)->vtx[index]);
}

hrck_BlockHeader* hrck_block_get_header(const hrck_Block* block)
{
    const auto& block_ptr = hrck_Block::get(block);
    return hrck_BlockHeader::create(static_cast<const CBlockHeader&>(*block_ptr));
}

int hrck_block_to_bytes(const hrck_Block* block, hrck_WriteBytes writer, void* user_data)
{
    try {
        WriterStream ws{writer, user_data};
        ws << TX_WITH_WITNESS(*hrck_Block::get(block));
        return 0;
    } catch (...) {
        return -1;
    }
}

hrck_BlockHash* hrck_block_get_hash(const hrck_Block* block)
{
    return hrck_BlockHash::create(hrck_Block::get(block)->GetHash());
}

void hrck_block_destroy(hrck_Block* block)
{
    delete block;
}

hrck_Block* hrck_block_read(const hrck_ChainstateManager* chainman, const hrck_BlockTreeEntry* entry)
{
    auto block{std::make_shared<CBlock>()};
    if (!hrck_ChainstateManager::get(chainman).m_chainman->m_blockman.ReadBlock(*block, hrck_BlockTreeEntry::get(entry))) {
        LogError("Failed to read block.");
        return nullptr;
    }
    return hrck_Block::create(block);
}

hrck_BlockHeader* hrck_block_tree_entry_get_block_header(const hrck_BlockTreeEntry* entry)
{
    return hrck_BlockHeader::create(hrck_BlockTreeEntry::get(entry).GetBlockHeader());
}

int32_t hrck_block_tree_entry_get_height(const hrck_BlockTreeEntry* entry)
{
    return hrck_BlockTreeEntry::get(entry).nHeight;
}

const hrck_BlockHash* hrck_block_tree_entry_get_block_hash(const hrck_BlockTreeEntry* entry)
{
    return hrck_BlockHash::ref(hrck_BlockTreeEntry::get(entry).phashBlock);
}

int hrck_block_tree_entry_equals(const hrck_BlockTreeEntry* entry1, const hrck_BlockTreeEntry* entry2)
{
    return &hrck_BlockTreeEntry::get(entry1) == &hrck_BlockTreeEntry::get(entry2);
}

hrck_BlockHash* hrck_block_hash_create(const unsigned char block_hash[32])
{
    return hrck_BlockHash::create(std::span<const unsigned char>{block_hash, 32});
}

hrck_BlockHash* hrck_block_hash_copy(const hrck_BlockHash* block_hash)
{
    return hrck_BlockHash::copy(block_hash);
}

void hrck_block_hash_to_bytes(const hrck_BlockHash* block_hash, unsigned char output[32])
{
    std::memcpy(output, hrck_BlockHash::get(block_hash).begin(), 32);
}

int hrck_block_hash_equals(const hrck_BlockHash* hash1, const hrck_BlockHash* hash2)
{
    return hrck_BlockHash::get(hash1) == hrck_BlockHash::get(hash2);
}

void hrck_block_hash_destroy(hrck_BlockHash* hash)
{
    delete hash;
}

hrck_BlockSpentOutputs* hrck_block_spent_outputs_read(const hrck_ChainstateManager* chainman, const hrck_BlockTreeEntry* entry)
{
    auto block_undo{std::make_shared<CBlockUndo>()};
    if (hrck_BlockTreeEntry::get(entry).nHeight < 1) {
        LogDebug(BCLog::KERNEL, "The genesis block does not have any spent outputs.");
        return hrck_BlockSpentOutputs::create(block_undo);
    }
    if (!hrck_ChainstateManager::get(chainman).m_chainman->m_blockman.ReadBlockUndo(*block_undo, hrck_BlockTreeEntry::get(entry))) {
        LogError("Failed to read block spent outputs data.");
        return nullptr;
    }
    return hrck_BlockSpentOutputs::create(block_undo);
}

hrck_BlockSpentOutputs* hrck_block_spent_outputs_copy(const hrck_BlockSpentOutputs* block_spent_outputs)
{
    return hrck_BlockSpentOutputs::copy(block_spent_outputs);
}

size_t hrck_block_spent_outputs_count(const hrck_BlockSpentOutputs* block_spent_outputs)
{
    return hrck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.size();
}

const hrck_TransactionSpentOutputs* hrck_block_spent_outputs_get_transaction_spent_outputs_at(const hrck_BlockSpentOutputs* block_spent_outputs, size_t transaction_index)
{
    assert(transaction_index < hrck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.size());
    const auto* tx_undo{&hrck_BlockSpentOutputs::get(block_spent_outputs)->vtxundo.at(transaction_index)};
    return hrck_TransactionSpentOutputs::ref(tx_undo);
}

void hrck_block_spent_outputs_destroy(hrck_BlockSpentOutputs* block_spent_outputs)
{
    delete block_spent_outputs;
}

hrck_TransactionSpentOutputs* hrck_transaction_spent_outputs_copy(const hrck_TransactionSpentOutputs* transaction_spent_outputs)
{
    return hrck_TransactionSpentOutputs::copy(transaction_spent_outputs);
}

size_t hrck_transaction_spent_outputs_count(const hrck_TransactionSpentOutputs* transaction_spent_outputs)
{
    return hrck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.size();
}

void hrck_transaction_spent_outputs_destroy(hrck_TransactionSpentOutputs* transaction_spent_outputs)
{
    delete transaction_spent_outputs;
}

const hrck_Coin* hrck_transaction_spent_outputs_get_coin_at(const hrck_TransactionSpentOutputs* transaction_spent_outputs, size_t coin_index)
{
    assert(coin_index < hrck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.size());
    const Coin* coin{&hrck_TransactionSpentOutputs::get(transaction_spent_outputs).vprevout.at(coin_index)};
    return hrck_Coin::ref(coin);
}

hrck_Coin* hrck_coin_copy(const hrck_Coin* coin)
{
    return hrck_Coin::copy(coin);
}

uint32_t hrck_coin_confirmation_height(const hrck_Coin* coin)
{
    return hrck_Coin::get(coin).nHeight;
}

int hrck_coin_is_coinbase(const hrck_Coin* coin)
{
    return hrck_Coin::get(coin).IsCoinBase() ? 1 : 0;
}

const hrck_TransactionOutput* hrck_coin_get_output(const hrck_Coin* coin)
{
    return hrck_TransactionOutput::ref(&hrck_Coin::get(coin).out);
}

void hrck_coin_destroy(hrck_Coin* coin)
{
    delete coin;
}

int hrck_chainstate_manager_process_block(
    hrck_ChainstateManager* chainman,
    const hrck_Block* block,
    int* _new_block)
{
    bool new_block;
    auto result = hrck_ChainstateManager::get(chainman).m_chainman->ProcessNewBlock(hrck_Block::get(block), /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&new_block);
    if (_new_block) {
        *_new_block = new_block ? 1 : 0;
    }
    return result ? 0 : -1;
}

int hrck_chainstate_manager_process_block_header(
    hrck_ChainstateManager* chainstate_manager,
    const hrck_BlockHeader* header,
    hrck_BlockValidationState* state)
{
    try {
        auto& chainman = hrck_ChainstateManager::get(chainstate_manager).m_chainman;
        auto result = chainman->ProcessNewBlockHeaders({&hrck_BlockHeader::get(header), 1}, /*min_pow_checked=*/true, hrck_BlockValidationState::get(state), /*ppindex=*/nullptr);

        return result ? 0 : -1;
    } catch (const std::exception& e) {
        LogError("Failed to process block header: %s", e.what());
        return -1;
    }
}

const hrck_Chain* hrck_chainstate_manager_get_active_chain(const hrck_ChainstateManager* chainman)
{
    return hrck_Chain::ref(&WITH_LOCK(hrck_ChainstateManager::get(chainman).m_chainman->GetMutex(), return hrck_ChainstateManager::get(chainman).m_chainman->ActiveChain()));
}

int hrck_chain_get_height(const hrck_Chain* chain)
{
    LOCK(::cs_main);
    return hrck_Chain::get(chain).Height();
}

const hrck_BlockTreeEntry* hrck_chain_get_by_height(const hrck_Chain* chain, int height)
{
    LOCK(::cs_main);
    return hrck_BlockTreeEntry::ref(hrck_Chain::get(chain)[height]);
}

int hrck_chain_contains(const hrck_Chain* chain, const hrck_BlockTreeEntry* entry)
{
    LOCK(::cs_main);
    return hrck_Chain::get(chain).Contains(&hrck_BlockTreeEntry::get(entry)) ? 1 : 0;
}

hrck_BlockHeader* hrck_block_header_create(const void* raw_block_header, size_t raw_block_header_len)
{
    if (raw_block_header == nullptr && raw_block_header_len != 0) {
        return nullptr;
    }
    auto header{std::make_unique<CBlockHeader>()};
    SpanReader stream{std::span{reinterpret_cast<const std::byte*>(raw_block_header), raw_block_header_len}};

    try {
        stream >> *header;
    } catch (...) {
        LogError("Block header decode failed.");
        return nullptr;
    }

    return hrck_BlockHeader::ref(header.release());
}

hrck_BlockHeader* hrck_block_header_copy(const hrck_BlockHeader* header)
{
    return hrck_BlockHeader::copy(header);
}

hrck_BlockHash* hrck_block_header_get_hash(const hrck_BlockHeader* header)
{
    return hrck_BlockHash::create(hrck_BlockHeader::get(header).GetHash());
}

const hrck_BlockHash* hrck_block_header_get_prev_hash(const hrck_BlockHeader* header)
{
    return hrck_BlockHash::ref(&hrck_BlockHeader::get(header).hashPrevBlock);
}

uint32_t hrck_block_header_get_timestamp(const hrck_BlockHeader* header)
{
    return hrck_BlockHeader::get(header).nTime;
}

uint32_t hrck_block_header_get_bits(const hrck_BlockHeader* header)
{
    return hrck_BlockHeader::get(header).nBits;
}

int32_t hrck_block_header_get_version(const hrck_BlockHeader* header)
{
    return hrck_BlockHeader::get(header).nVersion;
}

uint32_t hrck_block_header_get_nonce(const hrck_BlockHeader* header)
{
    return hrck_BlockHeader::get(header).nNonce;
}

void hrck_block_header_destroy(hrck_BlockHeader* header)
{
    delete header;
}
