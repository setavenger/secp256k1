/***********************************************************************
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or https://www.opensource.org/licenses/mit-license.php.*
 ***********************************************************************/

#ifndef SECP256K1_MODULE_SILENTPAYMENTS_TESTS_H
#define SECP256K1_MODULE_SILENTPAYMENTS_TESTS_H

#include "../../../include/secp256k1_silentpayments.h"
#include "../../../src/modules/silentpayments/vectors.h"
#include "../../../examples/examples_util.h"

struct label_cache_entry {
    secp256k1_pubkey label;
    unsigned char label_tweak[32];
};
struct labels_cache {
    const secp256k1_context *ctx;
    size_t entries_used;
    struct label_cache_entry entries[10];
};
struct labels_cache labels_cache;
const unsigned char* label_lookup(const secp256k1_pubkey* key, const void* cache_ptr) {
    const struct labels_cache* cache = (const struct labels_cache*)cache_ptr;
    size_t i;
    for (i = 0; i < cache->entries_used; i++) {
        if (secp256k1_ec_pubkey_cmp(cache->ctx, &cache->entries[i].label, key) == 0) {
            return cache->entries[i].label_tweak;
        }
    }
    return NULL;
}

void run_silentpayments_test_vector_send(const struct bip352_test_vector *test) {
    secp256k1_silentpayments_recipient recipients[MAX_OUTPUTS_PER_TEST_CASE];
    secp256k1_silentpayments_recipient *recipient_ptrs[MAX_OUTPUTS_PER_TEST_CASE];
    unsigned char const *plain_seckeys[MAX_INPUTS_PER_TEST_CASE];
    unsigned char const *taproot_seckeys[MAX_INPUTS_PER_TEST_CASE];
    unsigned char created_output[32];
    size_t i, j;
    int match;

    /* Check that sender creates expected outputs */
    for (i = 0; i < test->num_outputs; i++) {
        CHECK(secp256k1_ec_pubkey_parse(CTX, &recipients[i].scan_pubkey, test->recipient_pubkeys[i].scan_pubkey, 33));
        CHECK(secp256k1_ec_pubkey_parse(CTX, &recipients[i].spend_pubkey, test->recipient_pubkeys[i].spend_pubkey, 33));
        recipient_ptrs[i] = &recipients[i];
    }
    for (i = 0; i < test->num_plain_inputs; i++) {
        plain_seckeys[i] = test->plain_seckeys[i];
    }
    for (i = 0; i < test->num_taproot_inputs; i++) {
        taproot_seckeys[i] = test->taproot_seckeys[i];
    }
    CHECK(secp256k1_silentpayments_sender_create_outputs(CTX,
                recipient_ptrs,
                test->num_outputs,
                test->outpoint_smallest,
                test->num_plain_inputs > 0 ? plain_seckeys : NULL, test->num_plain_inputs,
                test->num_taproot_inputs > 0 ? taproot_seckeys : NULL, test->num_taproot_inputs
    ));
    for (i = 0; i < test->num_outputs; i++) {
        CHECK(secp256k1_xonly_pubkey_serialize(CTX, created_output, &recipients[i].generated_output));
        match = 0;
        /* Loop over both lists to ensure tests don't fail due to different orderings of outputs */
        for (j = 0; j < test->num_recipient_outputs; j++) {
            if (secp256k1_memcmp_var(created_output, test->recipient_outputs[j], 32) == 0) {
                match = 1;
                break;
            }
        }
        CHECK(match);
    }
}

