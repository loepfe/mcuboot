/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2019 JUUL Labs
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "bootutil/bootutil.h"
#include "bootutil_priv.h"
#include "swap_priv.h"
#include "bootutil/bootutil_log.h"

#include "mcuboot_config/mcuboot_config.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#if !defined(MCUBOOT_SWAP_USING_MOVE) && !defined(MCUBOOT_SWAP_USING_OFFSET)

#if defined(MCUBOOT_VALIDATE_PRIMARY_SLOT)
/*
 * FIXME: this might have to be updated for threaded sim
 */
int boot_status_fails = 0;
#define BOOT_STATUS_ASSERT(x)                \
    do {                                     \
        if (!(x)) {                          \
            boot_status_fails++;             \
        }                                    \
    } while (0)
#else
#define BOOT_STATUS_ASSERT(x) ASSERT(x)
#endif

#if MCUBOOT_SWAP_USING_SCRATCH
/**
 * Finds the first sector of a given slot that holds image trailer data.
 *
 * @param state      Current bootloader's state.
 * @param slot       The index of the slot to consider.
 * @param trailer_sz The size of the trailer, in bytes.
 *
 * @return The index of the first sector of the slot that holds image trailer data.
 */
static size_t
boot_get_first_trailer_sector(struct boot_loader_state *state, size_t slot, size_t trailer_sz)
{
    size_t first_trailer_sector = boot_img_num_sectors(state, slot) - 1;
    size_t sector_sz = boot_img_sector_size(state, slot, first_trailer_sector);
    size_t trailer_sector_sz = sector_sz;

    while (trailer_sector_sz < trailer_sz) {
        /* Consider that the image trailer may span across sectors of different sizes */
        --first_trailer_sector;
        sector_sz = boot_img_sector_size(state, slot, first_trailer_sector);

        trailer_sector_sz += sector_sz;
    }

    return first_trailer_sector;
}

/**
 * Returns the offset to the end of the first sector of a given slot that holds image trailer data.
 *
 * @param state      Current bootloader's state.
 * @param slot       The index of the slot to consider.
 * @param trailer_sz The size of the trailer, in bytes.
 *
 * @return The offset to the end of the first sector of the slot that holds image trailer data.
 */
static uint32_t
get_first_trailer_sector_end_off(struct boot_loader_state *state, size_t slot, size_t trailer_sz)
{
    size_t first_trailer_sector = boot_get_first_trailer_sector(state, slot, trailer_sz);

    return boot_img_sector_off(state, slot, first_trailer_sector) +
           boot_img_sector_size(state, slot, first_trailer_sector);
}

/**
 * Returns the size of the part of the slot that can be used for storing image data.
 *
 * @param state     Current bootloader's state.
 * @param slot_size The size of the slot partition.
 *
 * @return The offset to the end of the first sector of the slot that holds image trailer data.
 */
static size_t app_max_size_adjust_to_trailer(struct boot_loader_state *state, size_t slot_size)
{
    size_t slot_trailer_sz = boot_trailer_sz(BOOT_WRITE_SZ(state));
    size_t slot_trailer_off = slot_size - slot_trailer_sz;
    size_t trailer_sz_in_first_sector;
    size_t trailer_sector_end_off;


    size_t trailer_sector_primary_end_off =
        get_first_trailer_sector_end_off(state, BOOT_PRIMARY_SLOT, slot_trailer_sz);
    size_t trailer_sector_secondary_end_off =
        get_first_trailer_sector_end_off(state, BOOT_SECONDARY_SLOT, slot_trailer_sz);

    /* If slots have sectors of different sizes, we need to find the "common" sector
     * boundary (slot compatibility checks ensure that the larger sector contains a multiple
     * of the smaller sector size). This will be the larger of the
     * trailer_sector_primary_end_off/trailer_sector_secondary_end_off.
     *
     *  <-------copy size-------> <--------copy size------> <----copy size--->
     * v                         v                         v                  v
     * +------------+------------+-------------------------+------------------+
     * |   sector   |   sector   |          sector         |      sector      |
     * +------------+------------+------------+------------+------------------+
     * |          sector         |   sector   |   sector   |      sector      |
     * +-------------------------+------------+------------+------------------+
     *
     * The swap logic always uses the common boundary when performing the copy.
     * Hence, the first trailer sector used for calculation is the larger
     * sector from the two slots.
     *
     * <-----------copy size--------------->
     * |     sector      |     sector      |
     * +-----------------------------------+
     * |              sector               |
     * +-----------------------------------+
     * |Image->|     |<-trailer------------|
     * +-----------------------------------+
     * |                |<-scratch trailer>|
     * +-----------------------------------+
     */
    if (trailer_sector_primary_end_off > trailer_sector_secondary_end_off) {
        trailer_sector_end_off = trailer_sector_primary_end_off;
    } else {
        trailer_sector_end_off = trailer_sector_secondary_end_off;
    }

    trailer_sz_in_first_sector = trailer_sector_end_off - slot_trailer_off;

    size_t trailer_padding = 0;
    size_t scratch_trailer_sz = boot_scratch_trailer_sz(BOOT_WRITE_SZ(state));

    /* Some padding might have to be inserted between the end of the firmware image and the
     * beginning of the trailer to ensure there is enough space for the trailer in the scratch area
     * when the last sector of the secondary will be copied to the scratch area.
     *
     * +-----------------------------------+-----------------------------------+
     * |              sector               |              sector               |
     * +-----------------------------------+-----------------------------------+
     * |Image->|             |<--trailer---|-----------trailer (cont.)-------->|
     * +-----------------------------------+-----------------------------------+
     * |         |<----scratch trailer---->|
     * +-----------------------------------+
     *            <-padding->
     *  <--------scratch area size-------->
     *
     * The value of the padding depends on the amount of trailer data that is contained in the first
     * sector containing part of the trailer in the primary and secondary slot.
     */
    if (scratch_trailer_sz > trailer_sz_in_first_sector) {
        trailer_padding = scratch_trailer_sz - trailer_sz_in_first_sector;
    }

    return slot_trailer_off - trailer_padding;
}
#endif /* MCUBOOT_SWAP_USING_SCRATCH */

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
/**
 * Reads the status of a partially-completed swap, if any.  This is necessary
 * to recover in case the boot lodaer was reset in the middle of a swap
 * operation.
 */
