/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "Blob.h"

#include "nsIDOMFile.h"
#include "nsIInputStream.h"
#include "nsIMultiplexInputStream.h"
#include "nsIIPCSerializableInputStream.h"
#include "nsIRemoteBlob.h"
#include "nsISeekableStream.h"

#include "mozilla/Assertions.h"
#include "mozilla/Monitor.h"
#include "mozilla/unused.h"
#include "mozilla/Util.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsDOMFile.h"
#include "nsDOMBlobBuilder.h"
#include "nsThreadUtils.h"

#include "ContentChild.h"
#include "ContentParent.h"

#define PRIVATE_REMOTE_INPUT_STREAM_IID \
  {0x30c7699f, 0x51d2, 0x48c8, {0xad, 0x56, 0xc0, 0x16, 0xd7, 0x6f, 0x71, 0x27}}

using namespace mozilla::dom;
using namespace mozilla::dom::ipc;
using namespace mozilla::ipc;

namespace {

class NS_NO_VTABLE IPrivateRemoteInputStream : public nsISupports
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(PRIVATE_REMOTE_INPUT_STREAM_IID)

  // This will return the underlying stream.
  virtual nsIInputStream*
  BlockAndGetInternalStream() = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(IPrivateRemoteInputStream,
                              PRIVATE_REMOTE_INPUT_STREAM_IID)

template <template <class> class SmartPtr, class T>
void
ProxyReleaseToMainThread(SmartPtr<T>& aDoomed)
{
  MOZ_ASSERT(!NS_IsMainThread());

  T* doomed;
  aDoomed.forget(&doomed);

  nsCOMPtr<nsIRunnable> runnable =
    NS_NewNonOwningRunnableMethod(doomed, &T::Release);
  if (NS_FAILED(NS_DispatchToMainThread(runnable, NS_DISPATCH_NORMAL))) {
    NS_WARNING("Failed to proxy release, leaking!");
  }
}

template <ActorFlavorEnum ActorFlavor>
already_AddRefed<nsIDOMBlob>
BlobFromExistingActor(const BlobConstructorNoMultipartParams& aParams)
{
  MOZ_STATIC_ASSERT(ActorFlavor == Parent,
                    "There can only be Parent and Child, and Child should "
                    "instantiate below!");

  MOZ_ASSERT(aParams.type() == BlobConstructorNoMultipartParams::TPBlobParent);

  Blob<Parent>* actor = static_cast<Blob<Parent>*>(aParams.get_PBlobParent());
  MOZ_ASSERT(actor);

  return actor->GetBlob();
}

template <>
already_AddRefed<nsIDOMBlob>
BlobFromExistingActor<Child>(const BlobConstructorNoMultipartParams& aParams)
{
  MOZ_ASSERT(aParams.type() == BlobConstructorNoMultipartParams::TPBlobChild);

  Blob<Child>* actor = static_cast<Blob<Child>*>(aParams.get_PBlobChild());
  MOZ_ASSERT(actor);

  return actor->GetBlob();
}

// This class exists to keep a blob alive at least as long as its internal
// stream.
class BlobInputStreamTether : public nsIMultiplexInputStream,
                              public nsISeekableStream,
                              public nsIIPCSerializableInputStream
{
  nsCOMPtr<nsIInputStream> mStream;
  nsCOMPtr<nsIDOMBlob> mSourceBlob;

  nsIMultiplexInputStream* mWeakMultiplexStream;
  nsISeekableStream* mWeakSeekableStream;
  nsIIPCSerializableInputStream* mWeakSerializableStream;

public:
  NS_DECL_ISUPPORTS
  NS_FORWARD_NSIINPUTSTREAM(mStream->)
  NS_FORWARD_SAFE_NSIMULTIPLEXINPUTSTREAM(mWeakMultiplexStream)
  NS_FORWARD_SAFE_NSISEEKABLESTREAM(mWeakSeekableStream)
  NS_FORWARD_SAFE_NSIIPCSERIALIZABLEINPUTSTREAM(mWeakSerializableStream)

  BlobInputStreamTether(nsIInputStream* aStream, nsIDOMBlob* aSourceBlob)
  : mStream(aStream), mSourceBlob(aSourceBlob), mWeakMultiplexStream(nullptr),
    mWeakSeekableStream(nullptr), mWeakSerializableStream(nullptr)
  {
    MOZ_ASSERT(aStream);
    MOZ_ASSERT(aSourceBlob);

    nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_QueryInterface(aStream);
    if (multiplexStream) {
      MOZ_ASSERT(SameCOMIdentity(aStream, multiplexStream));
      mWeakMultiplexStream = multiplexStream;
    }

    nsCOMPtr<nsISeekableStream> seekableStream = do_QueryInterface(aStream);
    if (seekableStream) {
      MOZ_ASSERT(SameCOMIdentity(aStream, seekableStream));
      mWeakSeekableStream = seekableStream;
    }

    nsCOMPtr<nsIIPCSerializableInputStream> serializableStream =
      do_QueryInterface(aStream);
    if (serializableStream) {
      MOZ_ASSERT(SameCOMIdentity(aStream, serializableStream));
      mWeakSerializableStream = serializableStream;
    }
  }

protected:
  virtual ~BlobInputStreamTether()
  {
    MOZ_ASSERT(mStream);
    MOZ_ASSERT(mSourceBlob);

    if (!NS_IsMainThread()) {
      mStream = nullptr;
      ProxyReleaseToMainThread(mSourceBlob);
    }
  }
};

NS_IMPL_THREADSAFE_ADDREF(BlobInputStreamTether)
NS_IMPL_THREADSAFE_RELEASE(BlobInputStreamTether)

NS_INTERFACE_MAP_BEGIN(BlobInputStreamTether)
  NS_INTERFACE_MAP_ENTRY(nsIInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIMultiplexInputStream,
                                     mWeakMultiplexStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISeekableStream, mWeakSeekableStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIIPCSerializableInputStream,
                                     mWeakSerializableStream)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInputStream)
NS_INTERFACE_MAP_END

