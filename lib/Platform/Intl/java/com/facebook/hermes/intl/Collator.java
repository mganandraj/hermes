/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.facebook.hermes.intl;

import android.icu.text.RuleBasedCollator;
import android.os.Build;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Predicate;

class PlatformCollatorObject {
  private android.icu.text.RuleBasedCollator icu4jCollator = null;
  private java.text.RuleBasedCollator legacyCollator = null;

  private PlatformCollatorObject(LocaleObject locale) {
    if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      icu4jCollator = (RuleBasedCollator) RuleBasedCollator.getInstance(locale.getICU4jLocale());

      // Normalization is always on by the spec. We don't know whether the text is already normalized.
      // This has perf implications.
      icu4jCollator.setDecomposition(android.icu.text.Collator.CANONICAL_DECOMPOSITION);

    } else {
      legacyCollator = (java.text.RuleBasedCollator) java.text.Collator.getInstance(locale.getLegacyLocale());
      legacyCollator.setDecomposition(java.text.Collator.CANONICAL_DECOMPOSITION);
    }
  }

  public static  PlatformCollatorObject getInstance(LocaleObject locale) {
    return new PlatformCollatorObject(locale);
  }

  public boolean isNumericCollationSupported() {
    if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      return true;
    } else {
      return false;
    }
  }

  public void setNumericAttribute(boolean numeric) {
    if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      icu4jCollator.setNumericCollation(numeric);
    } else {
      throw new UnsupportedOperationException("Numeric collation not supported !");
    }
  }

  public boolean isCaseFirstCollationSupported() {
    if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      return true;
    } else {
      return false;
    }
  }

  public void setCaseFirstAttribute(String caseFirst) {
    if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      switch (caseFirst) {
        case "upper":
          icu4jCollator.setUpperCaseFirst();
          break;

        case "lower":
          icu4jCollator.setLowerCaseFirst();
          break;

        case "false":
        default:
          icu4jCollator.setCaseFirstDefault();
          break;
      }
    } else {
      throw new UnsupportedOperationException("CaseFirst collation attribute not supported !");
    }
  }
}

/**
 * This class represents the Java part of the Android Intl.Collator
 * implementation.  The interaction with the Hermes JaveScript
 * internals are implemented in C++ and should not generally need to
 * be changed.  Implementers' notes here will describe what parts of
 * the ECMA 402 spec remain to be implemented.
 *
 * Also see the implementer' notes on DateTimeFormat.java.
 */
public class Collator {

  private static final String defaultSensitivity = "variant";
  private static final String defaultUsage = "sort";
  private static final boolean defaultIsIgnorePunctuation = false;

  private static final boolean defaultIsNumeric = false;
  private static final String defaultCaseFirst = "false";

  private LocaleObject resolvedLocaleObject = null;
  private String resolvedSensitivity = defaultSensitivity;
  private String resolvedUsage = defaultUsage;
  private boolean resolvedIsIgnorePunctuation = defaultIsIgnorePunctuation;

  // Note that these two are optional properties. If not set through the extension or through options, it won't be used.
  private boolean resolvedIsNumeric = defaultIsNumeric;
  private boolean resolvedIsNumericSet = false;

  private String resolvedCaseFirst = defaultCaseFirst;
  private boolean resolvedCaseFirstSet = false;

  private PlatformCollatorObject platformCollatorObject = null;

  private boolean containsString(String[] list, String value) {
    return Arrays.stream(list).anyMatch(new Predicate<String>() {
      @Override
      public boolean test(String value) {
        return value.equals(value);
      }
    });
  }

  private void resolveArguments(List<String> locales, Map<String, Object> options) throws JSRangeErrorException {

    if(locales == null || locales.size() == 0) {
      resolvedLocaleObject = LocaleObject.constructDefault();
    } else {
      String localeMatcher = (String) options.get(Constants.LOCALEMATCHER);
      if(localeMatcher == null)
        localeMatcher = Constants.LOCALEMATCHER_BESTFIT;

      resolvedLocaleObject = LocaleMatcher.lookupAgainstAvailableLocales(locales, localeMatcher);
    }

    if (options.containsKey(Constants.USAGE)) {
      final String optionUsage = (String) options.get(Constants.USAGE);
      if(containsString(Constants.COLLATOR_USAGE_POSSIBLE_VALUES, optionUsage)) {
        resolvedUsage = optionUsage;
      } else {
        resolvedUsage = Constants.SORT;
      }
    }

  }

