/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothprofilemanagerbase_h__
#define mozilla_dom_bluetooth_bluetoothprofilemanagerbase_h__

/**
 * Error Messages used in Bluetooth profiles
 *
 * These error messages would be sent to Gaia as an argument of onError event.
 */
#define ERR_ALREADY_CONNECTED           "AlreadyConnectedError"
#define ERR_NO_AVAILABLE_RESOURCE       "NoAvailableResourceError"
#define ERR_REACHED_CONNECTION_LIMIT    "ReachedConnectionLimitError"
#define ERR_SERVICE_CHANNEL_NOT_FOUND   "DeviceChannelRetrievalError"

#include "BluetoothCommon.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothProfileManagerBase
{
public:
  virtual void OnGetServiceChannel(const nsAString& aDeviceAddress,
                                   const nsAString& aServiceUuid,
                                   int aChannel) = 0;

  virtual void OnUpdateSdpRecords(const nsAString& aDeviceAddress) = 0;
};

END_BLUETOOTH_NAMESPACE

#endif  //#ifndef mozilla_dom_bluetooth_bluetoothprofilemanagerbase_h__
