/*-
 * Copyright (C) 2010, Romain Tartiere, Romuald Conty.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <freefare.h>

#include "freefare_internal.h"

#define MAX_CANDIDATES 16

#define NXP_MANUFACTURER_CODE 0x04

struct supported_tag supported_tags[] = {
    { FELICA,              "FeliCA",                       NMT_FELICA,    0x00, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_1K,   "Mifare Classic 1k",            NMT_ISO14443A, 0x08, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_1K,   "Mifare Classic 1k (Emulated)", NMT_ISO14443A, 0x28, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_1K,   "Mifare Classic 1k (Emulated)", NMT_ISO14443A, 0x68, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_1K,   "Infineon Mifare Classic 1k",   NMT_ISO14443A, 0x88, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_4K,   "Mifare Classic 4k",            NMT_ISO14443A, 0x18, 0, 0, { 0x00 }, NULL },
    { MIFARE_CLASSIC_4K,   "Mifare Classic 4k (Emulated)", NMT_ISO14443A, 0x38, 0, 0, { 0x00 }, NULL },
    { MIFARE_DESFIRE,      "Mifare DESFire",               NMT_ISO14443A, 0x20, 5, 4, { 0x75, 0x77, 0x81, 0x02 /*, 0xXX */ }, NULL},
    { MIFARE_DESFIRE,      "Cyanogenmod card emulation",   NMT_ISO14443A, 0x60, 4, 3, { 0x78, 0x33, 0x88 /*, 0xXX */ }, NULL},
    { MIFARE_DESFIRE,      "Android HCE",                  NMT_ISO14443A, 0x60, 4, 3, { 0x78, 0x80, 0x70 /*, 0xXX */ }, NULL},
    { MIFARE_ULTRALIGHT_C, "Mifare UltraLightC",           NMT_ISO14443A, 0x00, 0, 0, { 0x00 }, is_mifare_ultralightc_on_reader },
    { MIFARE_ULTRALIGHT,   "Mifare UltraLight",            NMT_ISO14443A, 0x00, 0, 0, { 0x00 }, NULL },
};

/*
 * Automagically allocate a FreefareTag given a device and target info.
 */
FreefareTag
freefare_tag_new (nfc_device *device, nfc_target target)
{
    FreefareTag tag = NULL;

    if (felica_taste (device, target)) {
	tag = felica_tag_new (device, target);
    } else if (mifare_classic1k_taste (device, target)) {
	tag = mifare_classic1k_tag_new (device, target);
    } else if (mifare_classic4k_taste (device, target)) {
	tag = mifare_classic4k_tag_new (device, target);
    } else if (mifare_desfire_taste (device, target)) {
	tag = mifare_desfire_tag_new (device, target);
    } else if (mifare_ultralightc_taste (device, target)) {
	tag = mifare_ultralightc_tag_new (device, target);
    } else if (mifare_ultralight_taste (device, target)) {
	tag = mifare_ultralight_tag_new (device, target);
    }

    return tag;
}


/*
 * MIFARE card common functions
 *
 * The following functions send NFC commands to the initiator to prepare
 * communication with a MIFARE card, and perform required cleannups after using
 * the targets.
 */

/*
 * Get a list of the MIFARE targets near to the provided NFC initiator.
 *
 * The list has to be freed using the freefare_free_tags() function.
 */
FreefareTag *
freefare_get_tags (nfc_device *device)
{
    FreefareTag *tags = NULL;
    int tag_count = 0;

    nfc_initiator_init(device);

    // Drop the field for a while
    nfc_device_set_property_bool(device,NP_ACTIVATE_FIELD,false);

    // Configure the CRC and Parity settings
    nfc_device_set_property_bool(device,NP_HANDLE_CRC,true);
    nfc_device_set_property_bool(device,NP_HANDLE_PARITY,true);
    nfc_device_set_property_bool(device,NP_AUTO_ISO14443_4,true);

    // Enable field so more power consuming cards can power themselves up
    nfc_device_set_property_bool(device,NP_ACTIVATE_FIELD,true);

    // Poll for a ISO14443A (MIFARE) tag
    nfc_target candidates[MAX_CANDIDATES];
    int candidates_count;
    nfc_modulation modulation = {
	.nmt = NMT_ISO14443A,
	.nbr = NBR_106
    };
    if ((candidates_count = nfc_initiator_list_passive_targets(device, modulation, candidates, MAX_CANDIDATES)) < 0)
	return NULL;

    tags = malloc(sizeof (void *));
    if(!tags) return NULL;
    tags[0] = NULL;

    for (int c = 0; c < candidates_count; c++) {
	FreefareTag t;
	if ((t = freefare_tag_new(device, candidates[c]))) {
	    /* (Re)Allocate memory for the found MIFARE targets array */
	    FreefareTag *p = realloc (tags, (tag_count + 2) * sizeof (FreefareTag));
	    if (p)
		tags = p;
	    else
		return tags; // FAIL! Return what has been found so far.
	    tags[tag_count++] = t;
	    tags[tag_count] = NULL;
	}
    }

    // Poll for a FELICA tag
    modulation.nmt = NMT_FELICA;
    modulation.nbr = NBR_424; // FIXME NBR_212 should also be supported
    if ((candidates_count = nfc_initiator_list_passive_targets(device, modulation, candidates, MAX_CANDIDATES)) < 0)
	return NULL;

    for (int c = 0; c < candidates_count; c++) {
	FreefareTag t;
	if ((t = freefare_tag_new(device, candidates[c]))) {
	    /* (Re)Allocate memory for the found FELICA targets array */
	    FreefareTag *p = realloc (tags, (tag_count + 2) * sizeof (FreefareTag));
	    if (p)
		tags = p;
	    else
		return tags; // FAIL! Return what has been found so far.
	    tags[tag_count++] = t;
	    tags[tag_count] = NULL;
	}
    }

    return tags;
}