  // options are usage:string, localeMatcher:string, numeric:boolean, caseFirst:string,
  // sensitivity:string, ignorePunctuation:boolean
  //
  // Implementer note: The ctor corresponds roughly to
  // https://tc39.es/ecma402/#sec-initializecollator
  // Also see the implementer notes on DateTimeFormat#DateTimeFormat()
  public Collator(List<String> locales, Map<String, Object> options)
    throws JSRangeErrorException
  {
    resolveArguments(locales, options);

    // 1.
    if(locales == null || locales.size() == 0) {
      resolvedLocaleObject = LocaleObject.constructDefault();
    } else {
      String localeMatcher = (String) options.get(Constants.LOCALEMATCHER);
      if(localeMatcher == null)
        localeMatcher = Constants.LOCALEMATCHER_BESTFIT;

      resolvedLocaleObject = LocaleMatcher.lookupAgainstAvailableLocales(locales, localeMatcher);
    }





    platformCollatorObject = PlatformCollatorObject.getInstance(resolvedLocaleObject);

    // 2.
    // Options take priority
    if(platformCollatorObject.isNumericCollationSupported()) {
      if (options.containsKey(Constants.NUMERIC)) {
        boolean optionIsNumeric = (boolean) options.get(Constants.NUMERIC);
        resolvedIsNumeric = optionIsNumeric;
        resolvedIsNumericSet = true;
      } else {
        // TODO: Check extensions in localeId
      }

      if(resolvedIsNumericSet)
        platformCollatorObject.setNumericAttribute(resolvedIsNumeric);
    }


    if(platformCollatorObject.isCaseFirstCollationSupported()) {
      if (options.containsKey(Constants.CASEFIRST)) {
        String optionCaseFirst = (String) options.get(Constants.CASEFIRST);
        resolvedCaseFirst = optionCaseFirst;
        resolvedCaseFirstSet = true;
      } else {
        // TODO: Check extensions in localeId
      }

      if (resolvedCaseFirstSet)
        platformCollatorObject.setCaseFirstAttribute(resolvedCaseFirst);
    }


  }

  // options are localeMatcher:string
  //
  // Implementer note: This method corresponds roughly to
  // https://tc39.es/ecma402/#sec-intl.collator.supportedlocalesof
  //
  // The notes on DateTimeFormat#DateTimeFormat() for Locales and
  // Options also apply here.
  public static List<String> supportedLocalesOf(List<String> locales, Map<String, Object> options)
    throws JSRangeErrorException
  {
    List<LocaleObject> supportedLocaleObjects = LocaleMatcher.filterAgainstAvailableLocales(locales);
    ArrayList<String> supportedLocaleIds = new ArrayList<>();
    for (LocaleObject supportedLocaleObject : supportedLocaleObjects)
      supportedLocaleIds.add(supportedLocaleObject.toLocaleId());

    return supportedLocaleIds;
  }

  // Implementer note: This method corresponds roughly to
  // https://tc39.es/ecma402/#sec-intl.collator.prototype.resolvedoptions
  //
  // Also see the implementer notes on DateTimeFormat#resolvedOptions()
  public Map<String, Object> resolvedOptions() {

    HashMap<String, Object> resolvedOptions = new HashMap<>();
    resolvedOptions.put("locale", resolvedLocaleObject.toLocaleId());
    resolvedOptions.put("sensitivity", resolvedSensitivity);
    resolvedOptions.put("usage", resolvedUsage);
    resolvedOptions.put("ignorePunctuation", resolvedIsIgnorePunctuation);
    resolvedOptions.put("collation", "default");

    if(resolvedIsNumericSet)
      resolvedOptions.put("numeric", resolvedIsNumeric);

    if(resolvedIsCaseFirstSet)
      resolvedOptions.put("caseFirst", resolvedIsCaseFirst);

    return resolvedOptions;
  }

  // Implementer note: This method corresponds roughly to
  // https://tc39.es/ecma402/#sec-collator-comparestrings
  public double compare(String a, String b) {

    return a.compareTo(b);
  }
}

