#ifndef H_RT1176_FLEXSPI_REMAPPING
#define H_RT1176_FLEXSPI_REMAPPING

#include <stdint.h>

#include "bootutil/crypto/sha.h"  // TODO MAS: Replace with correct include for FLEXSPI!

#define FLASH_BASE_ADDRESS \
 DT_REG_ADDR_BY_IDX(DT_NODELABEL(flexspi2), 1)
#define PRIMARY_SLOT_PARTITION_OFFSET \
  DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_0), reg, 0)
#define PRIMARY_SLOT_PARTITION_SIZE \
  DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_0), reg, 1)
#define SECONDARY_SLOT_PARTITION_OFFSET \
  DT_REG_ADDR_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_1), 0)

struct rt1176_flexspi_remap_config {
        FLEXSPI_Type * flexspi;  /** FlexSPI interface instance */
        uint32_t exec_area_start_address;  /** Start address of execution area */
        uint32_t exec_area_end_address;  /** End address of execution area */
        uint32_t remap_offset;         /**< Offset of remapped area */
};

void rt1176_flexspi_remap_configure(const struct rt1176_flexspi_remap_config *config);
void rt1176_flexspi_remap_enable(void);
void rt1176_flexspi_remap_disable(void);
bool rt1176_flexspi_is_remapping_enabled(void);

#endif
