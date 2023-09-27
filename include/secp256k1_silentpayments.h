#ifndef SECP256K1_SILENTPAYMENTS_H
#define SECP256K1_SILENTPAYMENTS_H

#include "secp256k1.h"
#include "secp256k1_extrakeys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This module provides an implementation for the ECC related parts of
 * Silent Payments, as specified in BIP352. This particularly involves
 * the creation of input tweak data by summing up private or public keys
 * and the derivation of a shared secret using Elliptic Curve Diffie-Hellman.
 * Combined are either:
 *   - spender's private keys and receiver's public key (a * B, sender side)
 *   - spender's public keys and receiver's private key (A * b, receiver side)
 * With this result, the necessary key material for ultimately creating/scanning
 * or spending Silent Payment outputs can be determined.
 *
 * Note that this module is _not_ a full implementation of BIP352, as it
 * inherently doesn't deal with higher-level concepts like addresses, output
 * script types or transactions. The intent is to provide cryptographical
 * helpers for low-level calculations that are most error-prone to custom
 * implementations (e.g. enforcing the right y-parity for key material, ECDH
 * calculation etc.). For any wallet software already using libsecp256k1, this
 * API should provide all the functions needed for a Silent Payments
 * implementation without the need for any further manual elliptic-curve
 * operations.
 */

/* This struct serves as an In/Out param for the sender.
 * The recipient's silent payment addresses is passed in for generating the taproot outputs.
 * The generated outputs are saved in the `generated_outputs` array. This ensures the caller
 * is able to match up the generated outputs to the correct recpient (e.g. to be able to assign
 * the correct amounts to the correct outputs in the final transaction).
 */
typedef struct {
    secp256k1_pubkey scan_pubkey;
    secp256k1_pubkey spend_pubkey;
    secp256k1_xonly_pubkey generated_output;
} secp256k1_silentpayments_recipient;

/** Create Silent Payment shared secret.
 *
 * Given a public component Pub, a private component sec and an input_hash,
 * calculate the corresponding shared secret using ECDH:
 *
 * shared_secret = (sec * input_hash) * Pub
 *
 * What the components should be set to depends on the role of the caller.
 * For the sender side, the public component is set the recipient's scan public key
 * B_scan, and the private component is set to the input's private keys sum:
 *
 * shared_secret = (a_sum * input_hash) * B_scan   [Sender]
 *
 * For the receiver side, the public component is set to the input's public keys sum,
 * and the private component is set to the receiver's scan private key:
 *
 * shared_secret = (b_scan * input_hash) * A_sum   [Receiver, Full node scenario]
 *
 * In the "light client" scenario for receivers, the public component is already
 * tweaked with the input hash: A_tweaked = input_hash * A_sum
 * In this case, the input_hash parameter should be set to NULL, to signal that
 * no further tweaking should be done before the ECDH:
 *
 * shared_secret = b_scan * A_tweaked   [Receiver, Light client scenario]
 *
 * The resulting shared secret is needed as input for creating silent payments
 * outputs belonging to the same receiver scan public key.
 *
 *  Returns: 1 if shared secret creation was successful. 0 if an error occured.
 *  Args:                  ctx: pointer to a context object
 *  Out:       shared_secret33: pointer to the resulting 33-byte shared secret
 *  In:       public_component: pointer to the public component
 *           private_component: pointer to 32-byte private component
 *                  input_hash: pointer to 32-byte input hash (can be NULL if the
 *                              public component is already tweaked with the input hash)
 */
SECP256K1_API SECP256K1_WARN_UNUSED_RESULT int secp256k1_silentpayments_create_shared_secret(
    const secp256k1_context *ctx,
    unsigned char *shared_secret33,
    const secp256k1_pubkey *public_component,
    const unsigned char *secret_component,
    const unsigned char *input_hash
) SECP256K1_ARG_NONNULL(1) SECP256K1_ARG_NONNULL(2) SECP256K1_ARG_NONNULL(3) SECP256K1_ARG_NONNULL(4);

/** Create Silent Payment output public key.
 *
 *  Given a shared_secret, a public key B_spend, and an output counter k,
 *  calculate the corresponding output public key:
 *
 *  P_output_xonly = B_spend + hash(shared_secret || ser_32(k)) * G
 *
 *  This function can be used by the sender or receiver, but is particularly useful for the reciever
 *  when scanning for outputs without access to the transaction outputs (e.g. using BIP158 block filters).
 *  When scanning with this function, it is the scanners responsibility to determine if the generated
 *  output exists in a block before proceeding to the next value of `k`.
 *
 *  Returns: 1 if output creation was successful. 0 if an error occured.
 *  Args:               ctx: pointer to a context object
 *  Out:     P_output_xonly: pointer to the resulting output x-only pubkey
 *  In:     shared_secret33: shared secret, derived from either sender's
 *                           or receiver's perspective with routines from above
 *    receiver_spend_pubkey: pointer to the receiver's spend pubkey (labelled or unlabelled)
 *                        k: output counter (initially set to 0, must be incremented for each
 *                           additional output created or after each output found when scanning)
 */
SECP256K1_API SECP256K1_WARN_UNUSED_RESULT int secp256k1_silentpayments_create_output_pubkey(
    const secp256k1_context *ctx,
    secp256k1_xonly_pubkey *P_output_xonly,
    const unsigned char *shared_secret33,
    const secp256k1_pubkey *receiver_spend_pubkey,
    unsigned int k
) SECP256K1_ARG_NONNULL(1) SECP256K1_ARG_NONNULL(2) SECP256K1_ARG_NONNULL(3) SECP256K1_ARG_NONNULL(4);

/* TODO: add function API for sender side. */

/* TODO: add function API for receiver side. */

#ifdef __cplusplus
}
#endif

#endif /* SECP256K1_SILENTPAYMENTS_H */