int
swap_read_status_bytes(const struct flash_area *fap,
        struct boot_loader_state *state, struct boot_status *bs)
{
    uint32_t off;
    uint8_t status;
    int max_entries;
    int found;
    int found_idx;
    int invalid;
    int rc;
    int i;

    off = boot_status_off(fap);
    max_entries = boot_status_entries(BOOT_CURR_IMG(state), fap);
    if (max_entries < 0) {
        return BOOT_EBADARGS;
    }

    found = 0;
    found_idx = 0;
    invalid = 0;
    for (i = 0; i < max_entries; i++) {
        rc = flash_area_read(fap, off + i * BOOT_WRITE_SZ(state),
                &status, 1);
        if (rc < 0) {
            return BOOT_EFLASH;
        }

        if (bootutil_buffer_is_erased(fap, &status, 1)) {
            if (found && !found_idx) {
                found_idx = i;
            }
        } else if (!found) {
            found = 1;
        } else if (found_idx) {
            invalid = 1;
            break;
        }
    }

    if (invalid) {
        /* This means there was an error writing status on the last
         * swap. Tell user and move on to validation!
         */
#if !defined(__BOOTSIM__)
        BOOT_LOG_ERR("Detected inconsistent status!");
#endif

#if !defined(MCUBOOT_VALIDATE_PRIMARY_SLOT)
        /* With validation of the primary slot disabled, there is no way
         * to be sure the swapped primary slot is OK, so abort!
         */
        assert(0);
#endif
    }

    if (found) {
        if (!found_idx) {
            found_idx = i;
        }
        bs->idx = (found_idx / BOOT_STATUS_STATE_COUNT) + 1;
        bs->state = (found_idx % BOOT_STATUS_STATE_COUNT) + 1;
    }

    return 0;
}

uint32_t
boot_status_internal_off(const struct boot_status *bs, int elem_sz)
{
    int idx_sz;

    idx_sz = elem_sz * BOOT_STATUS_STATE_COUNT;

    return (bs->idx - BOOT_STATUS_IDX_0) * idx_sz +
           (bs->state - BOOT_STATUS_STATE_0) * elem_sz;
}

/*
 * Slots are compatible when all sectors that store up to to size of the image
 * round up to sector size, in both slot's are able to fit in the scratch
 * area, and have sizes that are a multiple of each other (powers of two
 * presumably!).
 */
int
boot_slots_compatible(struct boot_loader_state *state)
{
    size_t num_sectors_primary;
    size_t num_sectors_secondary;
    size_t sz0, sz1;
    size_t primary_slot_sz, secondary_slot_sz;
#ifndef MCUBOOT_OVERWRITE_ONLY
    size_t scratch_sz;
#endif
    size_t i, j;
    int8_t smaller;

    num_sectors_primary = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT);
    num_sectors_secondary = boot_img_num_sectors(state, BOOT_SECONDARY_SLOT);
    if ((num_sectors_primary > BOOT_MAX_IMG_SECTORS) ||
        (num_sectors_secondary > BOOT_MAX_IMG_SECTORS)) {
        BOOT_LOG_WRN("Cannot upgrade: more sectors than allowed");
        return 0;
    }

#ifndef MCUBOOT_OVERWRITE_ONLY
    scratch_sz = boot_scratch_area_size(state);