class RemoteInputStream : public nsIInputStream,
                          public nsISeekableStream,
                          public nsIIPCSerializableInputStream,
                          public IPrivateRemoteInputStream
{
  mozilla::Monitor mMonitor;
  nsCOMPtr<nsIInputStream> mStream;
  nsCOMPtr<nsIDOMBlob> mSourceBlob;
  nsISeekableStream* mWeakSeekableStream;
  ActorFlavorEnum mOrigin;

public:
  NS_DECL_ISUPPORTS

  RemoteInputStream(nsIDOMBlob* aSourceBlob, ActorFlavorEnum aOrigin)
  : mMonitor("RemoteInputStream.mMonitor"), mSourceBlob(aSourceBlob),
    mWeakSeekableStream(nullptr), mOrigin(aOrigin)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aSourceBlob);
  }

  void
  Serialize(InputStreamParams& aParams)
  {
    nsCOMPtr<nsIRemoteBlob> remote = do_QueryInterface(mSourceBlob);
    MOZ_ASSERT(remote);

    if (mOrigin == Parent) {
      aParams = RemoteInputStreamParams(
        static_cast<PBlobParent*>(remote->GetPBlob()), nullptr);
    } else {
      aParams = RemoteInputStreamParams(
        nullptr, static_cast<PBlobChild*>(remote->GetPBlob()));
    }
  }

  bool
  Deserialize(const InputStreamParams& aParams)
  {
    // See InputStreamUtils.cpp to see how deserialization of a
    // RemoteInputStream is special-cased.
    MOZ_NOT_REACHED("RemoteInputStream should never be deserialized");
    return false;
  }

  void
  SetStream(nsIInputStream* aStream)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aStream);

    nsCOMPtr<nsIInputStream> stream = aStream;
    nsCOMPtr<nsISeekableStream> seekableStream = do_QueryInterface(aStream);

    MOZ_ASSERT_IF(seekableStream, SameCOMIdentity(aStream, seekableStream));

    {
      mozilla::MonitorAutoLock lock(mMonitor);

      MOZ_ASSERT(!mStream);
      MOZ_ASSERT(!mWeakSeekableStream);

      mStream.swap(stream);
      mWeakSeekableStream = seekableStream;

      mMonitor.Notify();
    }
  }

  NS_IMETHOD
  Close() MOZ_OVERRIDE
  {
    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDOMBlob> sourceBlob;
    mSourceBlob.swap(sourceBlob);

    rv = mStream->Close();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  Available(uint64_t* aAvailable) MOZ_OVERRIDE
  {
    // See large comment in FileInputStreamWrapper::Available.
    if (NS_IsMainThread()) {
      return NS_BASE_STREAM_CLOSED;
    }

    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mStream->Available(aAvailable);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  Read(char* aBuffer, uint32_t aCount, uint32_t* aResult) MOZ_OVERRIDE
  {
    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mStream->Read(aBuffer, aCount, aResult);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  ReadSegments(nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
               uint32_t* aResult) MOZ_OVERRIDE
  {
    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mStream->ReadSegments(aWriter, aClosure, aCount, aResult);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) MOZ_OVERRIDE
  {
    NS_ENSURE_ARG_POINTER(aNonBlocking);

    *aNonBlocking = false;
    return NS_OK;
  }

  NS_IMETHOD
  Seek(int32_t aWhence, int64_t aOffset) MOZ_OVERRIDE
  {
    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mWeakSeekableStream) {
      NS_WARNING("Underlying blob stream is not seekable!");
      return NS_ERROR_NO_INTERFACE;
    }

    rv = mWeakSeekableStream->Seek(aWhence, aOffset);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  Tell(int64_t* aResult)
  {
    // We can cheat here and assume that we're going to start at 0 if we don't
    // yet have our stream. Though, really, this should abort since most input
    // streams could block here.
    if (NS_IsMainThread() && !mStream) {
      *aResult = 0;
      return NS_OK;
    }

    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mWeakSeekableStream) {
      NS_WARNING("Underlying blob stream is not seekable!");
      return NS_ERROR_NO_INTERFACE;
    }

    rv = mWeakSeekableStream->Tell(aResult);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD
  SetEOF()
  {
    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mWeakSeekableStream) {
      NS_WARNING("Underlying blob stream is not seekable!");
      return NS_ERROR_NO_INTERFACE;
    }

    rv = mWeakSeekableStream->SetEOF();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  virtual nsIInputStream*
  BlockAndGetInternalStream()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    nsresult rv = BlockAndWaitForStream();
    NS_ENSURE_SUCCESS(rv, nullptr);

    return mStream;
  }

private:
  virtual ~RemoteInputStream()
  {
    if (!NS_IsMainThread()) {
      mStream = nullptr;
      mWeakSeekableStream = nullptr;

      ProxyReleaseToMainThread(mSourceBlob);
    }
  }

  void
  ReallyBlockAndWaitForStream()
  {
    mozilla::DebugOnly<bool> waited;

    {
      mozilla::MonitorAutoLock lock(mMonitor);

      waited = !mStream;

      while (!mStream) {
        mMonitor.Wait();
      }
    }

    MOZ_ASSERT(mStream);

#ifdef DEBUG
    if (waited && mWeakSeekableStream) {
      int64_t position;
      MOZ_ASSERT(NS_SUCCEEDED(mWeakSeekableStream->Tell(&position)),
                 "Failed to determine initial stream position!");
      MOZ_ASSERT(!position, "Stream not starting at 0!");
    }
#endif
  }

  nsresult
  BlockAndWaitForStream()
  {
    if (NS_IsMainThread()) {
      NS_WARNING("Blocking the main thread is not supported!");
      return NS_ERROR_FAILURE;
    }

    ReallyBlockAndWaitForStream();

    return NS_OK;
  }

  bool
  IsSeekableStream()
  {
    if (NS_IsMainThread()) {
      if (!mStream) {
        NS_WARNING("Don't know if this stream is seekable yet!");
        return true;
      }
    }
    else {
      ReallyBlockAndWaitForStream();
    }

    return !!mWeakSeekableStream;
  }
};

NS_IMPL_THREADSAFE_ADDREF(RemoteInputStream)
NS_IMPL_THREADSAFE_RELEASE(RemoteInputStream)

NS_INTERFACE_MAP_BEGIN(RemoteInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIInputStream)
  NS_INTERFACE_MAP_ENTRY(nsIIPCSerializableInputStream)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISeekableStream, IsSeekableStream())
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInputStream)
  NS_INTERFACE_MAP_ENTRY(IPrivateRemoteInputStream)
NS_INTERFACE_MAP_END

template <ActorFlavorEnum ActorFlavor>
class InputStreamActor : public BlobTraits<ActorFlavor>::StreamType
{
  nsRefPtr<RemoteInputStream> mRemoteStream;

public:
  InputStreamActor(RemoteInputStream* aRemoteStream)
  : mRemoteStream(aRemoteStream)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aRemoteStream);
  }

  InputStreamActor()
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

private:
  // This method is only called by the IPDL message machinery.
  virtual bool
  Recv__delete__(const InputStreamParams& aParams) MOZ_OVERRIDE
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mRemoteStream);

    nsCOMPtr<nsIInputStream> stream = DeserializeInputStream(aParams);
    if (!stream) {
      return false;
    }

    mRemoteStream->SetStream(stream);
    return true;
  }
};

