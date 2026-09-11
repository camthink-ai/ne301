/*******************************************************************************
 * @file  sl_net_credentials.c
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

#include "sli_net_credentials.h"
#include "sli_net_common_utility.h"
#include "sl_net_si91x.h"

#define CRED_TYPE_CERT 0
#define CRED_TYPE_CRED 1

sl_status_t sl_net_set_credential(sl_net_credential_id_t id,
                                  sl_net_credential_type_t type,
                                  const void *credential,
                                  uint32_t credential_length)
{

  if (CRED_TYPE_CERT == sli_net_check_cred_type(type)) {
    return sli_si91x_set_credential(id, type, credential, credential_length);
  }

  return sli_net_set_credential(id, type, credential, credential_length);
}

sl_status_t sl_net_get_credential(sl_net_credential_id_t id,
                                  sl_net_credential_type_t *type,
                                  void *credential,
                                  uint32_t *credential_length)
{
  // Check if the credential ID is one of the invalid parameters
  if (id == SL_NET_WIFI_EAP_SERVER_CREDENTIAL_ID || (SL_NET_TLS_CLIENT_CREDENTIAL_START == (id & ~0xff))
      || (SL_NET_TLS_SERVER_CREDENTIAL_START == (id & ~0xff))) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  return sli_net_get_credential(id, type, credential, credential_length);
}

sl_status_t sl_net_delete_credential(sl_net_credential_id_t id, sl_net_credential_type_t type)
{
  if (CRED_TYPE_CERT == sli_net_check_cred_type(type)) {
    return sli_si91x_delete_credential(id, type);
  }
  return sli_net_delete_credential(id, type);
}
