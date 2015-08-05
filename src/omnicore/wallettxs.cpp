#include "omnicore/wallettxs.h"

#include "omnicore/log.h"
#include "omnicore/omnicore.h"
#include "omnicore/rules.h"
#include "omnicore/script.h"
#include "omnicore/utilsbitcoin.h"

#include "amount.h"
#include "base58.h"
#include "coincontrol.h"
#include "init.h"
#include "main.h"
#include "pubkey.h"
#include "script/standard.h"
#include "sync.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "wallet.h"

#include <stdint.h>
#include <map>
#include <string>

namespace mastercore
{
/**
 * Retrieves a public key from the wallet, or converts a hex-string to a public key.
 */
bool AddressToPubKey(const std::string& key, CPubKey& pubKey)
{
#ifdef ENABLE_WALLET
    // Case 1: Bitcoin address and the key is in the wallet
    CBitcoinAddress address(key);
    if (pwalletMain && address.IsValid()) {
        CKeyID keyID;
        if (!address.GetKeyID(keyID)) {
            PrintToLog("%s() ERROR: redemption address %s does not refer to a public key\n", __func__, key);
            return false;
        }
        if (!pwalletMain->GetPubKey(keyID, pubKey)) {
            PrintToLog("%s() ERROR: no public key in wallet for redemption address %s\n", __func__, key);
            return false;
        }
    }
    // Case 2: Hex-encoded public key
    else
#endif
    if (IsHex(key)) {
        pubKey = CPubKey(ParseHex(key));
    }

    if (!pubKey.IsFullyValid()) {
        PrintToLog("%s() ERROR: invalid redemption key %s\n", __func__, key);
        return false;
    }

    return true;
}

/**
 * Checks, whether enough spendable outputs are available to pay for transaction fees.
 */
bool CheckFee(const std::string& fromAddress, size_t nDataSize)
{
    int64_t minFee = 0;
    int64_t inputTotal = 0;
#ifdef ENABLE_WALLET
    bool fUseClassC = UseEncodingClassC(nDataSize);

    CCoinControl coinControl;
    inputTotal = SelectCoins(fromAddress, coinControl, 0);

    // TODO: THIS NEEDS WORK - CALCULATIONS ARE UNSUITABLE CURRENTLY
    if (fUseClassC) {
        // estimated minimum fee calculation for Class C with payload of nDataSize
        // minFee = 3 * minRelayTxFee.GetFee(200) + CWallet::minTxFee.GetFee(200000);
        minFee = 10000; // simply warn when below 10,000 satoshi for now
    } else {
        // estimated minimum fee calculation for Class B with payload of nDataSize
        // minFee = 3 * minRelayTxFee.GetFee(200) + CWallet::minTxFee.GetFee(200000);
        minFee = 10000; // simply warn when below 10,000 satoshi for now
    }
#endif
    return inputTotal >= minFee;
}

/**
 * Checks, whether the output qualifies as input for a transaction.
 */
bool CheckInput(const CTxOut& txOut, int nHeight, CTxDestination& dest)
{
    txnouttype whichType;

    if (!GetOutputType(txOut.scriptPubKey, whichType)) {
        return false;
    }
    if (!IsAllowedInputType(whichType, nHeight)) {
        return false;
    }
    if (!ExtractDestination(txOut.scriptPubKey, dest)) {
        return false;
    }

    return true;
}

/**
 * Selects spendable outputs to create a transaction.
 */
int64_t SelectCoins(const std::string& fromAddress, CCoinControl& coinControl, int64_t additional)
{
    // total output funds collected
    int64_t nTotal = 0;

#ifdef ENABLE_WALLET
    if (NULL == pwalletMain) {
        return 0;
    }

    // assume 20 KB max. transaction size at 0.0001 per kilobyte
    int64_t nMax = (COIN * (20 * (0.0001)));

    // if referenceamount is set it is needed to be accounted for here too
    if (0 < additional) nMax += additional;

    int nHeight = GetHeight();
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // iterate over the wallet
    for (std::map<uint256, CWalletTx>::const_iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
        const uint256& txid = it->first;
        const CWalletTx& wtx = it->second;

        if (!wtx.IsTrusted()) {
            continue;
        }
        if (!wtx.GetAvailableCredit()) {
            continue;
        }

        for (unsigned int n = 0; n < wtx.vout.size(); n++) {
            const CTxOut& txOut = wtx.vout[n];

            CTxDestination dest;
            if (!CheckInput(txOut, nHeight, dest)) {
                continue;
            }
            if (!IsMine(*pwalletMain, dest)) {
                continue;
            }
            if (pwalletMain->IsSpent(txid, n)) {
                continue;
            }

            std::string sAddress = CBitcoinAddress(dest).ToString();
            if (msc_debug_tokens)
                PrintToLog("%s(): sender: %s, outpoint: %s:%d, value: %d\n", __func__, sAddress, txid.GetHex(), n, txOut.nValue);

            // only use funds from the sender's address
            if (fromAddress == sAddress) {
                COutPoint outpoint(txid, n);
                coinControl.Select(outpoint);

                nTotal += txOut.nValue;
                if (nMax <= nTotal) break;
            }
        }

        if (nMax <= nTotal) break;
    }
#endif

    return nTotal;
}


} // namespace mastercore