#endif

    /*
     * The following loop scans all sectors in a linear fashion, assuring that
     * for each possible sector in each slot, it is able to fit in the other
     * slot's sector or sectors. Slot's should be compatible as long as any
     * number of a slot's sectors are able to fit into another, which only
     * excludes cases where sector sizes are not a multiple of each other.
     */
    i = sz0 = primary_slot_sz = 0;
    j = sz1 = secondary_slot_sz = 0;
    smaller = 0;
    while (i < num_sectors_primary || j < num_sectors_secondary) {
        if (sz0 == sz1) {
            sz0 += boot_img_sector_size(state, BOOT_PRIMARY_SLOT, i);
            sz1 += boot_img_sector_size(state, BOOT_SECONDARY_SLOT, j);
            i++;
            j++;
        } else if (sz0 < sz1) {
            sz0 += boot_img_sector_size(state, BOOT_PRIMARY_SLOT, i);
            /* Guarantee that multiple sectors of the secondary slot
             * fit into the primary slot.
             */
            if (smaller == 2) {
                BOOT_LOG_WRN("Cannot upgrade: slots have non-compatible sectors");
                return 0;
            }
            smaller = 1;
            i++;
        } else {
            size_t sector_size = boot_img_sector_size(state, BOOT_SECONDARY_SLOT, j);

#ifdef MCUBOOT_DECOMPRESS_IMAGES
            if (sector_size == 0) {
                /* Since this supports decompressed images, we can safely exit if slot1 is
                 * smaller than slot0.
                 */
                break;
            }
#endif
            sz1 += sector_size;
            /* Guarantee that multiple sectors of the primary slot
             * fit into the secondary slot.
             */
            if (smaller == 1) {
                BOOT_LOG_WRN("Cannot upgrade: slots have non-compatible sectors");
                return 0;
            }
            smaller = 2;
            j++;
        }
#ifndef MCUBOOT_OVERWRITE_ONLY
        if (sz0 == sz1) {
            primary_slot_sz += sz0;
            secondary_slot_sz += sz1;
            /* Scratch has to fit each swap operation to the size of the larger
             * sector among the primary slot and the secondary slot.
             */
            if (sz0 > scratch_sz || sz1 > scratch_sz) {
                BOOT_LOG_WRN("Cannot upgrade: not all sectors fit inside scratch");
                return 0;
            }
            smaller = sz0 = sz1 = 0;
        }
#endif
    }

#ifndef MCUBOOT_DECOMPRESS_IMAGES
    if ((i != num_sectors_primary) ||
        (j != num_sectors_secondary) ||
        (primary_slot_sz != secondary_slot_sz)) {
        BOOT_LOG_WRN("Cannot upgrade: slots are not compatible");
        return 0;
    }
#endif

    return 1;
}

#define BOOT_LOG_SWAP_STATE(area, state)                            \
    BOOT_LOG_INF("%s: magic=%s, swap_type=0x%x, copy_done=0x%x, "   \
                 "image_ok=0x%x",                                   \
                 (area),                                            \
                 ((state)->magic == BOOT_MAGIC_GOOD ? "good" :      \
                  (state)->magic == BOOT_MAGIC_UNSET ? "unset" :    \
                  "bad"),                                           \
                 (state)->swap_type,                                \
                 (state)->copy_done,                                \
                 (state)->image_ok)

struct boot_status_table {
    uint8_t bst_magic_primary_slot;
    uint8_t bst_magic_scratch;
    uint8_t bst_copy_done_primary_slot;
    uint8_t bst_status_source;
};

/**
 * This set of tables maps swap state contents to boot status location.
 * When searching for a match, these tables must be iterated in order.
 */
static const struct boot_status_table boot_status_tables[] = {
    {
        /*           | primary slot | scratch      |
         * ----------+--------------+--------------|
         *     magic | Good         | Any          |
         * copy-done | Set          | N/A          |
         * ----------+--------------+--------------'
         * source: none                            |
         * ----------------------------------------'
         */
        .bst_magic_primary_slot =     BOOT_MAGIC_GOOD,
        .bst_magic_scratch =          BOOT_MAGIC_NOTGOOD,
        .bst_copy_done_primary_slot = BOOT_FLAG_SET,
        .bst_status_source =          BOOT_STATUS_SOURCE_NONE,
    },

    {
        /*           | primary slot | scratch      |
         * ----------+--------------+--------------|
         *     magic | Good         | Any          |
         * copy-done | Unset        | N/A          |
         * ----------+--------------+--------------'
         * source: primary slot                    |
         * ----------------------------------------'
         */
        .bst_magic_primary_slot =     BOOT_MAGIC_GOOD,
        .bst_magic_scratch =          BOOT_MAGIC_NOTGOOD,
        .bst_copy_done_primary_slot = BOOT_FLAG_UNSET,
        .bst_status_source =          BOOT_STATUS_SOURCE_PRIMARY_SLOT,
    },

    {
        /*           | primary slot | scratch      |
         * ----------+--------------+--------------|
         *     magic | Any          | Good         |
         * copy-done | Any          | N/A          |
         * ----------+--------------+--------------'
         * source: scratch                         |
         * ----------------------------------------'
         */
        .bst_magic_primary_slot =     BOOT_MAGIC_ANY,
        .bst_magic_scratch =          BOOT_MAGIC_GOOD,
        .bst_copy_done_primary_slot = BOOT_FLAG_ANY,
        .bst_status_source =          BOOT_STATUS_SOURCE_SCRATCH,
    },
    {
        /*           | primary slot | scratch      |
         * ----------+--------------+--------------|
         *     magic | Unset        | Any          |
         * copy-done | Unset        | N/A          |
         * ----------+--------------+--------------|
         * source: varies                          |
         * ----------------------------------------+--------------------------+
         * This represents one of two cases:                                  |
         * o No swaps ever (no status to read, so no harm in checking).       |
         * o Mid-revert; status in primary slot.                              |
         * -------------------------------------------------------------------'
         */
        .bst_magic_primary_slot =     BOOT_MAGIC_UNSET,
        .bst_magic_scratch =          BOOT_MAGIC_ANY,
        .bst_copy_done_primary_slot = BOOT_FLAG_UNSET,
        .bst_status_source =          BOOT_STATUS_SOURCE_PRIMARY_SLOT,
    },
};