template <ActorFlavorEnum ActorFlavor>
inline
already_AddRefed<nsIDOMBlob>
GetBlobFromParams(const SlicedBlobConstructorParams& aParams)
{
  MOZ_STATIC_ASSERT(ActorFlavor == mozilla::dom::ipc::Parent,
                    "No other flavor is supported here!");

  BlobParent* actor =
    const_cast<BlobParent*>(
      static_cast<const BlobParent*>(aParams.sourceParent()));
  MOZ_ASSERT(actor);

  return actor->GetBlob();
}

template <>
inline
already_AddRefed<nsIDOMBlob>
GetBlobFromParams<Child>(const SlicedBlobConstructorParams& aParams)
{
  BlobChild* actor =
    const_cast<BlobChild*>(
      static_cast<const BlobChild*>(aParams.sourceChild()));
  MOZ_ASSERT(actor);

  return actor->GetBlob();
}

inline
void
SetBlobOnParams(BlobChild* aActor, SlicedBlobConstructorParams& aParams)
{
  aParams.sourceChild() = aActor;
}

inline
void
SetBlobOnParams(BlobParent* aActor, SlicedBlobConstructorParams& aParams)
{
  aParams.sourceParent() = aActor;
}

inline
nsDOMFileBase*
ToConcreteBlob(nsIDOMBlob* aBlob)
{
  // XXX This is only safe so long as all blob implementations in our tree
  //     inherit nsDOMFileBase. If that ever changes then this will need to grow
  //     a real interface or something.
  return static_cast<nsDOMFileBase*>(aBlob);
}

} // anonymous namespace

namespace mozilla {
namespace dom {
namespace ipc {

// Each instance of this class will be dispatched to the network stream thread
// pool to run the first time where it will open the file input stream. It will
// then dispatch itself back to the main thread to send the child process its
// response (assuming that the child has not crashed). The runnable will then
// dispatch itself to the thread pool again in order to close the file input
// stream.
class BlobTraits<Parent>::BaseType::OpenStreamRunnable : public nsRunnable
{
  friend class nsRevocableEventPtr<OpenStreamRunnable>;

  typedef BlobTraits<Parent> TraitsType;
  typedef TraitsType::BaseType BlobActorType;
  typedef TraitsType::StreamType BlobStreamProtocolType;

  // Only safe to access these pointers if mRevoked is false!
  BlobActorType* mBlobActor;
  BlobStreamProtocolType* mStreamActor;

  nsCOMPtr<nsIInputStream> mStream;
  nsCOMPtr<nsIIPCSerializableInputStream> mSerializable;
  nsCOMPtr<nsIEventTarget> mTarget;

  bool mRevoked;
  bool mClosing;

public:
  OpenStreamRunnable(BlobActorType* aBlobActor,
                     BlobStreamProtocolType* aStreamActor,
                     nsIInputStream* aStream,
                     nsIIPCSerializableInputStream* aSerializable,
                     nsIEventTarget* aTarget)
  : mBlobActor(aBlobActor), mStreamActor(aStreamActor), mStream(aStream),
    mSerializable(aSerializable), mTarget(aTarget), mRevoked(false),
    mClosing(false)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aBlobActor);
    MOZ_ASSERT(aStreamActor);
    MOZ_ASSERT(aStream);
    // aSerializable may be null.
    MOZ_ASSERT(aTarget);
  }

  NS_IMETHOD
  Run()
  {
    if (NS_IsMainThread()) {
      return SendResponse();
    }

    if (!mClosing) {
      return OpenStream();
    }

    return CloseStream();
  }

  nsresult
  Dispatch()
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mTarget);

    nsresult rv = mTarget->Dispatch(this, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

private:
  void
  Revoke()
  {
    MOZ_ASSERT(NS_IsMainThread());
#ifdef DEBUG
    mBlobActor = nullptr;
    mStreamActor = nullptr;
#endif
    mRevoked = true;
  }

  nsresult
  OpenStream()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mStream);

    if (!mSerializable) {
      nsCOMPtr<IPrivateRemoteInputStream> remoteStream =
        do_QueryInterface(mStream);
      MOZ_ASSERT(remoteStream, "Must QI to IPrivateRemoteInputStream here!");

      nsCOMPtr<nsIInputStream> realStream =
        remoteStream->BlockAndGetInternalStream();
      NS_ENSURE_TRUE(realStream, NS_ERROR_FAILURE);

      mSerializable = do_QueryInterface(realStream);
      if (!mSerializable) {
        MOZ_ASSERT(false, "Must be serializable!");
        return NS_ERROR_FAILURE;
      }

      mStream.swap(realStream);
    }

    // To force the stream open we call Available(). We don't actually care
    // how much data is available.
    uint64_t available;
    if (NS_FAILED(mStream->Available(&available))) {
      NS_WARNING("Available failed on this stream!");
    }

    nsresult rv = NS_DispatchToMainThread(this, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  nsresult
  CloseStream()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mStream);

    // Going to always release here.
    nsCOMPtr<nsIInputStream> stream;
    mStream.swap(stream);

    nsresult rv = stream->Close();
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  nsresult
  SendResponse()
  {
    MOZ_ASSERT(NS_IsMainThread());

    MOZ_ASSERT(mStream);
    MOZ_ASSERT(mSerializable);
    MOZ_ASSERT(mTarget);
    MOZ_ASSERT(!mClosing);

    nsCOMPtr<nsIIPCSerializableInputStream> serializable;
    mSerializable.swap(serializable);

    if (mRevoked) {
      MOZ_ASSERT(!mBlobActor);
      MOZ_ASSERT(!mStreamActor);
    }
    else {
      MOZ_ASSERT(mBlobActor);
      MOZ_ASSERT(mStreamActor);

      InputStreamParams params;
      serializable->Serialize(params);

      MOZ_ASSERT(params.type() != InputStreamParams::T__None);

      unused << mStreamActor->Send__delete__(mStreamActor, params);

      mBlobActor->NoteRunnableCompleted(this);

#ifdef DEBUG
      mBlobActor = nullptr;
      mStreamActor = nullptr;
#endif
    }

    mClosing = true;

    nsCOMPtr<nsIEventTarget> target;
    mTarget.swap(target);

    nsresult rv = target->Dispatch(this, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }
};

BlobTraits<Parent>::BaseType::BaseType()
{
}

BlobTraits<Parent>::BaseType::~BaseType()
{
}

void
BlobTraits<Parent>::BaseType::NoteRunnableCompleted(
                    BlobTraits<Parent>::BaseType::OpenStreamRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  for (uint32_t index = 0; index < mOpenStreamRunnables.Length(); index++) {
    nsRevocableEventPtr<BaseType::OpenStreamRunnable>& runnable =
      mOpenStreamRunnables[index];

    if (runnable.get() == aRunnable) {
      runnable.Forget();
      mOpenStreamRunnables.RemoveElementAt(index);
      return;
    }
  }

  MOZ_NOT_REACHED("Runnable not in our array!");
}

template <ActorFlavorEnum ActorFlavor>
class RemoteBlobBase : public nsIRemoteBlob
{
public:
  typedef RemoteBlob<ActorFlavor> SelfType;
  typedef Blob<ActorFlavor> ActorType;
  typedef InputStreamActor<ActorFlavor> StreamActorType;

