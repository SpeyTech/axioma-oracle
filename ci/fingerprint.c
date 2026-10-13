/* ci/fingerprint.c - deterministic AX:OBS:v1 corpus for cross-architecture
 * byte-identity checks. One line per case: admission rc, canonical length,
 * obs_hash and a canonical SHA-256 prefix. CI hashes this output and compares
 * it with ci/fingerprints.txt. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "axilog/obs.h"
#include "axilog/canonical.h"
#include "axilog/limits.h"
#include <axilog/sha256.h>
static char obuf[AX_MAX_OUTPUT_BYTES + 1];
static char canon[AX_CANONICAL_BUFFER_SIZE];
static char out[AX_MAX_OUTPUT_BYTES + 1];
static char mid[300], oid[300];
static void run(int id, const char *o, size_t olen, const char *m, const char *r){
  ax_admission_ctx_t a; ax_obs_input_t in; ax_obs_record_t rec; ax_l3_fault_flags_t f; uint8_t h[32]; int rc, n=0;
  ax_admission_ctx_init(&a); memset(&in,0,sizeof in); memset(&f,0,sizeof f);
  in.completion_state=AX_COMPLETION_COMPLETE; in.input="prompt"; in.input_len=6; in.ledger_seq=1;
  in.model_id=m; in.oracle_id=r; in.output=o; in.output_len=olen; ax_oracle_params_init_null(&in.params);
  in.params.max_tokens = id; ax_obs_init(&rec);
  rc = ax_obs_admit(&rec, obuf, sizeof obuf, &in, &a, &f);
  if (rc == AX_OK) n = ax_obs_canonicalise(canon, sizeof canon, &rec, 1);
  printf("case=%d rc=%d len=%d", id, rc, n);
  if (rc == AX_OK && n > 0) {
    int loss = strstr(canon, "\"output\":\"\",\"output_size\":0,") == NULL && strstr(canon, "\"output\":\"\",") != NULL;
    axilog_sha256(h,(const uint8_t*)canon,(size_t)n);
    printf(" obs=%s canon=", rec.obs_hash); for(int i=0;i<8;i++) printf("%02x",h[i]); printf(" loss=%d", loss);
  }
  printf("\n");
}
int main(void){
  int id=0; unsigned s=12345;
  /* 1. newline-dense outputs around and beyond the old drop threshold */
  size_t nls[] = {0,1,100,1000,10000,20000,21000,21503,21504,21505,30000,64000};
  for (size_t i=0;i<sizeof nls/sizeof *nls;i++){ size_t n=nls[i]; memset(out,'\n',n); out[n]='x'; run(id++, out, n+1, "model-1", "oracle-A"); }
  /* 2. quote-dense */
  size_t qs[] = {1,1000,32000,64000,64510};
  for (size_t i=0;i<sizeof qs/sizeof *qs;i++){ memset(out,'"',qs[i]); run(id++, out, qs[i], "m", "o"); }
  /* 3. random printable ASCII + UTF-8 of many lengths */
  for (int k=0;k<300;k++){ size_t n = (size_t)(k*k*7 % 64000); size_t j=0;
    while (j<n){ s=s*1103515245u+12345u; unsigned c=(s>>16)%100;
      if (c<85) out[j++]=(char)(0x20+((s>>8)%95));
      else if (c<92 && j+2<=n){ out[j++]=(char)0xC3; out[j++]=(char)0xA9; }
      else if (c<96 && j+3<=n){ out[j++]=(char)0xE2; out[j++]=(char)0x82; out[j++]=(char)0xAC; }
      else out[j++] = (c<98)?'\\':'"'; }
    run(id++, out, n, "model-x", "oracle-y"); }
  /* 4. identifier lengths around the 256-byte limit */
  size_t ls[] = {0,1,255,256,257,299};
  for (size_t i=0;i<sizeof ls/sizeof *ls;i++){ memset(mid,'m',ls[i]); mid[ls[i]]=0; memset(oid,'o',ls[i]); oid[ls[i]]=0;
    run(id++, "hello", 5, mid, "o"); run(id++, "hello", 5, "m", oid); }
  return 0;
}