#define BOOT_STATUS_TABLES_COUNT \
    (sizeof boot_status_tables / sizeof boot_status_tables[0])

/**
 * Determines where in flash the most recent boot status is stored. The boot
 * status is necessary for completing a swap that was interrupted by a boot
 * loader reset.
 *
 * @return      A BOOT_STATUS_SOURCE_[...] code indicating where status should
 *              be read from.
 */
int
swap_status_source(struct boot_loader_state *state)
{
    const struct boot_status_table *table;
#if MCUBOOT_SWAP_USING_SCRATCH
    struct boot_swap_state state_scratch;
#endif
    struct boot_swap_state state_primary_slot;
    int rc;
    size_t i;
    uint8_t source;
    uint8_t image_index;

#if (BOOT_IMAGE_NUMBER == 1)
    (void)state;
#endif

    image_index = BOOT_CURR_IMG(state);
    rc = boot_read_swap_state(state->imgs[image_index][BOOT_PRIMARY_SLOT].area,
                              &state_primary_slot);
    assert(rc == 0);

#if MCUBOOT_SWAP_USING_SCRATCH
    rc = boot_read_swap_state(state->scratch.area, &state_scratch);
    assert(rc == 0);
#endif

    BOOT_LOG_SWAP_STATE("Primary image", &state_primary_slot);
#if MCUBOOT_SWAP_USING_SCRATCH
    BOOT_LOG_SWAP_STATE("Scratch", &state_scratch);
#endif
    for (i = 0; i < BOOT_STATUS_TABLES_COUNT; i++) {
        table = &boot_status_tables[i];

        if (boot_magic_compatible_check(table->bst_magic_primary_slot,
                          state_primary_slot.magic) &&
#if MCUBOOT_SWAP_USING_SCRATCH
            boot_magic_compatible_check(table->bst_magic_scratch,
                          state_scratch.magic) &&
#endif
            (table->bst_copy_done_primary_slot == BOOT_FLAG_ANY ||
             table->bst_copy_done_primary_slot == state_primary_slot.copy_done))
        {
            source = table->bst_status_source;

#if (BOOT_IMAGE_NUMBER > 1) && MCUBOOT_SWAP_USING_SCRATCH
            /* In case of multi-image boot it can happen that if boot status
             * info is found on scratch area then it does not belong to the
             * currently examined image.
             */
            if (source == BOOT_STATUS_SOURCE_SCRATCH &&
                state_scratch.image_num != BOOT_CURR_IMG(state)) {
                source = BOOT_STATUS_SOURCE_NONE;
            }
#endif

            BOOT_LOG_INF("Boot source: %s",
                         source == BOOT_STATUS_SOURCE_NONE ? "none" :
                         source == BOOT_STATUS_SOURCE_SCRATCH ? "scratch" :
                         source == BOOT_STATUS_SOURCE_PRIMARY_SLOT ?
                                   "primary slot" : "BUG; can't happen");
            return source;
        }
    }

    BOOT_LOG_INF("Boot source: none");
    return BOOT_STATUS_SOURCE_NONE;
}

#ifndef MCUBOOT_OVERWRITE_ONLY
/**
 * Calculates the number of sectors the scratch area can contain.  A "last"
 * source sector is specified because images are copied backwards in flash
 * (final index to index number 0).
 *
 * @param last_sector_idx       The index of the last source sector
 *                                  (inclusive).
 * @param out_first_sector_idx  The index of the first source sector
 *                                  (inclusive) gets written here.
 *
 * @return                      The number of bytes comprised by the
 *                                  [first-sector, last-sector] range.
 */
static uint32_t
boot_copy_sz(const struct boot_loader_state *state, int last_sector_idx,
             int *out_first_sector_idx)
{
    size_t scratch_sz;
    uint32_t new_sz;
    uint32_t sz;
    int i;

    sz = 0;

    scratch_sz = boot_scratch_area_size(state);
    for (i = last_sector_idx; i >= 0; i--) {
        new_sz = sz + boot_img_sector_size(state, BOOT_PRIMARY_SLOT, i);
        /*
         * The secondary slot is not being checked here, because
         * `boot_slots_compatible` already provides assurance that the copy size
         * will be compatible with the primary slot and scratch.
         */
        if (new_sz > scratch_sz) {
            break;
        }
        sz = new_sz;
    }

    /* i currently refers to a sector that doesn't fit or it is -1 because all
     * sectors have been processed.  In both cases, exclude sector i.
     */
    *out_first_sector_idx = i + 1;
    return sz;
}