/*
 * Returns the type of the provided tag.
 */
enum freefare_tag_type
freefare_get_tag_type (FreefareTag tag)
{
    return tag->type;
}

/*
 * Returns the friendly name of the provided tag.
 */
const char *
freefare_get_tag_friendly_name (FreefareTag tag)
{
    switch (tag->type) {
    case FELICA:
	return "FeliCA";
    case MIFARE_CLASSIC_1K:
	return "Mifare Classic 1k";
    case MIFARE_CLASSIC_4K:
	return "Mifare Classic 4k";
    case MIFARE_DESFIRE:
	return "Mifare DESFire";
    case MIFARE_ULTRALIGHT_C:
	return "Mifare UltraLightC";
    case MIFARE_ULTRALIGHT:
	return "Mifare UltraLight";
    default:
	return "UNKNOWN";
    }
}

/*
 * Returns the UID of the provided tag.
 */
char *
freefare_get_tag_uid (FreefareTag tag)
{
    char *res = NULL;
    switch (tag->info.nm.nmt) {
    case NMT_FELICA:
	if ((res = malloc (17))) {
	    for (size_t i = 0; i < 8; i++)
		snprintf (res + 2*i, 3, "%02x", tag->info.nti.nfi.abtId[i]);
	}
	break;
    case NMT_ISO14443A:
	if ((res = malloc (2 * tag->info.nti.nai.szUidLen + 1))) {
	    for (size_t i = 0; i < tag->info.nti.nai.szUidLen; i++)
		snprintf (res + 2*i, 3, "%02x", tag->info.nti.nai.abtUid[i]);
	}
	break;
    case NMT_DEP:
    case NMT_ISO14443B2CT:
    case NMT_ISO14443B2SR:
    case NMT_ISO14443B:
    case NMT_ISO14443BI:
    case NMT_JEWEL:
	res = strdup ("UNKNOWN");
    }
    return res;
}

/*
 * Returns true if last selected tag is still present.
 */
bool freefare_selected_tag_is_present(nfc_device *device)
{
    return (nfc_initiator_target_is_present(device, NULL) == NFC_SUCCESS);
}

/*
 * Free the provided tag.
 */
void
freefare_free_tag (FreefareTag tag)
{
    if (tag) {
	tag->free_tag (tag);
    }
}

const char *
freefare_strerror (FreefareTag tag)
{
    const char *p = "Unknown error";
    if (nfc_device_get_last_error (tag->device) < 0) {
      p = nfc_strerror (tag->device);
    } else {
      if (tag->type == MIFARE_DESFIRE) {
        if (MIFARE_DESFIRE (tag)->last_pcd_error) {
          p = mifare_desfire_error_lookup (MIFARE_DESFIRE (tag)->last_pcd_error);
        } else if (MIFARE_DESFIRE (tag)->last_picc_error) {
          p = mifare_desfire_error_lookup (MIFARE_DESFIRE (tag)->last_picc_error);
        }
      }
    }
    return p;
}

int
freefare_strerror_r (FreefareTag tag, char *buffer, size_t len)
{
    return (snprintf (buffer, len, "%s", freefare_strerror (tag)) < 0) ? -1 : 0;
}

void
freefare_perror (FreefareTag tag, const char *string)
{
    fprintf (stderr, "%s: %s\n", string, freefare_strerror (tag));
}

/*
 * Free the provided tag list.
 */
void
freefare_free_tags (FreefareTag *tags)
{
    if (tags) {
	for (int i=0; tags[i]; i++) {
	    freefare_free_tag(tags[i]);
	}
	free (tags);
    }
}


/*
 * Low-level API
 */

void *
memdup (const void *p, const size_t n)
{
    void *res;
    if ((res = malloc (n))) {
	memcpy (res, p, n);
    }
    return res;
}