  void
  SetPBlob(void* aActor) MOZ_OVERRIDE
  {
    MOZ_ASSERT(!aActor || !mActor);
    mActor = static_cast<ActorType*>(aActor);
  }

  virtual void*
  GetPBlob() MOZ_OVERRIDE
  {
    return static_cast<typename ActorType::ProtocolType*>(mActor);
  }

private:
  ActorType* mActor;

protected:
  RemoteBlobBase()
  : mActor(nullptr)
  { }

  virtual
  ~RemoteBlobBase()
  {
    if (mActor) {
      mActor->NoteDyingRemoteBlob();
    }
  }

  ActorType* Actor() const { return mActor; }
};

template <ActorFlavorEnum ActorFlavor>
class RemoteBlob : public nsDOMFile,
                   public RemoteBlobBase<ActorFlavor>
{
  typedef Blob<ActorFlavor> ActorType;
public:
  class StreamHelper : public nsRunnable
  {
    typedef Blob<ActorFlavor> ActorType;
    typedef InputStreamActor<ActorFlavor> StreamActorType;

    mozilla::Monitor mMonitor;
    ActorType* mActor;
    nsCOMPtr<nsIDOMBlob> mSourceBlob;
    nsRefPtr<RemoteInputStream> mInputStream;
    bool mDone;

  public:
    StreamHelper(ActorType* aActor, nsIDOMBlob* aSourceBlob)
    : mMonitor("RemoteBlob::StreamHelper::mMonitor"), mActor(aActor),
      mSourceBlob(aSourceBlob), mDone(false)
    {
      // This may be created on any thread.
      MOZ_ASSERT(aActor);
      MOZ_ASSERT(aSourceBlob);
    }

    nsresult
    GetStream(nsIInputStream** aInputStream)
    {
      // This may be called on any thread.
      MOZ_ASSERT(aInputStream);
      MOZ_ASSERT(mActor);
      MOZ_ASSERT(!mInputStream);
      MOZ_ASSERT(!mDone);

      if (NS_IsMainThread()) {
        RunInternal(false);
      }
      else {
        nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
        NS_ENSURE_TRUE(mainThread, NS_ERROR_FAILURE);

        nsresult rv = mainThread->Dispatch(this, NS_DISPATCH_NORMAL);
        NS_ENSURE_SUCCESS(rv, rv);

        {
          MonitorAutoLock lock(mMonitor);
          while (!mDone) {
            lock.Wait();
          }
        }
      }

      MOZ_ASSERT(!mActor);
      MOZ_ASSERT(mDone);

      if (!mInputStream) {
        return NS_ERROR_UNEXPECTED;
      }

      mInputStream.forget(aInputStream);
      return NS_OK;
    }

    NS_IMETHOD
    Run()
    {
      MOZ_ASSERT(NS_IsMainThread());
      RunInternal(true);
      return NS_OK;
    }

  private:
    void
    RunInternal(bool aNotify)
    {
      MOZ_ASSERT(NS_IsMainThread());
      MOZ_ASSERT(mActor);
      MOZ_ASSERT(!mInputStream);
      MOZ_ASSERT(!mDone);

      nsRefPtr<RemoteInputStream> stream = new RemoteInputStream(mSourceBlob,
                                                                 ActorFlavor);

      StreamActorType* streamActor = new StreamActorType(stream);
      if (mActor->SendPBlobStreamConstructor(streamActor)) {
        stream.swap(mInputStream);
      }

      mActor = nullptr;

      if (aNotify) {
        MonitorAutoLock lock(mMonitor);
        mDone = true;
        lock.Notify();
      }
      else {
        mDone = true;
      }
    }
  };

  class SliceHelper : public nsRunnable
  {
    typedef Blob<ActorFlavor> ActorType;

    mozilla::Monitor mMonitor;
    ActorType* mActor;
    nsCOMPtr<nsIDOMBlob> mSlice;
    uint64_t mStart;
    uint64_t mLength;
    nsString mContentType;
    bool mDone;

  public:
    SliceHelper(ActorType* aActor)
    : mMonitor("RemoteBlob::SliceHelper::mMonitor"), mActor(aActor), mStart(0),
      mLength(0), mDone(false)
    {
      // This may be created on any thread.
      MOZ_ASSERT(aActor);
      MOZ_ASSERT(aActor->HasManager());
    }

    nsresult
    GetSlice(uint64_t aStart, uint64_t aLength, const nsAString& aContentType,
             nsIDOMBlob** aSlice)
    {
      // This may be called on any thread.
      MOZ_ASSERT(aSlice);
      MOZ_ASSERT(mActor);
      MOZ_ASSERT(!mSlice);
      MOZ_ASSERT(!mDone);

      mStart = aStart;
      mLength = aLength;
      mContentType = aContentType;

      if (NS_IsMainThread()) {
        RunInternal(false);
      }
      else {
        nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
        NS_ENSURE_TRUE(mainThread, NS_ERROR_FAILURE);

        nsresult rv = mainThread->Dispatch(this, NS_DISPATCH_NORMAL);
        NS_ENSURE_SUCCESS(rv, rv);

        {
          MonitorAutoLock lock(mMonitor);
          while (!mDone) {
            lock.Wait();
          }
        }
      }

      MOZ_ASSERT(!mActor);
      MOZ_ASSERT(mDone);

      if (!mSlice) {
        return NS_ERROR_UNEXPECTED;
      }

      mSlice.forget(aSlice);
      return NS_OK;
    }

    NS_IMETHOD
    Run()
    {
      MOZ_ASSERT(NS_IsMainThread());
      RunInternal(true);
      return NS_OK;
    }

  private:
    void
    RunInternal(bool aNotify)
    {
      MOZ_ASSERT(NS_IsMainThread());
      MOZ_ASSERT(mActor);
      MOZ_ASSERT(!mSlice);
      MOZ_ASSERT(!mDone);

      NormalBlobConstructorParams normalParams;
      normalParams.contentType() = mContentType;
      normalParams.length() = mLength;

      BlobConstructorNoMultipartParams params(normalParams);

      ActorType* newActor = ActorType::Create(params);
      MOZ_ASSERT(newActor);
      mActor->PropagateManager(newActor);

      SlicedBlobConstructorParams slicedParams;
      slicedParams.contentType() = mContentType;
      slicedParams.begin() = mStart;
      slicedParams.end() = mStart + mLength;
      SetBlobOnParams(mActor, slicedParams);

      BlobConstructorNoMultipartParams params2(slicedParams);
      if (mActor->ConstructPBlobOnManager(newActor, params2)) {
        mSlice = newActor->GetBlob();
      }

      mActor = nullptr;

      if (aNotify) {
        MonitorAutoLock lock(mMonitor);
        mDone = true;
        lock.Notify();
      }
      else {
        mDone = true;
      }
    }
  };

public:
  NS_DECL_ISUPPORTS_INHERITED

  RemoteBlob(const nsAString& aName, const nsAString& aContentType,
             uint64_t aLength, uint64_t aModDate)
  : nsDOMFile(aName, aContentType, aLength, aModDate)
  {
    mImmutable = true;
  }

  RemoteBlob(const nsAString& aName, const nsAString& aContentType,
             uint64_t aLength)
  : nsDOMFile(aName, aContentType, aLength)
  {
    mImmutable = true;
  }

  RemoteBlob(const nsAString& aContentType, uint64_t aLength)
  : nsDOMFile(aContentType, aLength)
  {
    mImmutable = true;
  }

  RemoteBlob()
  : nsDOMFile(EmptyString(), EmptyString(), UINT64_MAX, UINT64_MAX)
  {
    mImmutable = true;
  }

  virtual already_AddRefed<nsIDOMBlob>
  CreateSlice(uint64_t aStart, uint64_t aLength, const nsAString& aContentType)
              MOZ_OVERRIDE
  {
    ActorType* actor = RemoteBlobBase<ActorFlavor>::Actor();
    if (!actor) {
      return nullptr;
    }

    nsRefPtr<SliceHelper> helper = new SliceHelper(actor);

    nsCOMPtr<nsIDOMBlob> slice;
    nsresult rv =
      helper->GetSlice(aStart, aLength, aContentType, getter_AddRefs(slice));
    NS_ENSURE_SUCCESS(rv, nullptr);

    return slice.forget();
  }

  NS_IMETHOD
  GetInternalStream(nsIInputStream** aStream) MOZ_OVERRIDE
  {
    ActorType* actor = RemoteBlobBase<ActorFlavor>::Actor();
    if (!actor) {
      return NS_ERROR_UNEXPECTED;
    }

    nsRefPtr<StreamHelper> helper = new StreamHelper(actor, this);
    return helper->GetStream(aStream);
  }

  NS_IMETHOD
  GetLastModifiedDate(JSContext* cx, JS::Value* aLastModifiedDate)
  {
    if (IsDateUnknown()) {
      aLastModifiedDate->setNull();
    } else {
      JSObject* date = JS_NewDateObjectMsec(cx, mLastModificationDate);
      if (!date) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      aLastModifiedDate->setObject(*date);
    }
    return NS_OK;
  }
};

template <ActorFlavorEnum ActorFlavor>
class RemoteMemoryBlob : public nsDOMMemoryFile,
                         public RemoteBlobBase<ActorFlavor>
{
public:
  NS_DECL_ISUPPORTS_INHERITED

  RemoteMemoryBlob(void* aMemoryBuffer,
                   uint64_t aLength,
                   const nsAString& aName,
                   const nsAString& aContentType,
                   uint64_t aModDate)
  : nsDOMMemoryFile(aMemoryBuffer, aLength, aName, aContentType, aModDate)
  {
    mImmutable = true;
  }

  RemoteMemoryBlob(void* aMemoryBuffer,
                   uint64_t aLength,
                   const nsAString& aName,
                   const nsAString& aContentType)
  : nsDOMMemoryFile(aMemoryBuffer, aLength, aName, aContentType)
  {
    mImmutable = true;
  }

  RemoteMemoryBlob(void* aMemoryBuffer,
                   uint64_t aLength,
                   const nsAString& aContentType)
  : nsDOMMemoryFile(aMemoryBuffer, aLength, EmptyString(), aContentType)
  {
    mImmutable = true;
  }

  NS_IMETHOD
  GetLastModifiedDate(JSContext* cx, JS::Value* aLastModifiedDate)
  {
    if (IsDateUnknown()) {
      aLastModifiedDate->setNull();
    } else {
      JSObject* date = JS_NewDateObjectMsec(cx, mLastModificationDate);
      if (!date) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      aLastModifiedDate->setObject(*date);
    }
    return NS_OK;
  }


  NS_IMETHOD
  GetInternalStream(nsIInputStream** aStream) MOZ_OVERRIDE
  {
    nsCOMPtr<nsIInputStream> realStream;
    nsresult rv =
      nsDOMMemoryFile::GetInternalStream(getter_AddRefs(realStream));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIInputStream> stream =
      new BlobInputStreamTether(realStream, this);
    stream.forget(aStream);
    return NS_OK;
  }
};

template <ActorFlavorEnum ActorFlavor>
class RemoteMultipartBlob : public nsDOMMultipartFile,
                            public RemoteBlobBase<ActorFlavor>
{
public:
  NS_DECL_ISUPPORTS_INHERITED

  RemoteMultipartBlob(const nsAString& aName,
                      const nsAString& aContentType)
  : nsDOMMultipartFile(aName, aContentType)
  {
    mImmutable = true;
  }

  RemoteMultipartBlob(const nsAString& aName)
  : nsDOMMultipartFile(aName)
  {
    mImmutable = true;
  }

  NS_IMETHOD
  GetLastModifiedDate(JSContext* cx, JS::Value* aLastModifiedDate) MOZ_OVERRIDE
  {
    if (IsDateUnknown()) {
      aLastModifiedDate->setNull();
    } else {
      JSObject* date = JS_NewDateObjectMsec(cx, mLastModificationDate);
      if (!date) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      aLastModifiedDate->setObject(*date);
    }
    return NS_OK;
  }

  NS_IMETHOD
  GetInternalStream(nsIInputStream** aStream) MOZ_OVERRIDE
  {
    nsCOMPtr<nsIInputStream> realStream;
    nsresult rv =
      nsDOMMultipartFile::GetInternalStream(getter_AddRefs(realStream));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIInputStream> stream =
      new BlobInputStreamTether(realStream, this);
    stream.forget(aStream);
    return NS_OK;
  }
};

template <ActorFlavorEnum ActorFlavor>
Blob<ActorFlavor>::Blob(nsIDOMBlob* aBlob)
: mBlob(aBlob), mRemoteBlob(nullptr), mRemoteMemoryBlob(nullptr),
  mRemoteMultipartBlob(nullptr), mContentManager(nullptr),
  mBlobManager(nullptr), mOwnsBlob(true), mBlobIsFile(false)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBlob);
  aBlob->AddRef();

  nsCOMPtr<nsIDOMFile> file = do_QueryInterface(aBlob);
  mBlobIsFile = !!file;
}