/**
 * Finds the index of the last sector in the primary slot that needs swapping.
 *
 * @param state     Current bootloader's state.
 * @param copy_size Total number of bytes to swap.
 *
 * @return          Index of the last sector in the primary slot that needs swapping.
 */
static int
find_last_sector_idx(const struct boot_loader_state *state, uint32_t copy_size)
{
    int last_sector_idx_primary;
    int last_sector_idx_secondary;
    uint32_t primary_slot_size;
    uint32_t secondary_slot_size;

    primary_slot_size = 0;
    secondary_slot_size = 0;
    last_sector_idx_primary = 0;
    last_sector_idx_secondary = 0;

    /*
     * Knowing the size of the largest image between both slots, here we
     * find what is the last sector in the primary slot that needs swapping.
     * Since we already know that both slots are compatible, the secondary
     * slot's last sector is not really required after this check is finished.
     */
    while (1) {
        if ((primary_slot_size < copy_size) ||
            (primary_slot_size < secondary_slot_size)) {
           primary_slot_size += boot_img_sector_size(state,
                                                     BOOT_PRIMARY_SLOT,
                                                     last_sector_idx_primary);
            ++last_sector_idx_primary;
        }
        if ((secondary_slot_size < copy_size) ||
            (secondary_slot_size < primary_slot_size)) {
           secondary_slot_size += boot_img_sector_size(state,
                                                       BOOT_SECONDARY_SLOT,
                                                       last_sector_idx_secondary);
            ++last_sector_idx_secondary;
        }
        if (primary_slot_size >= copy_size &&
                secondary_slot_size >= copy_size &&
                primary_slot_size == secondary_slot_size) {
            break;
        }
    }

    return last_sector_idx_primary - 1;
}

/**
 * Finds the number of swap operations that have to be performed to swap the two images.
 *
 * @param state     Current bootloader's state.
 * @param copy_size Total number of bytes to swap.
 *
 * @return          The number of swap operations that have to be performed.
*/
static uint32_t
find_swap_count(const struct boot_loader_state *state, uint32_t copy_size)
{
    int first_sector_idx;
    int last_sector_idx;
    uint32_t swap_count;

    last_sector_idx = find_last_sector_idx(state, copy_size);

    swap_count = 0;

    while (last_sector_idx >= 0) {
        boot_copy_sz(state, last_sector_idx, &first_sector_idx);

        last_sector_idx = first_sector_idx - 1;
        swap_count++;
    }

    return swap_count;
}

/**
 * Swaps the contents of two flash regions within the two image slots.
 *
 * @param idx                   The index of the first sector in the range of
 *                                  sectors being swapped.
 * @param sz                    The number of bytes to swap.
 * @param bs                    The current boot status.  This struct gets
 *                                  updated according to the outcome.
 *
 * @return                      0 on success; nonzero on failure.
 */
static void
boot_swap_sectors(int idx, uint32_t sz, struct boot_loader_state *state,
        struct boot_status *bs)
{
    const struct flash_area *fap_primary_slot;
    const struct flash_area *fap_secondary_slot;
    const struct flash_area *fap_scratch;
    uint32_t copy_sz;
    uint32_t trailer_sz;
    uint32_t img_off;
    uint32_t scratch_trailer_off;
    struct boot_swap_state swap_state;
    size_t first_trailer_sector_primary;
    bool erase_scratch;
    uint8_t image_index;
    int rc;

    image_index = BOOT_CURR_IMG(state);

