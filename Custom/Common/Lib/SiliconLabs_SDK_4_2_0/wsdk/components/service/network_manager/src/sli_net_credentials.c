/*******************************************************************************
 * @file  sli_net_credentials.c
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

#include "sl_net.h"
#include "sl_net_default_values.h"
#include "sl_wifi_credentials.h"
#include "sl_common.h"
#include "sli_net_common_utility.h"
#include <string.h>

#include "sl_constants.h"
#define CRED_TYPE_CERT 0
#define CRED_TYPE_CRED 1

// [SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID]
// [SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID]

sl_status_t sli_net_set_credential(sl_net_credential_id_t id,
                                   sl_net_credential_type_t type,
                                   const void *credential,
                                   uint32_t credential_length)
{
  // Check if the credential is invalid parameter
  if ((credential == NULL) || (credential_length == 0)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  int group_id                        = 0;
  int cred_id                         = 0;
  sl_status_t status                  = 0;
  sl_wifi_credential_type_t cred_type = 0;

  group_id = (id & SL_NET_CREDENTIAL_GROUP_MASK);

  if (group_id > 0) {
    cred_id = (SL_NET_USER_CREDENTIAL_ID + (group_id >> 8));
  } else {
    group_id = id;
    cred_id  = id;
  }

  switch (type) {
    case SL_NET_WIFI_PSK: {
      // PSK must be between 8 and 63 characters long
      if ((credential_length < SL_WIFI_MIN_PSK_LENGTH) || (credential_length > (SL_WIFI_MAX_PSK_LENGTH - 1))) {
        SL_DEBUG_LOG_V2(ERROR, "PSK: Invalid credential length: %ld\r\n", (long)credential_length);
        return SL_STATUS_SI91X_INVALID_PSK_LENGTH;
      }
      break;
    }
    case SL_NET_WIFI_PMK: {
      // PMK must be exactly 32 bytes in hex format
      if (credential_length != SL_WIFI_MAX_PMK_LENGTH) {
        SL_DEBUG_LOG_V2(ERROR, "PMK: Invalid credential length: %ld\r\n", (long)credential_length);
        return SL_STATUS_SI91X_INVALID_PMK_LEN;
      }
      break;
    }
    default:
      break;
  }

  switch (group_id) {
    case SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID:
    case SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID:
    case SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID:
    case SL_NET_WIFI_EAP_SERVER_CREDENTIAL_ID:
    case SL_NET_WIFI_CLIENT_CREDENTIAL_ID_1:
    case SL_NET_WIFI_AP_CREDENTIAL_ID_1:
      status = sli_net_get_wifi_credential_type(type, &cred_type);
      VERIFY_STATUS_AND_RETURN(status);
      break;
    case SL_NET_USER_CREDENTIAL_ID:
      status = sli_net_get_wifi_credential_type(type, &cred_type);
      if (status != SL_STATUS_OK) {
        cred_type = (SL_WIFI_USER_CREDENTIAL | type);
      }
      break;
    case SL_NET_TLS_CLIENT_CREDENTIAL_START:
    case SL_NET_TLS_SERVER_CREDENTIAL_START:
    case SL_NET_MQTT_SERVER_CREDENTIAL_START:
    case SL_NET_MQTT_CLIENT_CREDENTIAL_START:
    case SL_NET_HTTP_SERVER_CREDENTIAL_START:
    case SL_NET_HTTP_CLIENT_CREDENTIAL_START:
      cred_type = (SL_WIFI_USER_CREDENTIAL | type);
      break;

    default:
      return SL_STATUS_FAIL;
  }

  return sl_wifi_set_credential(cred_id, cred_type, credential, credential_length);
}

sl_status_t sli_net_get_credential(sl_net_credential_id_t id,
                                   sl_net_credential_type_t *type,
                                   void *credential,
                                   uint32_t *credential_length)
{
  // Check if the credential is invalid parameter
  if ((NULL == credential) || (0 == *credential_length)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  int group_id                        = 0;
  int cred_id                         = 0;
  sl_status_t status                  = 0;
  sl_wifi_credential_type_t cred_type = 0;

  group_id = (id & SL_NET_CREDENTIAL_GROUP_MASK);

  if (group_id > 0) {
    cred_id = (SL_NET_USER_CREDENTIAL_ID + (group_id >> 8));
  } else {
    group_id = id;
    cred_id  = id;
  }

  status = sl_wifi_get_credential(cred_id, &cred_type, credential, credential_length);
  VERIFY_STATUS_AND_RETURN(status);

  switch (group_id) {
    case SL_NET_DEFAULT_WIFI_AP_CREDENTIAL_ID:
    case SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID:
    case SL_NET_WIFI_EAP_CLIENT_CREDENTIAL_ID:
    case SL_NET_WIFI_EAP_SERVER_CREDENTIAL_ID:
    case SL_NET_WIFI_CLIENT_CREDENTIAL_ID_1:
    case SL_NET_WIFI_AP_CREDENTIAL_ID_1:
      sli_net_get_net_credential_type(cred_type, type);
      break;
    case SL_NET_USER_CREDENTIAL_ID:
    case SL_NET_TLS_CLIENT_CREDENTIAL_START:
    case SL_NET_TLS_SERVER_CREDENTIAL_START:
    case SL_NET_MQTT_SERVER_CREDENTIAL_START:
    case SL_NET_MQTT_CLIENT_CREDENTIAL_START:
    case SL_NET_HTTP_SERVER_CREDENTIAL_START:
    case SL_NET_HTTP_CLIENT_CREDENTIAL_START:
      *type = ((~SL_WIFI_USER_CREDENTIAL) & cred_type);
      break;
    default:
      return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

sl_status_t sli_net_delete_credential(sl_net_credential_id_t id, sl_net_credential_type_t type)
{

  UNUSED_PARAMETER(type);

  int group_id = 0;
  int cred_id  = 0;

  group_id = (id & SL_NET_CREDENTIAL_GROUP_MASK);

  if (group_id > 0) {
    cred_id = (SL_NET_USER_CREDENTIAL_ID + (group_id >> 8));
  } else {
    cred_id = id;
  }

  return sl_wifi_delete_credential(cred_id);
}