template <ActorFlavorEnum ActorFlavor>
Blob<ActorFlavor>::Blob(nsRefPtr<RemoteBlobType>& aBlob,
                        bool aBlobIsFile)
: mBlob(nullptr), mRemoteBlob(nullptr), mRemoteMemoryBlob(nullptr),
  mRemoteMultipartBlob(nullptr), mContentManager(nullptr),
  mBlobManager(nullptr), mOwnsBlob(true), mBlobIsFile(aBlobIsFile)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mBlob);
  MOZ_ASSERT(!mRemoteBlob);
  MOZ_ASSERT(aBlob);

  if (NS_FAILED(aBlob->SetMutable(false))) {
    MOZ_NOT_REACHED("Failed to make remote blob immutable!");
  }

  aBlob->SetPBlob(this);
  aBlob.forget(&mRemoteBlob);

  mBlob = mRemoteBlob;
}

template <ActorFlavorEnum ActorFlavor>
Blob<ActorFlavor>::Blob(nsRefPtr<RemoteMemoryBlobType>& aBlob,
                        bool aBlobIsFile)
: mBlob(nullptr), mRemoteBlob(nullptr), mRemoteMemoryBlob(nullptr),
  mRemoteMultipartBlob(nullptr), mContentManager(nullptr),
  mBlobManager(nullptr), mOwnsBlob(true), mBlobIsFile(aBlobIsFile)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mBlob);
  MOZ_ASSERT(!mRemoteMemoryBlob);
  MOZ_ASSERT(aBlob);

  if (NS_FAILED(aBlob->SetMutable(false))) {
    MOZ_NOT_REACHED("Failed to make remote blob immutable!");
  }

  aBlob->SetPBlob(this);
  aBlob.forget(&mRemoteMemoryBlob);

  mBlob = mRemoteMemoryBlob;
}