    fap_primary_slot = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);
    assert(fap_primary_slot != NULL);

    fap_secondary_slot = BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT);
    assert(fap_secondary_slot != NULL);

    fap_scratch = state->scratch.area;
    assert(fap_scratch != NULL);

    /* Calculate offset from start of image area. */
    img_off = boot_img_sector_off(state, BOOT_PRIMARY_SLOT, idx);

    copy_sz = sz;
    trailer_sz = boot_trailer_sz(BOOT_WRITE_SZ(state));

    /* sz in this function is always sized on a multiple of the sector size.
     * The check against the start offset of the first trailer sector is to determine if we're
     * swapping that sector, which might contains both part of the firmware image and part of the
     * trailer (or the whole trailer if the latter is small enough). Therefore, that sector needs
     * special handling: if we're copying it, we need to use scratch to write the trailer
     * temporarily.
     *
     * Since the primary and secondary slots don't necessarily have the same layout, the index of
     * the first trailer sector may be different for each slot.
     *
     * NOTE: `use_scratch` is a temporary flag (never written to flash) which
     * controls if special handling is needed (swapping the first trailer sector).
     */
    first_trailer_sector_primary =
        boot_get_first_trailer_sector(state, BOOT_PRIMARY_SLOT, trailer_sz);

    /* Check if the currently swapped sector(s) contain the trailer or part of it */
    if ((img_off + sz) >
        boot_img_sector_off(state, BOOT_PRIMARY_SLOT, first_trailer_sector_primary)) {
        copy_sz = flash_area_get_size(fap_primary_slot) - img_off - trailer_sz;

        /* Check if the computed copy size would cause the beginning of the trailer in the scratch
         * area to be overwritten. If so, adjust the copy size to avoid this.
         *
         * This could happen if the trailer is larger than a single sector since in that case the
         * first part of the trailer may be smaller than the trailer in the scratch area.
         */
        scratch_trailer_off = boot_status_off(fap_scratch);

        if (copy_sz > scratch_trailer_off) {
            copy_sz = scratch_trailer_off;
        }
    }

    bs->use_scratch = (bs->idx == BOOT_STATUS_IDX_0 && copy_sz != sz);

    if (bs->state == BOOT_STATUS_STATE_0) {
        BOOT_LOG_DBG("erasing scratch area");
        rc = boot_erase_region(fap_scratch, 0, flash_area_get_size(fap_scratch), false);
        assert(rc == 0);

        if (bs->idx == BOOT_STATUS_IDX_0) {
            /* Write a trailer to the scratch area, even if we don't need the
             * scratch area for status.  We need a temporary place to store the
             * `swap-type` while we erase the primary trailer.
             */
            rc = swap_status_init(state, fap_scratch, bs);
            assert(rc == 0);

            if (!bs->use_scratch) {
                /* Prepare the primary status area... here it is known that the
                 * last sector is not being used by the image data so it's safe
                 * to erase.
                 */
                rc = swap_scramble_trailer_sectors(state, fap_primary_slot);
                assert(rc == 0);

                rc = swap_status_init(state, fap_primary_slot, bs);
                assert(rc == 0);

                /* Erase the temporary trailer from the scratch area. */
                rc = boot_erase_region(fap_scratch, 0,
                        flash_area_get_size(fap_scratch), false);
                assert(rc == 0);
            }
        }

        rc = boot_copy_region(state, fap_secondary_slot, fap_scratch,
                              img_off, 0, copy_sz);
        assert(rc == 0);

        rc = boot_write_status(state, bs);
        bs->state = BOOT_STATUS_STATE_1;
        BOOT_STATUS_ASSERT(rc == 0);
    }

    if (bs->state == BOOT_STATUS_STATE_1) {
        uint32_t erase_sz = sz;

        if (bs->idx == BOOT_STATUS_IDX_0) {
            /* Guarantee here that only the primary slot will have the state.
             *
             * This is necessary even though the current area being swapped contains part of the
             * trailer since in case the trailer spreads over multiple sector erasing the [img_off,
             * img_off + sz) might not erase the entire trailer.
              */
            rc = swap_scramble_trailer_sectors(state, fap_secondary_slot);
            assert(rc == 0);

            if (bs->use_scratch) {
                /* If the area being swapped contains the trailer or part of it, ensure the
                 * sector(s) containing the beginning of the trailer won't be erased again.
                 */
                size_t trailer_sector_secondary =
                    boot_get_first_trailer_sector(state, BOOT_SECONDARY_SLOT, trailer_sz);

                uint32_t trailer_sector_offset =
                    boot_img_sector_off(state, BOOT_SECONDARY_SLOT, trailer_sector_secondary);

                erase_sz = trailer_sector_offset - img_off;
            }
        }

        if (erase_sz > 0) {
            rc = boot_erase_region(fap_secondary_slot, img_off, erase_sz, false);
            assert(rc == 0);
        }

        rc = boot_copy_region(state, fap_primary_slot, fap_secondary_slot,
                              img_off, img_off, copy_sz);
        assert(rc == 0);

        rc = boot_write_status(state, bs);
        bs->state = BOOT_STATUS_STATE_2;
        BOOT_STATUS_ASSERT(rc == 0);
    }

    if (bs->state == BOOT_STATUS_STATE_2) {
        uint32_t erase_sz = sz;

        if (bs->use_scratch) {
            /* The current area that is being swapped contains the trailer or part of it. In that
             * case, make sure to erase all sectors containing the trailer in the primary slot to be
             * able to write the new trailer. This is not always equivalent to erasing the [img_off,
             * img_off + sz) range when the trailer spreads across multiple sectors.
             */
            rc = swap_scramble_trailer_sectors(state, fap_primary_slot);
            assert(rc == 0);

            /* Ensure the sector(s) containing the beginning of the trailer won't be erased twice */
            uint32_t trailer_sector_off =
                boot_img_sector_off(state, BOOT_PRIMARY_SLOT, first_trailer_sector_primary);

            erase_sz = trailer_sector_off - img_off;
        }

        if (erase_sz > 0) {
            rc = boot_erase_region(fap_primary_slot, img_off, erase_sz, false);
            assert(rc == 0);
        }

        /* NOTE: If this is the final sector, we exclude the image trailer from
         * this copy (copy_sz was truncated earlier).
         */
        rc = boot_copy_region(state, fap_scratch, fap_primary_slot,
                              0, img_off, copy_sz);
        assert(rc == 0);

        if (bs->use_scratch) {
            scratch_trailer_off = boot_status_off(fap_scratch);

            /* copy current status that is being maintained in scratch */
            rc = boot_copy_region(state, fap_scratch, fap_primary_slot,
                        scratch_trailer_off, img_off + copy_sz,
                        (BOOT_STATUS_STATE_COUNT - 1) * BOOT_WRITE_SZ(state));
            BOOT_STATUS_ASSERT(rc == 0);

            rc = boot_read_swap_state(fap_scratch, &swap_state);
            assert(rc == 0);

            if (swap_state.image_ok == BOOT_FLAG_SET) {
                rc = boot_write_image_ok(fap_primary_slot);
                assert(rc == 0);
            }

            if (swap_state.swap_type != BOOT_SWAP_TYPE_NONE) {
                rc = boot_write_swap_info(fap_primary_slot,
                        swap_state.swap_type, image_index);
                assert(rc == 0);
            }

            rc = boot_write_swap_size(fap_primary_slot, bs->swap_size);
            assert(rc == 0);

#ifdef MCUBOOT_ENC_IMAGES
            rc = boot_write_enc_key(fap_primary_slot, 0, bs);
            assert(rc == 0);

            rc = boot_write_enc_key(fap_primary_slot, 1, bs);
            assert(rc == 0);
#endif
            rc = boot_write_magic(fap_primary_slot);
            assert(rc == 0);
        }

        /* If we wrote a trailer to the scratch area, erase it after we persist
         * a trailer to the primary slot.  We do this to prevent mcuboot from
         * reading a stale status from the scratch area in case of immediate
         * reset.
         */
        erase_scratch = bs->use_scratch;
        bs->use_scratch = 0;

        rc = boot_write_status(state, bs);
        bs->idx++;
        bs->state = BOOT_STATUS_STATE_0;
        BOOT_STATUS_ASSERT(rc == 0);

        if (erase_scratch) {
           /* Scratch trailers MUST be erased backwards, this is to avoid an issue whereby a
            * device reboots in the process of erasing the scratch if it erased forwards, if that
            * happens then the scratch which is partially erased would be wrote back to the
            * primary slot, causing a corrupt unbootable image
            */
            rc = boot_erase_region(fap_scratch, 0, flash_area_get_size(fap_scratch), true);
            assert(rc == 0);
        }
    }
}

