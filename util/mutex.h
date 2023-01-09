// Copyright 2007 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef UTIL_MUTEX_H_
#define UTIL_MUTEX_H_

/*
 * A simple mutex wrapper, supporting locks and read-write locks.
 * You should assume the locks are *not* re-entrant.
 */

#ifdef _WIN32
// Requires Windows Vista or Windows Server 2008 at minimum.
#include <windows.h>
#if defined(WINVER) && WINVER >= 0x0600
#define MUTEX_IS_WIN32_SRWLOCK
#endif
#if defined(WINVER) && WINVER < 0x0600
#define MUTEX_IS_WIN32_CSLOCK
#endif
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <unistd.h>
#if defined(_POSIX_READER_WRITER_LOCKS) && _POSIX_READER_WRITER_LOCKS > 0
#define MUTEX_IS_PTHREAD_RWLOCK
#endif
#endif

#if defined(MUTEX_IS_WIN32_SRWLOCK)
typedef SRWLOCK MutexType;
#elif defined(MUTEX_IS_WIN32_CSLOCK)
struct MutexType
{
    CRITICAL_SECTION countsLock;
    CRITICAL_SECTION writerLock;
    HANDLE noReaders;
    int readerCount;
    BOOL waitingWriter;
};
#elif defined(MUTEX_IS_PTHREAD_RWLOCK)
#include <pthread.h>
#include <stdlib.h>
typedef pthread_rwlock_t MutexType;
#else
#include <shared_mutex>
typedef std::shared_mutex MutexType;
#endif

// redefine call_once and once_flag for pre-Vista lack of SRWLock
#if defined(MUTEX_IS_WIN32_CSLOCK)
struct OnceFlag
{
    CRITICAL_SECTION cs;
    bool called;
    OnceFlag() { called = false; InitializeCriticalSection(&cs); }
    ~OnceFlag() { DeleteCriticalSection(&cs); }
};

template <class _Fn, class... _Args>
void(CallOnce)(OnceFlag& _Once, _Fn&& _Fx, _Args&&... _Ax) noexcept(
    noexcept(_STD invoke(_STD forward<_Fn>(_Fx), _STD forward<_Args>(_Ax)...))) /* strengthened */ {
    EnterCriticalSection(&_Once.cs);
    if (!_Once.called) 
    {
        _STD invoke(_STD forward<_Fn>(_Fx), _STD forward<_Args>(_Ax)...);
    }
    LeaveCriticalSection(&_Once.cs);
}
#else
#define CallOnce std::call_once
#define OnceFlag std::once_flag
#endif

namespace re2 {

class Mutex {
 public:
  inline Mutex();
  inline ~Mutex();
  inline void Lock();    // Block if needed until free then acquire exclusively
  inline void Unlock();  // Release a lock acquired via Lock()
  // Note that on systems that don't support read-write locks, these may
  // be implemented as synonyms to Lock() and Unlock().  So you can use
  // these for efficiency, but don't use them anyplace where being able
  // to do shared reads is necessary to avoid deadlock.
  inline void ReaderLock();   // Block until free or shared then acquire a share
  inline void ReaderUnlock(); // Release a read share of this Mutex
  inline void WriterLock() { Lock(); }     // Acquire an exclusive lock
  inline void WriterUnlock() { Unlock(); } // Release a lock from WriterLock()

 private:
  MutexType mutex_;