template <ActorFlavorEnum ActorFlavor>
Blob<ActorFlavor>::Blob(nsRefPtr<RemoteMultipartBlobType>& aBlob,
                        bool aBlobIsFile)
: mBlob(nullptr), mRemoteBlob(nullptr), mRemoteMemoryBlob(nullptr),
  mRemoteMultipartBlob(nullptr), mContentManager(nullptr),
  mBlobManager(nullptr), mOwnsBlob(true), mBlobIsFile(aBlobIsFile)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mBlob);
  MOZ_ASSERT(!mRemoteMultipartBlob);
  MOZ_ASSERT(aBlob);

  if (NS_FAILED(aBlob->SetMutable(false))) {
    MOZ_NOT_REACHED("Failed to make remote blob immutable!");
  }

  aBlob->SetPBlob(this);
  aBlob.forget(&mRemoteMultipartBlob);

  mBlob = mRemoteMultipartBlob;
}

template <ActorFlavorEnum ActorFlavor>
Blob<ActorFlavor>*
Blob<ActorFlavor>::Create(const BlobConstructorParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  switch (aParams.type()) {
    case BlobConstructorParams::TBlobConstructorNoMultipartParams: {
      const BlobConstructorNoMultipartParams& params =
        aParams.get_BlobConstructorNoMultipartParams();

      switch (params.type()) {
        case BlobConstructorNoMultipartParams::TBlobOrFileConstructorParams: {
          const BlobOrFileConstructorParams& fileParams =
            params.get_BlobOrFileConstructorParams();
          nsRefPtr<RemoteBlobType> remoteBlob;
          bool isFile = false;

          switch (fileParams.type()) {
            case BlobOrFileConstructorParams::TNormalBlobConstructorParams: {
              const NormalBlobConstructorParams& normalParams =
                fileParams.get_NormalBlobConstructorParams();
              remoteBlob = new RemoteBlobType(normalParams.contentType(),
                                              normalParams.length());
              break;
            }

            case BlobOrFileConstructorParams::TFileBlobConstructorParams: {
              const FileBlobConstructorParams& fbParams =
                fileParams.get_FileBlobConstructorParams();
              remoteBlob =
                new RemoteBlobType(fbParams.name(), fbParams.contentType(),
                                   fbParams.length(), fbParams.modDate());
              isFile = true;
              break;
            }

            default:
              MOZ_NOT_REACHED("Unknown params!");
          }

          return new Blob<ActorFlavor>(remoteBlob, isFile);
        }
        case BlobConstructorNoMultipartParams::TMemoryBlobOrFileConstructorParams: {
          const MemoryBlobOrFileConstructorParams& memoryParams =
            params.get_MemoryBlobOrFileConstructorParams();
          const BlobOrFileConstructorParams& internalParams =
            memoryParams.constructorParams();
          nsRefPtr<RemoteMemoryBlobType> remoteMemoryBlob;
          bool isFile = false;

          switch (internalParams.type()) {
            case BlobOrFileConstructorParams::TNormalBlobConstructorParams: {
              const NormalBlobConstructorParams& normalParams =
                internalParams.get_NormalBlobConstructorParams();
              MOZ_ASSERT(normalParams.length() == memoryParams.data().Length());

              void* data =
                BlobTraits<ActorFlavor>::Allocate(memoryParams.data().Length());
              if (!data) {
                return nullptr;
              }
              memcpy(data,
                     memoryParams.data().Elements(),
                     memoryParams.data().Length());
              remoteMemoryBlob =
                new RemoteMemoryBlobType(data,
                                         memoryParams.data().Length(),
                                         normalParams.contentType());
              break;
            }

            case BlobOrFileConstructorParams::TFileBlobConstructorParams: {
              const FileBlobConstructorParams& fbParams =
                internalParams.get_FileBlobConstructorParams();
              MOZ_ASSERT(fbParams.length() == memoryParams.data().Length());

              void* data =
                BlobTraits<ActorFlavor>::Allocate(memoryParams.data().Length());
              if (!data) {
                return nullptr;
              }
              memcpy(data,
                     memoryParams.data().Elements(),
                     memoryParams.data().Length());
              remoteMemoryBlob =
                new RemoteMemoryBlobType(data,
                                         memoryParams.data().Length(),
                                         fbParams.name(),
                                         fbParams.contentType(),
                                         fbParams.modDate());
              isFile = true;
              break;
            }

            default:
              MOZ_NOT_REACHED("Unknown params!");
          }

          return new Blob<ActorFlavor>(remoteMemoryBlob, isFile);
        }
        case BlobConstructorNoMultipartParams::TMysteryBlobConstructorParams: {
          nsRefPtr<RemoteBlobType> remoteBlob = new RemoteBlobType();
          return new Blob<ActorFlavor>(remoteBlob, true);
        }

        case BlobConstructorNoMultipartParams::TSlicedBlobConstructorParams: {
          const SlicedBlobConstructorParams& slicedParams =
            params.get_SlicedBlobConstructorParams();

          nsCOMPtr<nsIDOMBlob> source =
            GetBlobFromParams<ActorFlavor>(slicedParams);
          MOZ_ASSERT(source);

          nsCOMPtr<nsIDOMBlob> slice;
          nsresult rv =
            source->Slice(slicedParams.begin(), slicedParams.end(),
                          slicedParams.contentType(), 3,
                          getter_AddRefs(slice));
          NS_ENSURE_SUCCESS(rv, nullptr);

          return new Blob<ActorFlavor>(slice);
        }

        case BlobConstructorNoMultipartParams::TPBlobParent:
        case BlobConstructorNoMultipartParams::TPBlobChild: {
          nsCOMPtr<nsIDOMBlob> localBlob =
            BlobFromExistingActor<ActorFlavor>(params);
          MOZ_ASSERT(localBlob);

          return new Blob<ActorFlavor>(localBlob);
        }

        default:
          MOZ_NOT_REACHED("Unknown params!");
      }
    }

    case BlobConstructorParams::TMultipartBlobOrFileConstructorParams: {
      const MultipartBlobOrFileConstructorParams& params =
        aParams.get_MultipartBlobOrFileConstructorParams();

      nsRefPtr<RemoteMultipartBlobType> file;
      bool isFile = false;
      const BlobOrFileConstructorParams& internalParams =
        params.constructorParams();
      switch (internalParams.type()) {
        case BlobOrFileConstructorParams::TNormalBlobConstructorParams: {
          const NormalBlobConstructorParams& normalParams =
            internalParams.get_NormalBlobConstructorParams();
          file = new RemoteMultipartBlobType(normalParams.contentType());
          break;
        }

        case BlobOrFileConstructorParams::TFileBlobConstructorParams: {
          const FileBlobConstructorParams& fbParams =
            internalParams.get_FileBlobConstructorParams();
          file = new RemoteMultipartBlobType(fbParams.name(),
                                             fbParams.contentType());
          isFile = true;
          break;
        }

        default:
          MOZ_NOT_REACHED("Unknown params!");
      }

      return new Blob<ActorFlavor>(file, isFile);
    }

    default:
      MOZ_NOT_REACHED("Unknown params!");
  }

  return nullptr;
}

