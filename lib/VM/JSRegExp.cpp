/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "hermes/VM/JSRegExp.h"

#include "hermes/Regex/Compiler.h"
#include "hermes/Regex/Executor.h"
#include "hermes/Regex/RegexTraits.h"
#include "hermes/Support/UTF8.h"
#include "hermes/VM/BuildMetadata.h"
#include "hermes/VM/Operations.h"
#include "hermes/VM/RegExpMatch.h"
#include "hermes/VM/StringView.h"

#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "serialize"

namespace hermes {
namespace vm {

//===----------------------------------------------------------------------===//
// class JSRegExp

ObjectVTable JSRegExp::vt{
    VTable(CellKind::RegExpKind, sizeof(JSRegExp), JSRegExp::_finalizeImpl),
    JSRegExp::_getOwnIndexedRangeImpl,
    JSRegExp::_haveOwnIndexedImpl,
    JSRegExp::_getOwnIndexedPropertyFlagsImpl,
    JSRegExp::_getOwnIndexedImpl,
    JSRegExp::_setOwnIndexedImpl,
    JSRegExp::_deleteOwnIndexedImpl,
    JSRegExp::_checkAllOwnIndexedImpl,
};

void RegExpBuildMeta(const GCCell *cell, Metadata::Builder &mb) {
  ObjectBuildMeta(cell, mb);
}

#ifdef HERMESVM_SERIALIZE
void RegExpSerialize(Serializer &s, const GCCell *cell) {
  LLVM_DEBUG(llvm::dbgs() << "Serialize function not implemented for RegExp\n");
}

void RegExpDeserialize(Deserializer &d, CellKind kind) {
  LLVM_DEBUG(
      llvm::dbgs() << "Deserialize function not implemented for RegExp\n");
}
#endif

CallResult<HermesValue> JSRegExp::create(
    Runtime *runtime,
    Handle<JSObject> parentHandle) {
  void *mem =
      runtime->alloc</*fixedSize*/ true, HasFinalizer::Yes>(sizeof(JSRegExp));
  auto selfHandle = runtime->makeHandle(
      JSObject::allocateSmallPropStorage<NEEDED_PROPERTY_SLOTS>(
          new (mem) JSRegExp(
              runtime,
              *parentHandle,
              runtime->getHiddenClassForPrototypeRaw(*parentHandle))));

  Handle<> emptyString = runtime->makeHandle(HermesValue::encodeStringValue(
      runtime->getPredefinedString(Predefined::emptyString)));

  JSObject::addInternalProperties(selfHandle, runtime, 1, emptyString);
  static_assert(pattern == 0, "internal property 'pattern' must be first");

  return selfHandle.getHermesValue();
}

ExecutionStatus JSRegExp::initialize(
    Handle<JSRegExp> selfHandle,
    Runtime *runtime,
    Handle<StringPrimitive> pattern,
    Handle<StringPrimitive> flags,
    OptValue<llvm::ArrayRef<uint8_t>> bytecode) {
  assert(
      pattern && flags &&
      "Null pattern and/or flags passed to initializeWithPatternAndFlags");

  // Validate flags
  auto flagsView = StringPrimitive::createStringView(runtime, flags);
  auto fbits = FlagBits::fromString(flagsView);
  if (!fbits) {
    runtime->raiseSyntaxError("Invalid flags passed to RegExp");
    return ExecutionStatus::EXCEPTION;
  }
  selfHandle->flagBits_ = *fbits;

  JSObject::setInternalProperty(
      selfHandle.get(),
      runtime,
      RegExpSlotIndexes::pattern,
      pattern.getHermesValue());

  DefinePropertyFlags dpf{};
  dpf.setEnumerable = 1;
  dpf.enumerable = 0;
  dpf.setWritable = 1;
  dpf.writable = 1;
  dpf.setConfigurable = 1;
  dpf.configurable = 0;
  dpf.setValue = 1;

  auto res = JSObject::defineOwnProperty(
      selfHandle,
      runtime,
      Predefined::getSymbolID(Predefined::lastIndex),
      dpf,
      runtime->makeHandle(HermesValue::encodeNumberValue(0)));
  (void)res;
  assert(
      res != ExecutionStatus::EXCEPTION && *res &&
      "defineOwnProperty() failed");

  if (bytecode) {
    selfHandle->bytecode_ = *bytecode;
  } else {
    regex::constants::SyntaxFlags nativeFlags = {};
    if (fbits->ignoreCase)
      nativeFlags |= regex::constants::icase;
    if (fbits->multiline)
      nativeFlags |= regex::constants::multiline;

    auto patternText = StringPrimitive::createStringView(runtime, pattern);
    llvm::SmallVector<char16_t, 16> patternText16;
    patternText.copyUTF16String(patternText16);

    // Build the regex.
    regex::Regex<regex::U16RegexTraits> regex(
        patternText16.begin(), patternText16.end(), nativeFlags);

    if (!regex.valid()) {
      runtime->raiseSyntaxError(
          TwineChar16("Invalid RegExp pattern: ") +
          regex::constants::messageForError(regex.getError()));
      return ExecutionStatus::EXCEPTION;
    }
    // The regex is valid. Compile and store its bytecode.
    selfHandle->bytecode_ = llvm::makeArrayRef(regex.compile());
  }

  return ExecutionStatus::RETURNED;
}

OptValue<JSRegExp::FlagBits> JSRegExp::FlagBits::fromString(StringView str) {
  // A flags string may contain i,m,g, in any order, but at most once each
  auto error = llvm::NoneType::None;
  JSRegExp::FlagBits ret = {};
  for (char16_t c : str) {
    switch (c) {
      case u'i':
        if (ret.ignoreCase)
          return error;
        ret.ignoreCase = 1;
        break;
      case u'm':
        if (ret.multiline)
          return error;
        ret.multiline = 1;
        break;
      case u'g':
        if (ret.global)
          return error;
        ret.global = 1;
        break;
      default:
        return error;
    }
  }
  return ret;
}

Handle<StringPrimitive> JSRegExp::getPattern(
    Handle<JSRegExp> selfHandle,
    Runtime *runtime) {
  return runtime->makeHandle(
      JSObject::getInternalProperty(
          selfHandle.get(), runtime, RegExpSlotIndexes::pattern)
          .getString());
}

template <typename CharT, typename Traits>
CallResult<RegExpMatch> performSearch(
    Runtime *runtime,
    llvm::ArrayRef<uint8_t> bytecode,
    const CharT *start,
    uint32_t stringLength,
    uint32_t searchStartOffset,
    regex::constants::MatchFlagType matchFlags) {
  regex::MatchResults<const CharT *> nativeMatchRanges;
  auto matchResult = regex::searchWithBytecode(
      bytecode,
      start + searchStartOffset,
      start + stringLength,
      nativeMatchRanges,
      matchFlags);
  if (matchResult == regex::MatchRuntimeResult::StackOverflow) {
    runtime->raiseRangeError("Maximum regex stack depth reached");
    return ExecutionStatus::EXCEPTION;
  } else if (matchResult == regex::MatchRuntimeResult::NoMatch) {
    return RegExpMatch{}; // not found.
  }
  size_t matchRangeCount = nativeMatchRanges.size();
  assert(matchRangeCount > 0);
  RegExpMatch match;
  match.reserve(matchRangeCount);
  for (size_t i = 0; i < matchRangeCount; i++) {
    const auto &submatch = nativeMatchRanges[i];
    if (!submatch.matched) {
      assert(i > 0 && "match_result[0] should always match");
      match.push_back(llvm::None);
    } else {
      uint32_t pos = submatch.first - start;
      uint32_t length = submatch.length();
      match.push_back(RegExpMatchRange{pos, length});
    }
  }
  assert(!match.empty() && "Unexpected empty match");
  return match;
}

CallResult<RegExpMatch> JSRegExp::search(
    Handle<JSRegExp> selfHandle,
    Runtime *runtime,
    Handle<StringPrimitive> strHandle,
    uint32_t searchStartOffset) {
  assert(!selfHandle->bytecode_.empty() && "Missing bytecode");
  auto input = StringPrimitive::createStringView(runtime, strHandle);

  // Note we may still have a match if searchStartOffset == str.size(),
  // if the regexp can match an empty string
  if (searchStartOffset > input.length()) {
    return RegExpMatch{}; // no match possible
  }

  // Tell the regex if the previous character is available
  // This is important to ensure that ^ does not match in the middle of the
  // string, among other reasons.
  auto matchFlags = regex::constants::matchDefault;
  if (searchStartOffset > 0) {
    matchFlags |= regex::constants::matchPreviousCharAvailable;
  }

  CallResult<RegExpMatch> matchResult = RegExpMatch{};
  if (input.isASCII()) {
    matchFlags |= regex::constants::matchInputAllAscii;
    matchResult = performSearch<char, regex::ASCIIRegexTraits>(
        runtime,
        selfHandle->bytecode_,
        input.castToCharPtr(),
        input.length(),
        searchStartOffset,
        matchFlags);
  } else {
    matchResult = performSearch<char16_t, regex::U16RegexTraits>(
        runtime,
        selfHandle->bytecode_,
        input.castToChar16Ptr(),
        input.length(),
        searchStartOffset,
        matchFlags);
  }

  // Only update on successful match.
  if (LLVM_UNLIKELY(matchResult == ExecutionStatus::EXCEPTION)) {
    return ExecutionStatus::EXCEPTION;
  } else if (!matchResult->empty()) {
    runtime->regExpLastInput = strHandle.getHermesValue();
    runtime->regExpLastRegExp = selfHandle.getHermesValue();
    runtime->regExpLastMatch = *matchResult;
  }
  return matchResult;
}

void JSRegExp::_finalizeImpl(GCCell *cell, GC *) {
  JSRegExp *self = vmcast<JSRegExp>(cell);
  self->~JSRegExp();
}

/// \return an escaped string equivalent to \p pattern.
/// This is used to construct the 'source' property of RegExp. This requires
/// us to return a string from which the regexp may be reconstructed as if
/// from a /foo/ style literal. Note this is different from the RegExp
/// constructor that takes a string, e.g. new RegExp("/") returns a regexp
/// that matches /, but
/// /// does not (it's a comment!). So we may have to perform surgery on the
/// pattern.
CallResult<HermesValue> JSRegExp::escapePattern(
    Handle<StringPrimitive> pattern,
    Runtime *runtime) {
  SmallU16String<32> result;
  result.reserve(pattern->getStringLength());
  auto patternView = StringPrimitive::createStringView(runtime, pattern);
  bool isBackslashed = false;
  for (char16_t c : patternView) {
    switch (c) {
      case u'/':
        // Avoid premature end of regex.
        // TODO nice to have: don't do this if we are in square brackets.
        // /[/]/ is valid and the middle / does not need to be escaped. However
        // /[\/]/ is also valid and means the same thing (CharacterEscape
        // production from regexp grammar). Still it would be nice to not
        // unnecessarily mangle the user's supplied pattern.
        result.append(isBackslashed ? "/" : "\\/");
        break;

      // Escape line terminators. See ES5.1 7.3.
      case u'\n':
        result.append(isBackslashed ? "n" : "\\n");
        break;

      case u'\r':
        result.append(isBackslashed ? "r" : "\\r");
        break;

      case 0x2028:
        result.append(isBackslashed ? "u2028" : "\\u2028");
        break;

      case 0x2029:
        result.append(isBackslashed ? "u2029" : "\\u2029");
        break;

      default:
        result.append(c);
        break;
    }
    isBackslashed = (c == u'\\') && !isBackslashed;
  }
  // "If P is the empty String, this specification can be met by letting S be
  // '(?:)'."
  if (result.empty()) {
    result = u"(?:)";
  }

  // Avoid unnecessary allocation in the likely event the source and pattern
  // match.
  if (patternView.equals(result.arrayRef())) {
    return pattern.getHermesValue();
  }
  return StringPrimitive::create(runtime, result);
}

} // namespace vm
} // namespace hermes