void
swap_run(struct boot_loader_state *state, struct boot_status *bs,
         uint32_t copy_size)
{
    uint32_t sz;
    int first_sector_idx;
    int last_sector_idx;
    uint32_t swap_idx;

    BOOT_LOG_INF("Starting swap using scratch algorithm.");

    last_sector_idx = find_last_sector_idx(state, copy_size);

    swap_idx = 0;
    while (last_sector_idx >= 0) {
        sz = boot_copy_sz(state, last_sector_idx, &first_sector_idx);
        if (swap_idx >= (bs->idx - BOOT_STATUS_IDX_0)) {
            boot_swap_sectors(first_sector_idx, sz, state, bs);
        }

        last_sector_idx = first_sector_idx - 1;
        swap_idx++;
    }

}
#endif /* !MCUBOOT_OVERWRITE_ONLY */

int app_max_size(struct boot_loader_state *state)
{
    size_t num_sectors_primary;
    size_t num_sectors_secondary;
    size_t sz0, sz1;
#ifndef MCUBOOT_OVERWRITE_ONLY
    size_t slot_sz;
    size_t scratch_sz;
#endif
    size_t i, j;
    int8_t smaller;

    num_sectors_primary = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT);
    num_sectors_secondary = boot_img_num_sectors(state, BOOT_SECONDARY_SLOT);

#ifndef MCUBOOT_OVERWRITE_ONLY
    scratch_sz = boot_scratch_area_size(state);
#endif

    /*
     * The following loop scans all sectors in a linear fashion, assuring that
     * for each possible sector in each slot, it is able to fit in the other
     * slot's sector or sectors. Slot's should be compatible as long as any
     * number of a slot's sectors are able to fit into another, which only
     * excludes cases where sector sizes are not a multiple of each other.
     */
#ifndef MCUBOOT_OVERWRITE_ONLY
    slot_sz = 0;