template <ActorFlavorEnum ActorFlavor>
already_AddRefed<nsIDOMBlob>
Blob<ActorFlavor>::GetBlob()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);

  nsCOMPtr<nsIDOMBlob> blob;

  // Remote blobs are held alive until the first call to GetBlob. Thereafter we
  // only hold a weak reference. Normal blobs are held alive until the actor is
  // destroyed.
  if (mOwnsBlob &&
      (mRemoteBlob || mRemoteMemoryBlob || mRemoteMultipartBlob)) {
    blob = dont_AddRef(mBlob);
    mOwnsBlob = false;
  }
  else {
    blob = mBlob;
  }

  MOZ_ASSERT(blob);

  return blob.forget();
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::SetMysteryBlobInfo(const nsString& aName,
                                      const nsString& aContentType,
                                      uint64_t aLength,
                                      uint64_t aLastModifiedDate)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(mRemoteBlob);
  MOZ_ASSERT(aLength);
  MOZ_ASSERT(aLastModifiedDate != UINT64_MAX);

  ToConcreteBlob(mBlob)->SetLazyData(aName, aContentType,
                                     aLength, aLastModifiedDate);

  FileBlobConstructorParams params(aName, aContentType,
                                   aLength, aLastModifiedDate);
  return ProtocolType::SendResolveMystery(params);
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::SetMysteryBlobInfo(const nsString& aContentType,
                                      uint64_t aLength)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(mRemoteBlob);
  MOZ_ASSERT(aLength);

  nsString voidString;
  voidString.SetIsVoid(true);

  ToConcreteBlob(mBlob)->SetLazyData(voidString, aContentType,
                                     aLength, UINT64_MAX);

  NormalBlobConstructorParams params(aContentType, aLength);
  return ProtocolType::SendResolveMystery(params);
}

template <ActorFlavorEnum ActorFlavor>
typename Blob<ActorFlavor>::ProtocolType*
Blob<ActorFlavor>::ConstructPBlobOnManager(ProtocolType* aActor,
                                           const BlobConstructorParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(HasManager());

  if (mContentManager) {
    return mContentManager->SendPBlobConstructor(aActor, aParams);
  }

  if (mBlobManager) {
    return mBlobManager->SendPBlobConstructor(aActor, aParams);
  }

  MOZ_NOT_REACHED("Why don't I have a manager?!");
  return nullptr;
}

template <ActorFlavorEnum ActorFlavor>
void
Blob<ActorFlavor>::SetManager(ContentManagerType* aManager)
{
  MOZ_ASSERT(!mContentManager && !mBlobManager);
  MOZ_ASSERT(aManager);

  mContentManager = aManager;
}

template <ActorFlavorEnum ActorFlavor>
void
Blob<ActorFlavor>::SetManager(BlobManagerType* aManager)
{
  MOZ_ASSERT(!mContentManager && !mBlobManager);
  MOZ_ASSERT(aManager);

  mBlobManager = aManager;
}

template <ActorFlavorEnum ActorFlavor>
void
Blob<ActorFlavor>::PropagateManager(Blob<ActorFlavor>* aActor) const
{
  MOZ_ASSERT(HasManager());

  aActor->mContentManager = mContentManager;
  aActor->mBlobManager = mBlobManager;
}

template <ActorFlavorEnum ActorFlavor>
void
Blob<ActorFlavor>::NoteDyingRemoteBlob()
{
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(mRemoteBlob || mRemoteMemoryBlob || mRemoteMultipartBlob);
  MOZ_ASSERT(!mOwnsBlob);

  // This may be called on any thread due to the fact that RemoteBlob is
  // designed to be passed between threads. We must start the shutdown process
  // on the main thread, so we proxy here if necessary.
  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> runnable =
      NS_NewNonOwningRunnableMethod(this,
                                    &Blob<ActorFlavor>::NoteDyingRemoteBlob);
    if (NS_FAILED(NS_DispatchToMainThread(runnable))) {
      MOZ_ASSERT(false, "Should never fail!");
    }

    return;
  }

  // Must do this before calling Send__delete__ or we'll crash there trying to
  // access a dangling pointer.
  mBlob = nullptr;
  mRemoteBlob = nullptr;
  mRemoteMemoryBlob = nullptr;
  mRemoteMultipartBlob = nullptr;

  mozilla::unused << ProtocolType::Send__delete__(this);
}

