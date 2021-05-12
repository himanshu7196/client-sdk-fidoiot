/*
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: Apache 2.0
 */

/*!
 * \file
 * \brief This file implements msg50 of TO2 state machine.
 */

#include "fdoCrypto.h"
#include "load_credentials.h"
#include "fdoprot.h"
#include "util.h"

#define REUSE_HMAC_MAX_LEN 1

/**
 * msg70() - TO2.Done
 *
 * TO2.Done = [
 *   NonceTO2ProveDv         ;; Nonce generated by Owner Onboarding Service
 *                  ;; ...and sent to Device ROE in Msg TO2.ProveOVHdr
 * ]
 */
int32_t msg70(fdo_prot_t *ps)
{
	int ret = -1;
	fdo_hash_t *hmac = NULL;

	LOG(LOG_DEBUG, "TO2.Done started\n");

	/*
	 * TODO: Writing credentials to TEE!
	 * This GUID came as g3 - "the new transaction GUID"
	 * which will overwrite GUID in initial credential data.
	 * A new transaction will start fresh, taking the latest
	 * credential (among them this, new GUID). That's why
	 * simple memorizing GUID in RAM is not needed.
	 */
	fdo_byte_array_free(ps->dev_cred->owner_blk->guid);
	ps->dev_cred->owner_blk->guid = ps->osc->guid;

	fdo_rendezvous_list_free(ps->dev_cred->owner_blk->rvlst);
	ps->dev_cred->owner_blk->rvlst = ps->osc->rvlst;

	fdo_public_key_free(ps->dev_cred->owner_blk->pk);
	ps->dev_cred->owner_blk->pk = ps->osc->pubkey;

	if (ps->reuse_enabled && reuse_supported) {
		// Reuse scenario, moving to post DI state
		ps->dev_cred->ST = FDO_DEVICE_STATE_READY1;
	} else if (resale_supported) {
		// Done with FIDO Device Onboard.
		// As of now moving to done state for resale
		ps->dev_cred->ST = FDO_DEVICE_STATE_IDLE;
	}

	/* Rotate Data Protection Key */
	if (0 != fdo_generate_storage_hmac_key()) {
		LOG(LOG_ERROR, "TO2.Done: Failed to rotate data protection key.\n");
	}
	LOG(LOG_DEBUG, "TO2.Done: Data protection key rotated successfully!!\n");

	/* Write new device credentials */
	if (store_credential(ps->dev_cred) != 0) {
		LOG(LOG_ERROR, "TO2.Done: Failed to store new device creds\n");
		goto err;
	}
	LOG(LOG_DEBUG, "TO2.Done: Updated device with new credentials\n");

	// Do not point to ps->osc contents anymore.
	// This keeps it clean for freeing memory at TO2 exit at all times.
	ps->dev_cred->owner_blk->guid = NULL;
	ps->dev_cred->owner_blk->rvlst = NULL;
	ps->dev_cred->owner_blk->pk = NULL;

	fdow_next_block(&ps->fdow, FDO_TO2_DONE);

	if (!fdow_start_array(&ps->fdow, 1)) {
		LOG(LOG_ERROR, "TO2.Done: Failed to start array\n");
		return false;
	}

	if(!ps->nonce_to2provedv) {
		LOG(LOG_ERROR, "TO2.Done: NonceTO2ProveDv not found\n");
		return false;
	}

	if (!fdow_byte_string(&ps->fdow, ps->nonce_to2provedv->bytes,
		ps->nonce_to2provedv->byte_sz)) {
		LOG(LOG_ERROR, "TO2.Done: Failed to write NonceTO2ProveDv\n");
		return false;
	}

	if (!fdow_end_array(&ps->fdow)) {
		LOG(LOG_ERROR, "TO2.Done: Failed to end array\n");
		return false;
	}

	if (!fdo_encrypted_packet_windup(&ps->fdow, FDO_TO2_DONE, ps->iv)) {
		LOG(LOG_ERROR, "TO2.Done: Failed to create Encrypted Message\n");
		goto err;
	}

	ps->success = true;
	ps->state = FDO_STATE_TO2_RCV_DONE_2;
	LOG(LOG_DEBUG, "TO2.Done completed successfully\n");
	ret = 0; /*Mark as success */

err:
	if (hmac)
		fdo_hash_free(hmac);
	return ret;
}