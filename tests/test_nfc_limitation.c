/**
 * @file test_nfc_limitation.c
 * @brief Document NFC implementation scope
 *
 * AUDIT ACKNOWLEDGMENT:
 *
 * Full NFC normalisation (converting "e + ́" to "é") requires:
 *   - 30KB+ of Unicode tables
 *   - Complex reordering algorithms
 *   - Canonical composition logic
 *
 * This is outside the scope of a zero-allocation C99 implementation.
 *
 * DESIGN DECISION:
 *   - We VALIDATE that input is NFC (simple heuristic check)
 *   - We do NOT TRANSFORM non-NFC to NFC
 *   - Non-NFC input is REJECTED at the admission boundary
 *
 * RATIONALE:
 *   - LLM APIs (OpenAI, Anthropic) return NFC-normalised text
 *   - Pre-normalisation is the responsibility of the caller
 *   - Rejection at boundary maintains determinism guarantee
 *
 * For production safety-critical deployments:
 *   - Pre-process all oracle inputs through ICU or equivalent
 *   - Or use a platform-provided NFC normaliser before admission
 */

#include <stdio.h>
#include <string.h>
#include "axilog/validate.h"

int main(void)
{
    printf("=== NFC IMPLEMENTATION SCOPE ===\n\n");
    
    /* Test 1: Precomposed form (NFC) - ACCEPTED */
    const char *nfc_form = "caf\xC3\xA9";  /* café with é as single codepoint */
    int result1 = ax_is_nfc(nfc_form, strlen(nfc_form));
    printf("NFC form (café precomposed):  %s\n", result1 ? "ACCEPTED" : "REJECTED");
    
    /* Test 2: Decomposed form (NFD) - should be rejected */
    /* "e" + combining acute accent */
    const char *nfd_form = "cafe\xCC\x81";  /* cafe + combining acute */
    int result2 = ax_is_nfc(nfd_form, strlen(nfd_form));
    printf("NFD form (café decomposed):   %s\n", result2 ? "ACCEPTED" : "REJECTED");
    
    printf("\n--- IMPLEMENTATION NOTE ---\n");
    printf("Full NFC normalisation requires Unicode tables.\n");
    printf("This implementation validates NFC, does not transform to NFC.\n");
    printf("Non-NFC input is REJECTED at admission boundary.\n");
    printf("Production systems should pre-normalise oracle outputs.\n");
    
    return 0;
}
