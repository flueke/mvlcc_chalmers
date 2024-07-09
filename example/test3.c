#include <mvlcc_readout_config.h>
#include <mvlcc_wrap.h>
#include <stdio.h>

int main(){
  mvlcc_t mvlc = mvlcc_make_mvlc_eth("mvlc-0056");
  int ec;
  int i;
  int tell = 1;

  if (ec = mvlcc_connect(mvlc))
    {
      fprintf(stderr, "Could not connect.\n");
      return 1;
    }

  uint16_t regAddr = 0x1304u; // the controller/crate id register
  uint32_t readValue = 0u;
  const uint32_t writeValue = 0b11;

  for (i = 0; i < 100000; i++)
  {
    ec = mvlcc_register_read(mvlc, regAddr, &readValue);

    if (ec)
    {
      fprintf(stderr, "Could not read internal register @ 0x%04x: %s.\n", regAddr, mvlcc_strerror(ec));
      return 1;
    }

    ec = mvlcc_register_write(mvlc, regAddr, writeValue);

    if (ec)
    {
      fprintf(stderr, "Could not write internal register @ 0x%04x: %s.\n", regAddr, mvlcc_strerror(ec));
      return 1;
    }

    if (i == tell)
    {
      printf("%d  ", i);
      fflush(stdout);
      if (tell < 5000)
        tell = tell * 2;
      else
        tell += 5000;
    }
  }

  printf("\n");
  mvlcc_disconnect(mvlc);
  mvlcc_free_mvlc(mvlc);

  return 0;
}
