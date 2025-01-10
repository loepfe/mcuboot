#include <assert.h>
#include <string.h>

#include "rt1176_flexspi_remapping.h"

#ifdef __cplusplus
extern "C" {
#endif

static void invalidate_data_cache(void);
static void invalidate_prefetch_buffer(void);

static struct rt1176_flexspi_remap_config remap_config = {};

void rt1176_flexspi_remap_configure(const struct rt1176_flexspi_remap_config *config)
{
    assert(!is_remapping_enabled);
    assert(config->exec_area_end > config->exec_area_start);

    memcpy(&remap_config, config, sizeof(remap_config));

    remap_config.flexspi->HADDRSTART = config->exec_area_start_address;
    remap_config.flexspi->HADDREND = config->exec_area_end_address;
    remap_config.flexspi->HADDROFFSET = config->remap_offset;
}

void rt1176_flexspi_remap_enable(void)
{
    remap_config.flexspi->HADDRSTART |= FLEXSPI_HADDRSTART_REMAPEN(1);
    invalidate_data_cache();
    invalidate_prefetch_buffer();
}

void rt1176_flexspi_remap_disable(void)
{
    remap_config.flexspi->HADDRSTART &= FLEXSPI_HADDRSTART_REMAPEN(0);
    invalidate_data_cache();
    invalidate_prefetch_buffer();
}

bool rt1176_flexspi_is_remapping_enabled(void)
{
  if ((remap_config.flexspi->HADDRSTART & FLEXSPI_HADDRSTART_REMAPEN_MASK) > 0) {
    return true;
  } else {
    return false;
  }
}

static void invalidate_data_cache(void)
{
    /* Invalidate data cache. At this moment, the flash memory is considered
       data since no code is not executed from it. */
    const uint32_t exec_area_size = remap_config.exec_area_end_address -
                                    remap_config.exec_area_start_address;

    SCB_InvalidateDCache_by_Addr ((void*)(remap_config.exec_area_start_address),
                                  exec_area_size);
}

static void invalidate_prefetch_buffer(void)
{
   /* Do software reset or clear AHB buffer directly. */
   FLEXSPI2->AHBCR |= FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;
   FLEXSPI2->AHBCR &= ~FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;
}

#ifdef __cplusplus
}
#endif
