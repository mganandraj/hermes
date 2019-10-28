/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "hermes/Platform/Unicode/PlatformUnicode.h"

#if HERMES_PLATFORM_UNICODE == HERMES_PLATFORM_UNICODE_ICU

#include "hermes/Platform/Unicode/icu.h"

#include <time.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hermes {
namespace platform_unicode {

int localeCompare(
    llvm::ArrayRef<char16_t> left,
    llvm::ArrayRef<char16_t> right) {
  std::wstring str1((const UChar *)left.data(), left.size());
  std::wstring str2((const UChar *)right.data(), right.size());
  return str1.compare(str2);

  // UErrorCode err{U_ZERO_ERROR};
  // UCollator *coll = ucol_open(uloc_getDefault(), &err);
  // if (U_FAILURE(err)) {
  //  // Failover to root locale if we're unable to open in default locale.
  //  err = U_ZERO_ERROR;
  //  coll = ucol_open("", &err);
  //}
  // assert(U_SUCCESS(err) && "failed to open collator");

  //// Normalization mode allows for strings that can be represented
  //// in two different ways to compare as equal.
  // ucol_setAttribute(coll, UCOL_NORMALIZATION_MODE, UCOL_ON, &err);
  // assert(U_SUCCESS(err) && "failed to set collator attribute");

  // auto result = ucol_strcoll(
  //    coll,
  //    (const UChar *)left.data(),
  //    left.size(),
  //    (const UChar *)right.data(),
  //    right.size());

  // ucol_close(coll);

  // switch (result) {
  //  case UCOL_LESS:
  //    return -1;
  //  case UCOL_EQUAL:
  //    return 0;
  //  case UCOL_GREATER:
  //    return 1;
  //}
  // llvm_unreachable("Invalid result from ucol_strcoll");
}

void dateFormat(
    double unixtimeMs,
    bool formatDate,
    bool formatTime,
    llvm::SmallVectorImpl<char16_t> &buf) {
  // Sample output : "Jan 2, 1970, 5:47:04 PM"

  std::time_t time_t = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::time_point(
          std::chrono::milliseconds(static_cast<uint64_t>(unixtimeMs))));
  std::tm tm = *std::localtime(&time_t);

  std::stringstream ss;
  std::string format;
  if (formatDate & formatTime) {
    format = "%b %e, %Y, %T";
  } else if (formatDate) {
    format = "%b %e, %Y";
  } else if (formatTime) {
    format = "%T";
  }

  ss << std::put_time(&tm, format.c_str());

  std::string sstr = ss.str();
  std::wstring wsstr = std::wstring(sstr.begin(), sstr.end());

  int idx = 0;
  buf.resize(wsstr.length() * sizeof(wchar_t));
  for (wchar_t wc : wsstr) {
    buf[idx++] = wc;
  }
}

// ::tzset();
//  UDateFormatStyle dateStyle = formatDate ? UDAT_MEDIUM : UDAT_NONE;
//  UDateFormatStyle timeStyle = formatTime ? UDAT_MEDIUM : UDAT_NONE;
//
//  UErrorCode err{U_ZERO_ERROR};
//  // Get the tzname manually to allow stable testing.
//  llvm::SmallVector<char16_t, 32> tzstr;
//#ifdef _WINDOWS
//  const char *tz = _tzname[0];
//#else
//  const char *tz = ::tzname[0];
//#endif
//  tzstr.append(tz, tz + strlen(tz));
//
//  auto *df = udat_open(
//      timeStyle,
//      dateStyle,
//      uloc_getDefault(),
//      (const UChar *)tzstr.data(),
//      tzstr.size(),
//      nullptr,
//      0,
//      &err);
//  if (!df) {
//    return;
//  }
//
//  const int INITIAL_SIZE = 128;
//  buf.resize(INITIAL_SIZE);
//  err = U_ZERO_ERROR;
//  int length =
//      udat_format(df, unixtimeMs, (UChar *)buf.begin(), INITIAL_SIZE, 0,
//      &err);
//  if (length > INITIAL_SIZE) {
//    buf.resize(length + 1);
//    err = U_ZERO_ERROR;
//    udat_format(df, unixtimeMs, (UChar *)buf.begin(), length, 0, &err);
//    buf.resize(length);
//  } else {
//    buf.resize(length);
//  }
//
//  udat_close(df);
// } // namespace platform_unicode

