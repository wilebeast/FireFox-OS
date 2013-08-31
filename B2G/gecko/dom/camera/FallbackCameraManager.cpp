/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMCameraManager.h"

// From nsDOMCameraManager.

/* void getListOfCameras ([optional] out unsigned long aCount, [array, size_is (aCount), retval] out string aCameras); */
NS_IMETHODIMP
nsDOMCameraManager::GetListOfCameras(uint32_t *aCount, char * **aCameras)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}
