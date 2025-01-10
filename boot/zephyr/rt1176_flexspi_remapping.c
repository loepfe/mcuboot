#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "rt1176_flexspi_remapping.h"

#ifdef __cplusplus
extern "C" {
#endif

static void __attribute__((section(".itcm"))) invalidate_data_cache(void);
static void __attribute__((section(".itcm"))) invalidate_prefetch_buffer(void);

static void __attribute__((section(".itcm"))) _invalidate_data_cache(const struct _rt1176_flexspi_remap_config *config);
static void __attribute__((section(".itcm"))) _invalidate_prefetch_buffer(FLEXSPI_Type* flexspi);

static struct rt1176_flexspi_remap_config ap_config = {};

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

void _rt1176_flexspi_remap_set_config(FLEXSPI_Type * flexspi,
                                     const struct _rt1176_flexspi_remap_config *config)
{
    flexspi->HADDRSTART = config->exec_area_start_address & FLEXSPI_HADDRSTART_ADDRSTART_MASK;
    flexspi->HADDREND = config->exec_area_end_address & FLEXSPI_HADDREND_ENDSTART_MASK;
    flexspi->HADDROFFSET = config->remap_offset & FLEXSPI_HADDROFFSET_ADDROFFSET_MASK;

    if (config->enable) {
       remap_config.flexspi->HADDRSTART |= FLEXSPI_HADDRSTART_REMAPEN(1);
    }

    _invalidate_data_cache(config);
    _invalidate_prefetch_buffer(flexspi);
}

void _rt1176_flexspi_remap_get_config(FLEXSPI_Type * flexspi,
                                     struct _rt1176_flexspi_remap_config *config)
{
    config->exec_area_start_address = flexspi->HADDRSTART & ~FLEXSPI_HADDRSTART_REMAPEN_MASK;
    config->exec_area_end_address = flexspi->HADDREND;
    config->remap_offset = flexspi->HADDROFFSET;

    if (flexspi->HADDRSTART & FLEXSPI_HADDRSTART_REMAPEN_MASK) {
      config->enable = true;
    } else {
      config->enable = false;
    }
}

void _rt1176_flexspi_remap_print_config(const char* title,
                                        const struct _rt1176_flexspi_remap_config* config)
{
  printf("%s\n", title);
    printf("exec_area_start_address: 0x%08X\n", config->exec_area_start_address);
    printf("exec_area_start_end    : 0x%08X\n", config->exec_area_end_address);
    printf("remap_offset           : 0x%08X\n", config->remap_offset);
    printf("enable                 : 0x%08X\n", config->enable);
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


static void _invalidate_data_cache(const struct _rt1176_flexspi_remap_config *config)
{
    /* Invalidate data cache. At this moment, the flash memory is considered
       data since no code is not executed from it. */
    const uint32_t exec_area_size = config->exec_area_end_address -
                                    config->exec_area_start_address;

    SCB_InvalidateDCache_by_Addr ((void*)(config->exec_area_start_address),
                                  exec_area_size);
}

static void _invalidate_prefetch_buffer(FLEXSPI_Type* flexspi)
{
   /* Do software reset or clear AHB buffer directly. */
   flexspi->AHBCR |= FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;
   flexspi->AHBCR &= ~FLEXSPI_AHBCR_CLRAHBRXBUF_MASK;
}
#ifdef __cplusplus
}
#endif