template <ActorFlavorEnum ActorFlavor>
void
Blob<ActorFlavor>::ActorDestroy(ActorDestroyReason aWhy)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mRemoteBlob) {
    mRemoteBlob->SetPBlob(nullptr);
  }

  if (mRemoteMemoryBlob) {
    mRemoteMemoryBlob->SetPBlob(nullptr);
  }

  if (mRemoteMultipartBlob) {
    mRemoteMultipartBlob->SetPBlob(nullptr);
  }

  if (mBlob && mOwnsBlob) {
    mBlob->Release();
  }
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::RecvResolveMystery(const ResolveMysteryParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(!mRemoteBlob);
  MOZ_ASSERT(!mRemoteMemoryBlob);
  MOZ_ASSERT(!mRemoteMultipartBlob);
  MOZ_ASSERT(mOwnsBlob);

  if (!mBlobIsFile) {
    MOZ_ASSERT(false, "Must always be a file!");
    return false;
  }

  nsDOMFileBase* blob = ToConcreteBlob(mBlob);

  switch (aParams.type()) {
    case ResolveMysteryParams::TNormalBlobConstructorParams: {
      const NormalBlobConstructorParams& params =
        aParams.get_NormalBlobConstructorParams();
      nsString voidString;
      voidString.SetIsVoid(true);
      blob->SetLazyData(voidString, params.contentType(),
                        params.length(), UINT64_MAX);
      break;
    }

    case ResolveMysteryParams::TFileBlobConstructorParams: {
      const FileBlobConstructorParams& params =
        aParams.get_FileBlobConstructorParams();
      blob->SetLazyData(params.name(), params.contentType(),
                        params.length(), params.modDate());
      break;
    }

    default:
      MOZ_NOT_REACHED("Unknown params!");
  }

  return true;
}

template <>
bool
Blob<Parent>::RecvPBlobStreamConstructor(StreamType* aActor)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(!mRemoteBlob);
  MOZ_ASSERT(!mRemoteMemoryBlob);
  MOZ_ASSERT(!mRemoteMultipartBlob);

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = mBlob->GetInternalStream(getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIRemoteBlob> remoteBlob = do_QueryInterface(mBlob);

  nsCOMPtr<IPrivateRemoteInputStream> remoteStream;
  if (remoteBlob) {
    remoteStream = do_QueryInterface(stream);
  }

  // There are three cases in which we can use the stream obtained from the blob
  // directly as our serialized stream:
  //
  //   1. The blob is not a remote blob.
  //   2. The blob is a remote blob that represents this actor.
  //   3. The blob is a remote blob representing a different actor but we
  //      already have a non-remote, i.e. serialized, serialized stream.
  //
  // In all other cases we need to be on a background thread before we can get
  // to the real stream.
  nsCOMPtr<nsIIPCSerializableInputStream> serializableStream;
  if (!remoteBlob ||
      static_cast<ProtocolType*>(remoteBlob->GetPBlob()) == this ||
      !remoteStream) {
    serializableStream = do_QueryInterface(stream);
    if (!serializableStream) {
      MOZ_ASSERT(false, "Must be serializable!");
      return false;
    }
  }

  nsCOMPtr<nsIEventTarget> target =
    do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  NS_ENSURE_TRUE(target, false);

  nsRefPtr<BaseType::OpenStreamRunnable> runnable =
    new BaseType::OpenStreamRunnable(this, aActor, stream, serializableStream,
                                     target);

  rv = runnable->Dispatch();
  NS_ENSURE_SUCCESS(rv, false);

  nsRevocableEventPtr<BaseType::OpenStreamRunnable>* arrayMember =
    mOpenStreamRunnables.AppendElement();
  *arrayMember = runnable;
  return true;
}

template <>
bool
Blob<Child>::RecvPBlobStreamConstructor(StreamType* aActor)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(!mRemoteBlob);
  MOZ_ASSERT(!mRemoteMemoryBlob);
  MOZ_ASSERT(!mRemoteMultipartBlob);

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = mBlob->GetInternalStream(getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, false);

  nsCOMPtr<nsIIPCSerializableInputStream> serializable =
    do_QueryInterface(stream);
  if (!serializable) {
    MOZ_ASSERT(false, "Must be serializable!");
    return false;
  }

  InputStreamParams params;
  serializable->Serialize(params);

  MOZ_ASSERT(params.type() != InputStreamParams::T__None);

  return aActor->Send__delete__(aActor, params);
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::RecvPBlobConstructor(ProtocolType* aActor,
                                        const BlobConstructorParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  Blob<ActorFlavor>* subBlobActor = static_cast<Blob<ActorFlavor>*>(aActor);

  if (!subBlobActor->ManagerIs(this)) {
    // Somebody screwed up!
    return false;
  }

  nsCOMPtr<nsIDOMBlob> blob = subBlobActor->GetBlob();
  static_cast<nsDOMMultipartFile*>(mBlob)->AddBlob(blob);
  return true;
}

template <ActorFlavorEnum ActorFlavor>
typename Blob<ActorFlavor>::StreamType*
Blob<ActorFlavor>::AllocPBlobStream()
{
  MOZ_ASSERT(NS_IsMainThread());
  return new InputStreamActor<ActorFlavor>();
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::DeallocPBlobStream(StreamType* aActor)
{
  MOZ_ASSERT(NS_IsMainThread());
  delete aActor;
  return true;
}

template <ActorFlavorEnum ActorFlavor>
typename Blob<ActorFlavor>::ProtocolType*
Blob<ActorFlavor>::AllocPBlob(const BlobConstructorParams& aParams)
{
  Blob<ActorFlavor>* actor = Blob<ActorFlavor>::Create(aParams);
  actor->SetManager(this);
  return actor;
}

template <ActorFlavorEnum ActorFlavor>
bool
Blob<ActorFlavor>::DeallocPBlob(ProtocolType* aActor)
{
  delete aActor;
  return true;
}

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_ADDREF_INHERITED(RemoteBlob<ActorFlavor>, nsDOMFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_RELEASE_INHERITED(RemoteBlob<ActorFlavor>, nsDOMFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_QUERY_INTERFACE_INHERITED1(RemoteBlob<ActorFlavor>, nsDOMFile,
                                                            nsIRemoteBlob)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_ADDREF_INHERITED(RemoteMemoryBlob<ActorFlavor>, nsDOMMemoryFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_RELEASE_INHERITED(RemoteMemoryBlob<ActorFlavor>, nsDOMMemoryFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_QUERY_INTERFACE_INHERITED1(RemoteMemoryBlob<ActorFlavor>, nsDOMMemoryFile,
                                                                  nsIRemoteBlob)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_ADDREF_INHERITED(RemoteMultipartBlob<ActorFlavor>, nsDOMMultipartFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_RELEASE_INHERITED(RemoteMultipartBlob<ActorFlavor>, nsDOMMultipartFile)

template <ActorFlavorEnum ActorFlavor>
NS_IMPL_QUERY_INTERFACE_INHERITED1(RemoteMultipartBlob<ActorFlavor>, nsDOMMultipartFile,
                                                                     nsIRemoteBlob)

// Explicit instantiation of both classes.
template class Blob<Parent>;
template class Blob<Child>;

} // namespace ipc
} // namespace dom
} // namespace mozilla