#endif
    i = sz0 = 0;
    j = sz1 = 0;
    smaller = 0;
    while (i < num_sectors_primary || j < num_sectors_secondary) {
        if (sz0 == sz1) {
            sz0 += boot_img_sector_size(state, BOOT_PRIMARY_SLOT, i);
            sz1 += boot_img_sector_size(state, BOOT_SECONDARY_SLOT, j);
            i++;
            j++;
        } else if (sz0 < sz1) {
            sz0 += boot_img_sector_size(state, BOOT_PRIMARY_SLOT, i);
            /* Guarantee that multiple sectors of the secondary slot
             * fit into the primary slot.
             */
            if (smaller == 2) {
                BOOT_LOG_WRN("Cannot upgrade: slots have non-compatible sectors");
                return 0;
            }
            smaller = 1;
            i++;
        } else {
            sz1 += boot_img_sector_size(state, BOOT_SECONDARY_SLOT, j);
            /* Guarantee that multiple sectors of the primary slot
             * fit into the secondary slot.
             */
            if (smaller == 1) {
                BOOT_LOG_WRN("Cannot upgrade: slots have non-compatible sectors");
                return 0;
            }
            smaller = 2;
            j++;
        }
#ifndef MCUBOOT_OVERWRITE_ONLY
        if (sz0 == sz1) {
            slot_sz += sz0;
            /* Scratch has to fit each swap operation to the size of the larger
             * sector among the primary slot and the secondary slot.
             */
            if (sz0 > scratch_sz || sz1 > scratch_sz) {
                BOOT_LOG_WRN("Cannot upgrade: not all sectors fit inside scratch");
                return 0;
            }
            smaller = sz0 = sz1 = 0;
        }
#endif
    }

#ifdef MCUBOOT_OVERWRITE_ONLY
    return (sz1 < sz0 ? sz1 : sz0);
#elif MCUBOOT_SWAP_USING_SCRATCH
    return app_max_size_adjust_to_trailer(state, slot_sz);
#else
    return slot_sz;
#endif
}
#else
int app_max_size(struct boot_loader_state *state)
{
    const struct flash_area *fap = NULL;
    uint32_t active_slot;
    int primary_sz, secondary_sz;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

    fap = BOOT_IMG_AREA(state, active_slot);
    assert(fap != NULL);
    primary_sz = flash_area_get_size(fap);

    if (active_slot == BOOT_PRIMARY_SLOT) {
        active_slot = BOOT_SECONDARY_SLOT;
    } else {
        active_slot = BOOT_PRIMARY_SLOT;
    }

    fap = BOOT_IMG_AREA(state, active_slot);
    assert(fap != NULL);
    secondary_sz = flash_area_get_size(fap);

    return (secondary_sz < primary_sz ? secondary_sz : primary_sz);
}

#endif /* !MCUBOOT_DIRECT_XIP && !MCUBOOT_RAM_LOAD */

int
boot_read_image_header(struct boot_loader_state *state, int slot,
                       struct image_header *out_hdr, struct boot_status *bs)
{
    const struct flash_area *fap;
#ifdef MCUBOOT_SWAP_USING_SCRATCH
    uint32_t swap_count;
    uint32_t swap_size;
#endif
    int hdr_slot;
    int rc = 0;

#ifndef MCUBOOT_SWAP_USING_SCRATCH
    (void)bs;
#endif

#if (BOOT_IMAGE_NUMBER == 1)
    (void)state;
#endif

    hdr_slot = slot;

#ifdef MCUBOOT_SWAP_USING_SCRATCH
    /* If the slots are being swapped, the headers might have been moved to scratch area or to the
     * other slot depending on the progress of the swap process.
     */
    if (bs && !boot_status_is_reset(bs)) {
        fap = boot_find_status(state, BOOT_CURR_IMG(state));

        if (rc != 0) {
            rc = BOOT_EFLASH;
            goto done;
        }

        rc = boot_read_swap_size(fap, &swap_size);
        flash_area_close(fap);

        if (rc != 0) {
            rc = BOOT_EFLASH;
            goto done;
        }

        swap_count = find_swap_count(state, swap_size);

        if (bs->idx - BOOT_STATUS_IDX_0 >= swap_count) {
            /* If all segments have been swapped, the header is located in the other slot */
            hdr_slot = (slot == BOOT_PRIMARY_SLOT) ? BOOT_SECONDARY_SLOT : BOOT_PRIMARY_SLOT;
        } else if (bs->idx - BOOT_STATUS_IDX_0 == swap_count - 1) {
            /* If the last swap operation is in progress, the headers are currently being swapped
             * since the first segment of each slot is the last to be processed.
             */

            if (slot == BOOT_SECONDARY_SLOT && bs->state >= BOOT_STATUS_STATE_1) {
                /* After BOOT_STATUS_STATE_1, the secondary image's header has been moved to the
                 * scratch area.
                 */
                hdr_slot = BOOT_NUM_SLOTS;
            } else if (slot == BOOT_PRIMARY_SLOT && bs->state >= BOOT_STATUS_STATE_2) {
                /* After BOOT_STATUS_STATE_2, the primary image's header has been moved to the
                 * secondary slot.
                 */
                hdr_slot = BOOT_SECONDARY_SLOT;
            }
        }
    }

    if (hdr_slot == BOOT_NUM_SLOTS) {
        fap = state->scratch.area;
    } else {
        fap = BOOT_IMG_AREA(state, hdr_slot);
    }
#else
    fap = BOOT_IMG_AREA(state, hdr_slot);
#endif
    assert(fap != NULL);

    rc = flash_area_read(fap, 0, out_hdr, sizeof *out_hdr);

    if (rc != 0) {
        rc = BOOT_EFLASH;
        goto done;
    }

done:
    return rc;
}

#endif /* !MCUBOOT_SWAP_USING_MOVE && !MCUBOOT_SWAP_USING_OFFSET */