void run_silentpayments_test_vector_receive(const struct bip352_test_vector *test) {
    secp256k1_pubkey plain_pubkeys_objs[MAX_INPUTS_PER_TEST_CASE];
    secp256k1_xonly_pubkey xonly_pubkeys_objs[MAX_INPUTS_PER_TEST_CASE];
    secp256k1_xonly_pubkey tx_output_objs[MAX_OUTPUTS_PER_TEST_CASE];
    secp256k1_silentpayments_found_output found_output_objs[MAX_OUTPUTS_PER_TEST_CASE];
    secp256k1_pubkey const *plain_pubkeys[MAX_INPUTS_PER_TEST_CASE];
    secp256k1_xonly_pubkey const *xonly_pubkeys[MAX_INPUTS_PER_TEST_CASE];
    secp256k1_xonly_pubkey const *tx_outputs[MAX_OUTPUTS_PER_TEST_CASE];
    secp256k1_silentpayments_found_output *found_outputs[MAX_OUTPUTS_PER_TEST_CASE];
    secp256k1_pubkey receiver_scan_pubkey;
    secp256k1_pubkey receiver_spend_pubkey;
    unsigned char shared_secret[33];
    size_t i,j;
    int match;
    size_t n_found = 0;
    unsigned char found_output[32];
    unsigned char found_signatures[10][64];
    unsigned char input_hash_receiver[32];
    secp256k1_pubkey A_sum;


    /* prepare the inputs */
    {
        for (i = 0; i < test->num_plain_inputs; i++) {
            CHECK(secp256k1_ec_pubkey_parse(CTX, &plain_pubkeys_objs[i], test->plain_pubkeys[i], 33));
            plain_pubkeys[i] = &plain_pubkeys_objs[i];
        }
        for (i = 0; i < test->num_taproot_inputs; i++) {
            CHECK(secp256k1_xonly_pubkey_parse(CTX, &xonly_pubkeys_objs[i], test->xonly_pubkeys[i]));
            xonly_pubkeys[i] = &xonly_pubkeys_objs[i];
        }
        CHECK(secp256k1_silentpayments_create_public_tweak_data(CTX, &A_sum, input_hash_receiver,
            test->num_plain_inputs > 0 ? plain_pubkeys : NULL, test->num_plain_inputs,
            test->num_taproot_inputs > 0 ? xonly_pubkeys : NULL, test->num_taproot_inputs,
            test->outpoint_smallest));
    }
    /* prepare the outputs */
    {
        for (i = 0; i < test->num_to_scan_outputs; i++) {
            CHECK(secp256k1_xonly_pubkey_parse(CTX, &tx_output_objs[i], test->to_scan_outputs[i]));
            tx_outputs[i] = &tx_output_objs[i];
        }
        for (i = 0; i < test->num_found_output_pubkeys; i++) {
            found_outputs[i] = &found_output_objs[i];
        }
    }

    /* scan / spend pubkeys are not in the given data of the receiver part, so let's compute them */
    CHECK(secp256k1_ec_pubkey_create(CTX, &receiver_scan_pubkey, test->scan_seckey));
    CHECK(secp256k1_ec_pubkey_create(CTX, &receiver_spend_pubkey, test->spend_seckey));

    /* create shared secret */
    {
        unsigned char shared_secret_fullnode[33];
        unsigned char shared_secret_lightclient[33];
        secp256k1_pubkey A_tweaked;

        CHECK(secp256k1_silentpayments_create_shared_secret(CTX, shared_secret_fullnode, &A_sum, test->scan_seckey, input_hash_receiver));
        /* check that creating shared secret in light client / index mode (with intermediate A_tweaked) leads to the same result */
        CHECK(secp256k1_silentpayments_create_public_tweak_data(CTX, &A_tweaked, NULL,
            test->num_plain_inputs > 0 ? plain_pubkeys : NULL, test->num_plain_inputs,
            test->num_taproot_inputs > 0 ? xonly_pubkeys : NULL, test->num_taproot_inputs,
            test->outpoint_smallest));
        CHECK(secp256k1_silentpayments_create_shared_secret(CTX, shared_secret_lightclient, &A_tweaked, test->scan_seckey, NULL));
        CHECK(secp256k1_memcmp_var(shared_secret_fullnode, shared_secret_lightclient, 33) == 0);

        memcpy(shared_secret, shared_secret_fullnode, 33);
    }

    /* create labels cache */
    labels_cache.ctx = CTX;
    labels_cache.entries_used = 0;
    for (i = 0; i < test->num_labels; i++) {
        unsigned int m = test->label_integers[i];
        struct label_cache_entry *cache_entry = &labels_cache.entries[labels_cache.entries_used];
        CHECK(secp256k1_silentpayments_create_label_tweak(CTX, &cache_entry->label, cache_entry->label_tweak, test->scan_seckey, m));
        labels_cache.entries_used++;
    }
    CHECK(secp256k1_silentpayments_receiver_scan_outputs(CTX, found_outputs, &n_found, shared_secret, &receiver_spend_pubkey, tx_outputs, test->num_to_scan_outputs, label_lookup, &labels_cache));
    for (i = 0; i < n_found; i++) {
        unsigned char full_seckey[32];
        secp256k1_keypair keypair;
        unsigned char signature[64];
        const unsigned char msg32[32] = /* sha256("message") */
            {0xab,0x53,0x0a,0x13,0xe4,0x59,0x14,0x98,0x2b,0x79,0xf9,0xb7,0xe3,0xfb,0xa9,0x94,
             0xcf,0xd1,0xf3,0xfb,0x22,0xf7,0x1c,0xea,0x1a,0xfb,0xf0,0x2b,0x46,0x0c,0x6d,0x1d};
        const unsigned char aux32[32] = /* sha256("random auxiliary data") */
            {0x0b,0x3f,0xdd,0xfd,0x67,0xbf,0x76,0xae,0x76,0x39,0xee,0x73,0x5b,0x70,0xff,0x15,
             0x83,0xfd,0x92,0x48,0xc0,0x57,0xd2,0x86,0x07,0xa2,0x15,0xf4,0x0b,0x0a,0x3e,0xcc};
        memcpy(&full_seckey, test->spend_seckey, 32);
        CHECK(secp256k1_ec_seckey_tweak_add(CTX, full_seckey, found_outputs[i]->tweak));
        CHECK(secp256k1_keypair_create(CTX, &keypair, full_seckey));
        CHECK(secp256k1_schnorrsig_sign32(CTX, signature, msg32, &keypair, aux32));
        memcpy(found_signatures[i], signature, 64);
    }

    /* compare expected and scanned outputs (including calculated seckey tweaks and signatures) */
    for (i = 0; i < n_found; i++) {
        CHECK(secp256k1_xonly_pubkey_serialize(CTX, found_output, found_outputs[i]->output));
        match = 0;
        for (j = 0; j < test->num_found_output_pubkeys; j++) {
            if (secp256k1_memcmp_var(&found_output, test->found_output_pubkeys[j], 32) == 0) {
                match = 1;
                CHECK(secp256k1_memcmp_var(found_outputs[i]->tweak, test->found_seckey_tweaks[j], 32) == 0);
                CHECK(secp256k1_memcmp_var(found_signatures[i], test->found_signatures[j], 64) == 0);
                break;
            }
        }
        CHECK(match);
    }
    CHECK(n_found == test->num_found_output_pubkeys);
}

void run_silentpayments_test_vectors(void) {
    size_t i;


    for (i = 0; i < sizeof(bip352_test_vectors) / sizeof(bip352_test_vectors[0]); i++) {
        const struct bip352_test_vector *test = &bip352_test_vectors[i];
        run_silentpayments_test_vector_send(test);
        run_silentpayments_test_vector_receive(test);
    }
}

void run_silentpayments_tests(void) {
    run_silentpayments_test_vectors();

    /* TODO: add a few manual tests here, that target the ECC-related parts of silent payments */
}

#endif