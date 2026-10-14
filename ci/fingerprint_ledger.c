/* ci/fingerprint.c - commitments, chain heads and verifier verdicts for a
 * deterministic 300-record chain, for cross-architecture byte-identity checks
 * (CI compares a hash of this output with ci/fingerprints.txt). */
#include <axilog/audit.h>
#include <axilog/dvec.h>
#include <stdio.h>
#include <string.h>
#define N 300
static const char *const TAGS[5] = { AX_TAG_STATE, AX_TAG_TRANS, AX_TAG_OBS, AX_TAG_POLICY, AX_TAG_PROOF };
static char payload[N][96];
static ax_evidence_t rec[N];
static uint8_t commits[N][32];
static void hex(const uint8_t *b){ int i; for(i=0;i<32;i++) printf("%02x",b[i]); }
int main(void){
  ax_ledger_ctx_t c; ct_fault_flags_t f; uint8_t head[32]; unsigned i;
  ct_fault_init(&f); ax_ledger_genesis(&c,&f);
  printf("L0 "); hex(c.genesis_hash); printf("\n");
  for (i=0;i<N;i++){
    snprintf(payload[i], sizeof payload[i], "{\"i\":%u,\"v\":\"%08x\"}", i, i*2654435761u);
    rec[i].tag = TAGS[i%5]; rec[i].payload=(const uint8_t*)payload[i]; rec[i].payload_len=strlen(payload[i]);
    ax_commit_evidence(&rec[i], commits[i], &f); ax_ledger_append(&c, commits[i], &f);
    if (i%25==0 || i==N-1){ printf("%u ",i); hex(commits[i]); printf(" "); hex(c.current_hash); printf("\n"); }
  }
  memcpy(head,c.current_hash,32);
  ct_fault_init(&f); printf("verdict genuine %d\n", ax_verify_chain_records(rec,N,(const uint8_t(*)[32])commits,head,NULL,&f));
  ct_fault_init(&f); printf("verdict unanchored %d\n", ax_verify_chain_records(rec,N,NULL,NULL,NULL,&f));
  ct_fault_init(&f); printf("verdict truncated %d\n", ax_verify_chain_records(rec,N-1,NULL,head,NULL,&f));
  ct_fault_init(&f); printf("verdict commits %d\n", ax_verify_chain_commits((const uint8_t(*)[32])commits,N,head,NULL,&f));
  return ct_fault_any(&f) && 0;
}
