/***************************************************************************/ /**
 * @file sli_net_credentials.h
 * @brief Internal network credentials API
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#ifndef SLI_NET_CREDENTIALS_H
#define SLI_NET_CREDENTIALS_H

#include "sl_status.h"
#include "sl_net_types.h"

/***************************************************************************/ /**
 * @brief
 *   Set a network credential including client credentials, certificates, and keys.
 * 
 * @details
 *   This function sets the network credential type and data for the specified credential ID.
 *   The credential data can include client credentials, certificates, and keys.
 * 
 *   Repeatedly calling this API with the same ID will overwrite the existing credential type and data.
 *  
 * @pre Pre-conditions:
 * - @ref sl_net_init should be called before this API.
 * 
 * @param[in] id
 *   Network credential identifier as identified by @ref sl_net_credential_id_t.
 * 
 * @param[in] type
 *   Network credential type as identified by @ref sl_net_credential_type_t.
 * 
 * @param[in] credential
 *   Pointer to the credential data object.
 * 
 * @param[in] credential_length
 *   Length of the credential data object.
 * 
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 *   If the credential is NULL or the credential length is zero, this API will return an error `SL_STATUS_INVALID_PARAMETER`.
 *   If the credential_length exceeds NVM3_DEFAULT_MAX_OBJECT_SIZE when using NVM3 storage, this API will return an error `SL_STATUS_NVM3_WRITE_DATA_SIZE`.
 * @note
 * - Certificates should follow the standard array format.  
 * - After every 64 bytes, the special character `\n` should be used as a delimiter.
 * - The private key is prefixed with a header like "-----BEGIN PRIVATE KEY-----" line and postfixed with an footer like"-----END PRIVATE KEY-----". 
 * - Certificates are prefixed with a header like "-----BEGIN CERTIFICATE-----" line and postfixed with an footer like"-----END CERTIFICATE-----" line. 
 * - Text outside the prefix and postfix lines is ignored and can be used for metadata.
 * - The above mentioned Headers and Footers might vary 
 * - This API does not support the OPEN Security type for Wi-Fi client credentials.
 * - When configuring AP or client interfaces in open security mode, the credential ID must be set to `SL_NET_NO_CREDENTIAL_ID`.
 ******************************************************************************/
sl_status_t sli_net_set_credential(sl_net_credential_id_t id,
                                   sl_net_credential_type_t type,
                                   const void *credential,
                                   uint32_t credential_length);

/***************************************************************************/ /**
 * @brief
 *   Retrieve a stored network credential.
 * 
 * @details
 *   This function retrieves the network credential data for the specified credential ID.
 *   The retrieved credential data is stored in the provided credential object.
 * 
 * @pre Pre-conditions:
 * - @ref sl_net_init should be called before this API.
 * 
 * @param[in] id
 *   Network credential identifier as identified by @ref sl_net_credential_id_t.
 * 
 * @param[out] type
 *   Network credential type as identified by @ref sl_net_credential_type_t.
 * 
 * @param[out] credential
 *   Pointer to location where credential data is stored.
 * 
 * @param[in,out] credential_length
 *   in: Number of bytes available at credential,
 *   out: Number of bytes written.
 * 
 * @return
 *   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
 * 
 * @note
 *   Currently, @ref SL_NET_CERTIFICATE and @ref SL_NET_SIGNING_CERTIFICATE are not supported for retrieval.
 ******************************************************************************/
sl_status_t sli_net_get_credential(sl_net_credential_id_t id,
                                   sl_net_credential_type_t *type,
                                   void *credential,
                                   uint32_t *credential_length);

/***************************************************************************/ /**
* @brief
*   Delete a stored network credential.
* 
* @details
*   This function deletes the network credential data for the specified credential ID and type.
*   Once deleted, the credential cannot be used for network operations.
* 
* @pre Pre-conditions:
* -
*   @ref sl_net_init should be called before this API.
* 
* @param[in] id
*   Network credential identifier as identified by @ref sl_net_credential_id_t.
* 
* @param[out] type
*   Network credential type as identified by @ref sl_net_credential_type_t.
* 
* @return
*   sl_status_t. See [Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/status) and [WiSeConnect Status Codes](../wiseconnect-api-reference-guide-err-codes/wiseconnect-status-codes) for details.
******************************************************************************/
sl_status_t sli_net_delete_credential(sl_net_credential_id_t id, sl_net_credential_type_t type);

#endif // SLI_NET_CREDENTIALS_H
