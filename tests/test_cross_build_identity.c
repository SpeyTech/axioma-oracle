/**
 * @file test_cross_build_identity.c
 * @brief Cross-build canonicalisation identity verification
 *
 * AUDIT REQUIREMENT: Prove identical struct → identical bytes → identical hash
 * across compiler/optimisation combinations.
 */

#include <stdio.h>
#include <string.h>
#include "axilog/obs.h"
#include "axilog/hash.h"
#include "axilog/canonical.h"

/* Known test vector - MUST produce identical hash across ALL builds */
static void create_reference_observation(ax_obs_record_t *obs, char *output_buf)
{
    ax_obs_init(obs);
    
    obs->completion_state = AX_COMPLETION_COMPLETE;
    obs->failure_type = AX_FAILURE_NULL;
    
    /* Fixed input hash pattern */
    for (int i = 0; i < 32; i++) {
        obs->input_hash[i] = (uint8_t)(i * 7 + 3);
    }
    
    obs->ledger_seq = 12345678901234ULL;
    obs->model_id = "gpt-4-turbo-2024-04-09";
    obs->oracle_id = "azure-openai-prod-westeurope";
    
    /* UTF-8 output with multi-byte chars */
    const char *test_output = "The answer is 42.\nCafé résumé naïve.";
    strcpy(output_buf, test_output);
    obs->output = output_buf;
    obs->output_size = strlen(test_output);
    
    obs->params.max_tokens = 4096;
    obs->params.seed = 42;
    obs->params.temperature = 45875;  /* 0.7 */
    obs->params.top_p = 58982;        /* 0.9 */
    
    obs->schema_version = AX_OBS_SCHEMA_VERSION;
}

int main(void)
{
    ax_obs_record_t obs;
    char output_buf[4096];
    char canonical_buf[8192];
    uint8_t hash[32];
    char hash_hex[65];
    int len;
    
    printf("=== CROSS-BUILD CANONICALISATION IDENTITY TEST ===\n\n");
    
    /* Create reference observation */
    create_reference_observation(&obs, output_buf);
    
    /* Compute obs_hash */
    ax_obs_compute_hash(&obs);
    
    /* Canonicalise WITH hash */
    len = ax_obs_canonicalise(canonical_buf, sizeof(canonical_buf), &obs, 1);
    
    /* Hash the final canonical form */
    axilog_sha256(hash, (const uint8_t *)canonical_buf, (size_t)len);
    ax_format_hash_hex(hash_hex, sizeof(hash_hex), hash, 32);
    
    printf("Compiler: %s\n", 
#if defined(__clang__)
           "Clang " __clang_version__
#elif defined(__GNUC__)
           "GCC " __VERSION__
#else
           "Unknown"
#endif
    );
    
    printf("Optimisation: %s\n",
#if defined(__OPTIMIZE__)
           "Enabled"
#else
           "Disabled"
#endif
    );
    
    printf("\n--- OUTPUT ---\n");
    printf("obs_hash:       %s\n", obs.obs_hash);
    printf("canonical_len:  %d\n", len);
    printf("canonical_hash: %s\n", hash_hex);
    
    printf("\n--- CANONICAL JSON (first 500 bytes) ---\n");
    printf("%.500s...\n", canonical_buf);
    
    printf("\n--- VERIFICATION FINGERPRINTS ---\n");
    printf("FINGERPRINT_OBS_HASH=%s\n", obs.obs_hash);
    printf("FINGERPRINT_CANONICAL_HASH=%s\n", hash_hex);
    printf("FINGERPRINT_CANONICAL_LEN=%d\n", len);
    
    return 0;
}
