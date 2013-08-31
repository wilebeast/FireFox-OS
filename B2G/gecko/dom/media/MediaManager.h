/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngine.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/Services.h"
#include "mozilla/unused.h"

#include "nsHashKeys.h"
#include "nsGlobalWindow.h"
#include "nsClassHashtable.h"
#include "nsRefPtrHashtable.h"
#include "nsObserverService.h"

#include "nsPIDOMWindow.h"
#include "nsIDOMNavigatorUserMedia.h"
#include "nsXULAppAPI.h"
#include "mozilla/Attributes.h"

namespace mozilla {

class GetUserMediaNotificationEvent: public nsRunnable
{
  public:
    enum GetUserMediaStatus {
      STARTING,
      STOPPING
    };
    GetUserMediaNotificationEvent(GetUserMediaStatus aStatus)
    : mStatus(aStatus) {}

    NS_IMETHOD
    Run()
    {
      nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
      if (!obs) {
        NS_WARNING("Could not get the Observer service for GetUserMedia recording notification.");
        return NS_ERROR_FAILURE;
      }
      if (mStatus) {
        obs->NotifyObservers(nullptr,
            "recording-device-events",
            NS_LITERAL_STRING("starting").get());
        // Forward recording events to parent process.
        // The events are gathered in chrome process and used for recording indicator
        if (XRE_GetProcessType() != GeckoProcessType_Default) {
          unused << mozilla::dom::ContentChild::GetSingleton()->SendRecordingDeviceEvents(NS_LITERAL_STRING("starting"));
        }
      } else {
        obs->NotifyObservers(nullptr,
            "recording-device-events",
            NS_LITERAL_STRING("shutdown").get());
        // Forward recording events to parent process.
        // The events are gathered in chrome process and used for recording indicator
        if (XRE_GetProcessType() != GeckoProcessType_Default) {
          unused << mozilla::dom::ContentChild::GetSingleton()->SendRecordingDeviceEvents(NS_LITERAL_STRING("shutdown"));
        }
      }
      return NS_OK;
    }

  protected:
    GetUserMediaStatus mStatus;
};

/**
 * This class is an implementation of MediaStreamListener. This is used
 * to Start() and Stop() the underlying MediaEngineSource when MediaStreams
 * are assigned and deassigned in content.
 */
class GetUserMediaCallbackMediaStreamListener : public MediaStreamListener
{
public:
  GetUserMediaCallbackMediaStreamListener(MediaEngineSource* aSource,
    nsDOMMediaStream* aStream, TrackID aListenId)
    : mSource(aSource)
    , mStream(aStream)
    , mID(aListenId)
    , mValid(true) {}

  void
  Invalidate()
  {
    if (!mValid) {
      return;
    }

    mValid = false;
    mSource->Stop();
    mSource->Deallocate();

    nsCOMPtr<GetUserMediaNotificationEvent> event =
      new GetUserMediaNotificationEvent(GetUserMediaNotificationEvent::STOPPING);

    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }

  void
  NotifyConsumptionChanged(MediaStreamGraph* aGraph, Consumption aConsuming)
  {
    if (aConsuming == CONSUMED) {
      SourceMediaStream* stream = mStream->GetStream()->AsSourceStream();
      mSource->Start(stream, mID);
      nsCOMPtr<GetUserMediaNotificationEvent> event =
        new GetUserMediaNotificationEvent(GetUserMediaNotificationEvent::STARTING);

      NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
      return;
    }

    // NOT_CONSUMED
    Invalidate();
    return;
  }

  void NotifyBlockingChanged(MediaStreamGraph* aGraph, Blocking aBlocked) {}
  void NotifyOutput(MediaStreamGraph* aGraph) {}
  void NotifyFinished(MediaStreamGraph* aGraph) {}
  void NotifyQueuedTrackChanges(MediaStreamGraph* aGraph, TrackID aID,
    TrackRate aTrackRate, TrackTicks aTrackOffset,
    uint32_t aTrackEvents, const MediaSegment& aQueuedMedia) {}

private:
  nsRefPtr<MediaEngineSource> mSource;
  nsCOMPtr<nsDOMMediaStream> mStream;
  TrackID mID;
  bool mValid;
};

typedef nsTArray<nsRefPtr<GetUserMediaCallbackMediaStreamListener> > StreamListeners;
typedef nsClassHashtable<nsUint64HashKey, StreamListeners> WindowTable;

class MediaDevice : public nsIMediaDevice
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMEDIADEVICE

  MediaDevice(MediaEngineVideoSource* aSource) {
    mSource = aSource;
    mType.Assign(NS_LITERAL_STRING("video"));
    mSource->GetName(mName);
  };
  MediaDevice(MediaEngineAudioSource* aSource) {
    mSource = aSource;
    mType.Assign(NS_LITERAL_STRING("audio"));
    mSource->GetName(mName);
  };
  virtual ~MediaDevice() {};

  MediaEngineSource* GetSource();
private:
  nsString mName;
  nsString mType;
  nsRefPtr<MediaEngineSource> mSource;
};

class MediaManager MOZ_FINAL : public nsIObserver
{
public:
  static MediaManager* Get() {
    if (!sSingleton) {
      sSingleton = new MediaManager();

      NS_ASSERTION(NS_IsMainThread(), "Only create MediaManager on main thread");
      nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
      obs->AddObserver(sSingleton, "xpcom-shutdown", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:allow", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:deny", false);
    }
    return sSingleton;
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  MediaEngine* GetBackend();
  WindowTable* GetActiveWindows();

  nsresult GetUserMedia(bool aPrivileged, nsPIDOMWindow* aWindow,
    nsIMediaStreamOptions* aParams,
    nsIDOMGetUserMediaSuccessCallback* onSuccess,
    nsIDOMGetUserMediaErrorCallback* onError);
  nsresult GetUserMediaDevices(nsPIDOMWindow* aWindow,
    nsIGetUserMediaDevicesSuccessCallback* onSuccess,
    nsIDOMGetUserMediaErrorCallback* onError);
  void OnNavigation(uint64_t aWindowID);

private:
  // Make private because we want only one instance of this class
  MediaManager()
  : mBackend(nullptr)
  , mMediaThread(nullptr) {
    mActiveWindows.Init();
    mActiveCallbacks.Init();
  };
  MediaManager(MediaManager const&) {};

  ~MediaManager() {
    delete mBackend;
  };

  MediaEngine* mBackend;
  nsCOMPtr<nsIThread> mMediaThread;
  WindowTable mActiveWindows;
  nsRefPtrHashtable<nsStringHashKey, nsRunnable> mActiveCallbacks;

  static nsRefPtr<MediaManager> sSingleton;
};

} // namespace mozilla