  // Catch the error of writing Mutex when intending MutexLock.
  Mutex(Mutex *ignored);

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

#if defined(MUTEX_IS_WIN32_SRWLOCK)

Mutex::Mutex()             : mutex_(SRWLOCK_INIT) { }
Mutex::~Mutex()            { }
void Mutex::Lock()         { AcquireSRWLockExclusive(&mutex_); }
void Mutex::Unlock()       { ReleaseSRWLockExclusive(&mutex_); }
void Mutex::ReaderLock()   { AcquireSRWLockShared(&mutex_); }
void Mutex::ReaderUnlock() { ReleaseSRWLockShared(&mutex_); }

#elif defined(MUTEX_IS_WIN32_CSLOCK)

Mutex::Mutex()
{
    mutex_ = {};
    InitializeCriticalSection(&mutex_.writerLock);
    InitializeCriticalSection(&mutex_.countsLock);

    /*
     * Could use a semaphore as well.  There can only be one waiter ever,
     * so I'm showing an auto-reset event here.
     */
    mutex_.noReaders = CreateEvent (NULL, FALSE, FALSE, NULL);
}
Mutex::~Mutex()
{
    DeleteCriticalSection(&mutex_.writerLock);
    DeleteCriticalSection(&mutex_.countsLock);
    CloseHandle(mutex_.noReaders);
}

void Mutex::ReaderLock()
{
    /*
     * We need to lock the writerLock too, otherwise a writer could
     * do the whole of mutex__wrlock after the readerCount changed
     * from 0 to 1, but before the event was reset.
     */
    EnterCriticalSection(&mutex_.writerLock);
    EnterCriticalSection(&mutex_.countsLock);
    ++mutex_.readerCount;
    LeaveCriticalSection(&mutex_.countsLock);
    LeaveCriticalSection(&mutex_.writerLock);
}

void Mutex::Lock()
{
    EnterCriticalSection(&mutex_.writerLock);
    /*
     * readerCount cannot become non-zero within the writerLock CS,
     * but it can become zero...
     */
    if (mutex_.readerCount > 0) {
        EnterCriticalSection(&mutex_.countsLock);

        /* ... so test it again.  */
        if (mutex_.readerCount > 0) {
            mutex_.waitingWriter = TRUE;
            LeaveCriticalSection(&mutex_.countsLock);
            WaitForSingleObject(mutex_.noReaders, INFINITE);
        } else {
            /* How lucky, no need to wait.  */
            LeaveCriticalSection(&mutex_.countsLock);
        }
    }

    /* writerLock remains locked.  */
}

void Mutex::ReaderUnlock()
{
    EnterCriticalSection(&mutex_.countsLock);
    assert (mutex_.readerCount > 0);
    if (--mutex_.readerCount == 0) {
        if (mutex_.waitingWriter) {
            /*
             * Clear waitingWriter here to avoid taking countsLock
             * again in wrlock.
             */
            mutex_.waitingWriter = FALSE;
            SetEvent(mutex_.noReaders);
        }
    }
    LeaveCriticalSection(&mutex_.countsLock);
}

void Mutex::Unlock()
{
    LeaveCriticalSection(&mutex_.writerLock);
}

#elif defined(MUTEX_IS_PTHREAD_RWLOCK)

#define SAFE_PTHREAD(fncall)    \
  do {                          \
    if ((fncall) != 0) abort(); \
  } while (0)

Mutex::Mutex()             { SAFE_PTHREAD(pthread_rwlock_init(&mutex_, NULL)); }
Mutex::~Mutex()            { SAFE_PTHREAD(pthread_rwlock_destroy(&mutex_)); }
void Mutex::Lock()         { SAFE_PTHREAD(pthread_rwlock_wrlock(&mutex_)); }
void Mutex::Unlock()       { SAFE_PTHREAD(pthread_rwlock_unlock(&mutex_)); }
void Mutex::ReaderLock()   { SAFE_PTHREAD(pthread_rwlock_rdlock(&mutex_)); }
void Mutex::ReaderUnlock() { SAFE_PTHREAD(pthread_rwlock_unlock(&mutex_)); }

#undef SAFE_PTHREAD

#else

Mutex::Mutex()             { }
Mutex::~Mutex()            { }
void Mutex::Lock()         { mutex_.lock(); }
void Mutex::Unlock()       { mutex_.unlock(); }
void Mutex::ReaderLock()   { mutex_.lock_shared(); }
void Mutex::ReaderUnlock() { mutex_.unlock_shared(); }

#endif

// --------------------------------------------------------------------------
// Some helper classes

// MutexLock(mu) acquires mu when constructed and releases it when destroyed.
class MutexLock {
 public:
  explicit MutexLock(Mutex *mu) : mu_(mu) { mu_->Lock(); }
  ~MutexLock() { mu_->Unlock(); }
 private:
  Mutex * const mu_;

  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;
};

// ReaderMutexLock and WriterMutexLock do the same, for rwlocks
class ReaderMutexLock {
 public:
  explicit ReaderMutexLock(Mutex *mu) : mu_(mu) { mu_->ReaderLock(); }
  ~ReaderMutexLock() { mu_->ReaderUnlock(); }
 private:
  Mutex * const mu_;

  ReaderMutexLock(const ReaderMutexLock&) = delete;
  ReaderMutexLock& operator=(const ReaderMutexLock&) = delete;
};

class WriterMutexLock {
 public:
  explicit WriterMutexLock(Mutex *mu) : mu_(mu) { mu_->WriterLock(); }
  ~WriterMutexLock() { mu_->WriterUnlock(); }
 private:
  Mutex * const mu_;

  WriterMutexLock(const WriterMutexLock&) = delete;
  WriterMutexLock& operator=(const WriterMutexLock&) = delete;
};

// Catch bug where variable name is omitted, e.g. MutexLock (&mu);
#define MutexLock(x) static_assert(false, "MutexLock declaration missing variable name")
#define ReaderMutexLock(x) static_assert(false, "ReaderMutexLock declaration missing variable name")
#define WriterMutexLock(x) static_assert(false, "WriterMutexLock declaration missing variable name")

}  // namespace re2

#endif  // UTIL_MUTEX_H_
