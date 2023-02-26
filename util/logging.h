// Copyright 2009 The RE2 Authors.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef UTIL_LOGGING_H_
#define UTIL_LOGGING_H_

// Simplified version of Google's logging.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ostream>
#include <sstream>

#include "util/util.h"

// Debug-only checking.
#define DCHECK(condition) assert(condition)
#define DCHECK_EQ(val1, val2) assert((val1) == (val2))
#define DCHECK_NE(val1, val2) assert((val1) != (val2))
#define DCHECK_LE(val1, val2) assert((val1) <= (val2))
#define DCHECK_LT(val1, val2) assert((val1) < (val2))
#define DCHECK_GE(val1, val2) assert((val1) >= (val2))
#define DCHECK_GT(val1, val2) assert((val1) > (val2))

// Always-on checking
#define CHECK(x)	if(x){}else LogMessageFatal(__FILE__, __LINE__).stream() << "Check failed: " #x
#define CHECK_LT(x, y)	CHECK((x) < (y))
#define CHECK_GT(x, y)	CHECK((x) > (y))
#define CHECK_LE(x, y)	CHECK((x) <= (y))
#define CHECK_GE(x, y)	CHECK((x) >= (y))
#define CHECK_EQ(x, y)	CHECK((x) == (y))
#define CHECK_NE(x, y)	CHECK((x) != (y))

#define LOG_INFO    LogMessage(0, __FILE__, __LINE__)
#define LOG_WARNING LogMessage(1, __FILE__, __LINE__)
#define LOG_ERROR   LogMessage(2, __FILE__, __LINE__)
#define LOG_FATAL   LogMessageFatal(__FILE__, __LINE__)
#define LOG_QFATAL  LOG_FATAL

// It seems that one of the Windows header files defines ERROR as 0.
#ifdef _WIN32
#define LOG_0 LOG_INFO
#endif

#ifdef NDEBUG
#define LOG_DFATAL LOG_ERROR
#else
#define LOG_DFATAL LOG_FATAL
#endif

#define LOG(severity) LOG_ ## severity.stream()

#define VLOG(x) if((x)>0){}else LOG_INFO.stream()

// shim RE2 errors, ex:
// void RE2_WriteErrorString(const char *file, int line, int level, const char *msg)
// #define RE2_WRITE_ERROR RE2_WriteErrorString

class LogMessage {
 public:
  LogMessage(int level, const char* file, int line) {
    flushed_ = false;
    file_ = file;
    line_ = line;
    level_ = level;

#ifndef RE2_WRITE_ERROR
    stream() << file << ":" << line << ": ";
#endif
  }
  void Flush() {
    stream() << "\n";
    std::string s = str_.str();

#ifdef RE2_WRITE_ERROR
    RE2_WRITE_ERROR(file_, line_, level_, s.c_str());
#else
    if (fprintf(stderr, "%s", s.c_str()) == 0) {} // shut up gcc
#endif

    flushed_ = true;
  }
  ~LogMessage() {
    if (!flushed_) {
      Flush();
    }
  }
  std::ostream& stream() { return str_; }

 private:
  bool flushed_;
  std::ostringstream str_;
  const char *file_;
  int line_;
  int level_;

  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
};

// Silence "destructor never returns" warning for ~LogMessageFatal().
// Since this is a header file, push and then pop to limit the scope.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4722)
#endif

class LogMessageFatal : public LogMessage {
 public:
  LogMessageFatal(const char* file, int line)
      : LogMessage(3, file, line) {}
  ATTRIBUTE_NORETURN ~LogMessageFatal() {
    Flush();
    abort();
  }
 private:
  LogMessageFatal(const LogMessageFatal&) = delete;
  LogMessageFatal& operator=(const LogMessageFatal&) = delete;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif  // UTIL_LOGGING_H_