void convertToCase(
    llvm::SmallVectorImpl<char16_t> &buf,
    CaseConversion targetCase,
    bool useCurrentLocale) {
  if (targetCase == CaseConversion::ToUpper)
    for (auto c : buf) {
      c = std::toupper(c);
    }
  /*std::for_each(
      buf.begin(), buf.end(), [](wchar_t &c) { c = std::toupper(c); });*/
  else

    for (auto c : buf) {
      c = std::tolower(c);
    }
  /*std::for_each(
      buf.begin(), buf.end(), [](wchar_t &c) { c = std::tolower(c); });*/

  // To be able to call the converters, we have to get a pointer.
  // UChar is 16 bits, so a cast works.
  // const UChar *src = (const UChar *)buf.data();
  // auto srcLen = buf.size();

  // auto converter =
  //    targetCase == CaseConversion::ToUpper ? u_strToUpper : u_strToLower;
  // const char *locale = useCurrentLocale ? uloc_getDefault() : "";

  //// First, try to uppercase without changing the length.
  //// This will likely work.
  // llvm::SmallVector<char16_t, 64> dest{};
  // dest.resize(srcLen);
  // UErrorCode err = U_ZERO_ERROR;
  // size_t resultLen =
  //    converter((UChar *)dest.begin(), srcLen, src, srcLen, locale, &err);
  // dest.resize(resultLen);

  //// In the rare case our string was too small, rerun it.
  // if (LLVM_UNLIKELY(resultLen > srcLen)) {
  //  err = U_ZERO_ERROR;
  //  converter((UChar *)dest.begin(), resultLen, src, srcLen, locale, &err);
  //}
  //// Assign to the inout parameter.
  // buf = dest;
}

void normalize(llvm::SmallVectorImpl<char16_t> &buf, NormalizationForm form) {
  // NOPE

  // To be able to call the converters, we have to get a pointer.
  // UChar is 16 bits, so a cast works.
  // const UChar *src = (const UChar *)buf.data();
  // auto srcLen = buf.size();
  // UErrorCode err = U_ZERO_ERROR;

  // const UNormalizer2 *norm = nullptr;
  // switch (form) {
  //  case NormalizationForm::C:
  //    norm = unorm2_getNFCInstance(&err);
  //    break;
  //  case NormalizationForm::D:
  //    norm = unorm2_getNFDInstance(&err);
  //    break;
  //  case NormalizationForm::KC:
  //    norm = unorm2_getNFKCInstance(&err);
  //    break;
  //  case NormalizationForm::KD:
  //    norm = unorm2_getNFKDInstance(&err);
  //    break;
  //}
  // assert(U_SUCCESS(err) && norm && "Failed to get Normalizer instance");

  //// First, try to normalize without changing the length.
  //// This will likely work; note that this is an optimization for the
  //// non-enlarging case.
  // llvm::SmallVector<char16_t, 64> dest{};
  // dest.resize(srcLen);
  // err = U_ZERO_ERROR;
  // size_t resultLen =
  //    unorm2_normalize(norm, src, srcLen, (UChar *)dest.begin(), srcLen,
  //    &err);
  // dest.resize(resultLen);

  //// In the rare case our string was too small, rerun it.
  // if (LLVM_UNLIKELY(resultLen > srcLen)) {
  //  err = U_ZERO_ERROR;
  //  unorm2_normalize(norm, src, srcLen, (UChar *)dest.begin(), resultLen,
  //  &err);
  //}

  //// Assign to the inout parameter.
  // buf = dest;
}

} // namespace platform_unicode
} // namespace hermes

#endif // HERMES_PLATFORM_UNICODE_ICU
