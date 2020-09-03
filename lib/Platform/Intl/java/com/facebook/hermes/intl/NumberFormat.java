/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.facebook.hermes.intl;

import android.icu.text.DecimalFormat;
import android.icu.util.Currency;
import android.icu.util.Measure;
import android.icu.util.MeasureUnit;
import android.os.Build;

import java.text.AttributedCharacterIterator;
import java.text.CharacterIterator;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import static com.facebook.hermes.intl.IPlatformNumberFormatter.Style.currency;
import static com.facebook.hermes.intl.IPlatformNumberFormatter.Style.percent;
import static com.facebook.hermes.intl.IPlatformNumberFormatter.Style.unit;

/**
 * This class represents the Java part of the Android
 * Intl.NumberFormat implementation.  The interaction with the Hermes
 * JaveScript internals are implemented in C++ and should not
 * generally need to be changed.  Implementers' notes here will
 * describe what parts of the ECMA 402 spec remain to be implemented.
 * <p>
 * Also see the implementer' notes on DateTimeFormat.java.
 */
public class NumberFormat {
    // options are localeMatcher:string, numberingSystem:string, notation:string,
    // compactDisplay:string, useGrouping:string, signDisplay:string
    //
    // Implementer note: The ctor corresponds roughly to
    // https://tc39.es/ecma402/#sec-initializenumberformat
    // Also see the implementer notes on DateTimeFormat#DateTimeFormat()

    private IPlatformNumberFormatter.Style mResolvedStyle = null;

    private String mResolvedCurrency = null;
    private IPlatformNumberFormatter.CurrencyDisplay mResolvedCurrencyDisplay = IPlatformNumberFormatter.CurrencyDisplay.symbol;
    private IPlatformNumberFormatter.CurrencySign mResolvedCurrencySign = IPlatformNumberFormatter.CurrencySign.standard;

    private MeasureUnit mResolveMeasureUnitPlatform;
    private String mResolvedUnit = null;
    private IPlatformNumberFormatter.UnitDisplay mResolvedUnitDisplay;

    private boolean mGroupingUsed = true;

    private int mResolvedMinimumIntegerDigits = -1, mResolvedMinimumFractionDigits = -1, mResolvedMaximumFractionDigits = -1;
    private int mResolvedMinimumSignificantDigits = -1, mResolvedMaximumSignificantDigits = -1;

    private IPlatformNumberFormatter.RoundingType mRoundingType;

    private IPlatformNumberFormatter.SignDisplay mResolvedSignDisplay = IPlatformNumberFormatter.SignDisplay.auto;

    private IPlatformNumberFormatter mPlatformNumberFormatter = null;

    private String mResolvedNumberingSystem = null;

    private IPlatformNumberFormatter.Notation mResolvedNotation = null;
    private IPlatformNumberFormatter.CompactDisplay mResolvedCompactDisplay;

    // Note :: https://developer.android.com/reference/android/icu/number/NumberFormatter is not available until API 30.
    // private DecimalFormat mDecimalFormat = null;
    // private java.text.Format mFormatter = null;
    // private IPlatformNumberFormatter mPlatformNumberFormatter;

    private ILocaleObject mResolvedLocaleObject = null;
    private ILocaleObject mReolvedRequestedLocaleObject = null;

    private void resolveLocale(List<String> locales, String localeMatcher) throws JSRangeErrorException {
        if (locales == null || locales.size() == 0) {
            mResolvedLocaleObject = LocaleObject.createDefault();
            mReolvedRequestedLocaleObject = mResolvedLocaleObject;
        } else {
            PlatformCollator.LocaleResolutionResult localeResolutionResult = PlatformNumberFormatter.resolveLocales(locales, localeMatcher);
            mResolvedLocaleObject = localeResolutionResult.resolvedLocale;
            mReolvedRequestedLocaleObject = localeResolutionResult.resolvedDesiredLocale;
        }
    }

    // This list should be kept alphabetically ordered so that we can binary search in it.
    private static String[] s_sanctionedSimpleUnitIdentifiers = {"acre",
            "bit", "byte",
            "celsius", "centimeter",
            "day", "degree",
            "fahrenheit", "fluid-ounce", "foot",
            "gallon", "gigabit", "gigabyte", "gram",
            "hectare", "hour",
            "inch",
            "kilobit", "kilobyte", "kilogram", "kilometer",
            "liter",
            "megabit", "megabyte", "meter", "mile", "mile-scandinavian", "milliliter", "millimeter", "millisecond", "minute", "month",
            "ounce",
            "percent", "petabyte", "pound",
            "second", "stone",
            "terabit", "terabyte",
            "week",
            "yard", "year"};

    private boolean isSanctionedSimpleUnitIdentifier(String unitIdentifier) {
        return java.util.Arrays.binarySearch(s_sanctionedSimpleUnitIdentifiers, unitIdentifier) >= 0;
    }

    private boolean isWellFormedUnitIdentifier(String unitIdentifier) {
        // 1.
        if (isSanctionedSimpleUnitIdentifier(unitIdentifier))
            return true;

        String per = "-per-";
        int indexOfPer = unitIdentifier.indexOf(per);

        // 2
        if (indexOfPer < 0)
            return false;

        if (unitIdentifier.indexOf(per, indexOfPer + 1) >= 0)
            return false;

        // 3, 4
        String numerator = unitIdentifier.substring(0, indexOfPer);
        if (!isSanctionedSimpleUnitIdentifier(numerator))
            return false;

        // 5, 6
        String denominator = unitIdentifier.substring(indexOfPer + per.length());
        if (!isSanctionedSimpleUnitIdentifier(denominator))
            return false;

        // 7
        return true;
    }

    private String normalizeCurrencyCode(String currencyCode) {
        // https://tc39.es/ecma402/#sec-case-sensitivity-and-case-mapping
        // Note that we should convert only upper case translation in ASCII range.
        StringBuffer normalized = new StringBuffer(currencyCode.length());
        int offset = 'a' - 'A';
        for (int idx = 0; idx < currencyCode.length(); idx++) {
            char c = currencyCode.charAt(idx);
            if (c >= 'a' && c <= 'z') {
                normalized.append((char) (c - offset));
            } else {
                normalized.append(c);
            }
        }

        return normalized.toString();
    }

    private boolean isWellFormedCurrencyCode(String currencyCode) {
        String normalized = normalizeCurrencyCode(currencyCode);
        return normalized.matches("^[A-Z][A-Z][A-Z]$");
    }

    private int getCurrencyDigits(String currencyCode) throws JSRangeErrorException {
        try {
            return Currency.getInstance(currencyCode).getDefaultFractionDigits();
        } catch (IllegalArgumentException ex) {
            throw new JSRangeErrorException("Invalid currency code !");
        }
    }

    private void setNumberFormatUnitOptions(Map<String, Object> options) throws JSRangeErrorException {
        // 3,4
        // TODO :: Make is more robust.
        mResolvedStyle = Enum.valueOf(IPlatformNumberFormatter.Style.class, OptionHelpers.resolveStringOption(options, "style", new String[]{"decimal", "currency", "percent", "unit"}, "decimal"));

        // 5
        String currencyCode = OptionHelpers.resolveStringOption(options, "currency", new String[]{}, "");

        // 6a is done in C++ code.
        // 6b
        if (!currencyCode.isEmpty() && !isWellFormedCurrencyCode(currencyCode))
            throw new JSRangeErrorException("Malformed currency code !");

        String currencyDisplay = OptionHelpers.resolveStringOption(options, "currencyDisplay", new String[]{"symbol", "narrowSymbol", "code", "name"}, "symbol");
        String currencySign = OptionHelpers.resolveStringOption(options, "currencySign", new String[]{"accounting", "standard"}, "standard");

        String unitId = OptionHelpers.resolveStringOption(options, "unit", new String[]{}, "");

        // 11 is done in C++ code.
        // 12
        if (!unitId.isEmpty() && !isWellFormedUnitIdentifier(unitId))
            throw new JSRangeErrorException("Malformed unit identifier !");

        String unitDisplay = OptionHelpers.resolveStringOption(options, "unitDisplay", new String[]{"long", "short", "narrow"}, "short");

        if (mResolvedStyle == currency) {
            mResolvedCurrency = normalizeCurrencyCode(currencyCode);
            mResolvedCurrencyDisplay = Enum.valueOf(IPlatformNumberFormatter.CurrencyDisplay.class, currencyDisplay);
            mResolvedCurrencySign = Enum.valueOf(IPlatformNumberFormatter.CurrencySign.class, currencySign);
        } else if (mResolvedStyle == IPlatformNumberFormatter.Style.unit) {
            mResolvedUnit = unitId;
            mResolvedUnitDisplay = OptionHelpers.searchEnum(IPlatformNumberFormatter.UnitDisplay.class, unitDisplay);
        }
    }

    private void setNumberFormatDigitOptions(Map<String, Object> options, int mnfdDefault, int mxfdDefault, IPlatformNumberFormatter.Notation notation) throws JSRangeErrorException {
        int mnid = OptionHelpers.resolveIntegerOption(options, "minimumIntegerDigits", 1, 21, 1);

        int mnfd = OptionHelpers.resolveIntegerOption(options, "minimumFractionDigits", -1); // TODO These are differenct for currencies !
        int mxfd = OptionHelpers.resolveIntegerOption(options, "maximumFractionDigits", -1);

        int mnsd = OptionHelpers.resolveIntegerOption(options, "minimumSignificantDigits", -1);
        int mxsd = OptionHelpers.resolveIntegerOption(options, "maximumSignificantDigits", -1);

        mResolvedMinimumIntegerDigits = mnid;

        if (mnsd != -1 || mxsd != -1) {
            mRoundingType = IPlatformNumberFormatter.RoundingType.significantDigits;
            mnsd = OptionHelpers.DefaultNumberOption(mnsd, 1, 21, 1);
            mxsd = OptionHelpers.DefaultNumberOption(mxsd, mnsd, 21, 21);

            mResolvedMinimumSignificantDigits = mnsd;
            mResolvedMaximumSignificantDigits = mxsd;
        } else if (mnfd != -1 || mxfd != -1) {
            mRoundingType = IPlatformNumberFormatter.RoundingType.fractionDigits;
            mnfd = OptionHelpers.DefaultNumberOption(mnfd, 0, 20, mnfdDefault);
            int mxfdActualDefault = Integer.max(mnfd, mxfdDefault);
            mxfd = OptionHelpers.DefaultNumberOption(mxfd, mnfd, 20, mxfdActualDefault);

            mResolvedMinimumFractionDigits = mnfd;
            mResolvedMaximumFractionDigits = mxfd;
        } else if (mResolvedNotation == IPlatformNumberFormatter.Notation.compact) {
            mRoundingType = IPlatformNumberFormatter.RoundingType.compactRounding;
        } else if (mResolvedNotation == IPlatformNumberFormatter.Notation.engineering){
            // The default setting for engineering notation. It is not based on the spec, but is required by our implementation of engineering notation.
            // From https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/classicu_1_1DecimalFormat.html
            // If areSignificantDigitsUsed() returns false, then the minimum number of significant digits shown is one,
            // and the maximum number of significant digits shown is the sum of the minimum integer and maximum fraction digits,
            // and is unaffected by the maximum integer digits.
            //
            // In short, the minimum integer will be set to 1 and hence to achieve maximum default fraction digits of "3" (as in spec), we should set the maximum fraction digits to "5"
            mRoundingType = IPlatformNumberFormatter.RoundingType.fractionDigits;
            mResolvedMaximumFractionDigits = 5;
        } else {
            mRoundingType = IPlatformNumberFormatter.RoundingType.fractionDigits;
            mResolvedMinimumFractionDigits = mnfdDefault;
            mResolvedMaximumFractionDigits = mxfdDefault;
        }
    }

    private void initializeNumberFormat(List<String> locales, Map<String, Object> options) throws JSRangeErrorException {

        // 5,6
        String desiredLocaleMatcher = OptionHelpers.resolveStringOption(options, Constants.LOCALEMATCHER, Constants.LOCALEMATCHER_POSSIBLE_VALUES, Constants.LOCALEMATCHER_BESTFIT);

        // 10-14 ( Note we are resolving locales before resolving number formats .. This is to make sure that we know the picked locale and hence the locale exensions)
        resolveLocale(locales, desiredLocaleMatcher);

        // 7,8,9
        String numberingSystem = "";
        if (options.containsKey("numberingSystem")) {
            numberingSystem = mResolvedNumberingSystem = OptionHelpers.resolveStringOption(options, "numberingSystem", new String[]{}, "");
        }

        ArrayList<String> numberingSystemLocaleKeywords = mReolvedRequestedLocaleObject.getUnicodeExtensions("nu");
        if (numberingSystemLocaleKeywords != null && !numberingSystemLocaleKeywords.isEmpty()) {
            String numberingSystemLocaleKeyword = numberingSystemLocaleKeywords.get(0);
            if (!numberingSystem.isEmpty() && !numberingSystemLocaleKeyword.equals(numberingSystem)) {
                throw new JSRangeErrorException("Numbering system mismatch between options and locale keyword.");
            }
        }

        mResolvedNumberingSystem = numberingSystem;

        setNumberFormatUnitOptions(options);

        // 17, 18
        int mnfdDefault;
        int mxfdDefault;
        if (mResolvedStyle == currency) {
            int cDigits = getCurrencyDigits(mResolvedCurrency);

            mnfdDefault = cDigits;
            mxfdDefault = cDigits;
        } else {
            mnfdDefault = 0;

            if (mResolvedStyle == percent)
                mxfdDefault = 0;
            else
                mxfdDefault = 3;
        }

        // 19, 20
        String notation = OptionHelpers.resolveStringOption(options, "notation", new String[]{"standard", "scientific", "engineering", "compact"}, "standard");
        mResolvedNotation = Enum.valueOf(IPlatformNumberFormatter.Notation.class, notation);

        // 21
        setNumberFormatDigitOptions(options, mnfdDefault, mxfdDefault, mResolvedNotation);

        // 22, 23
        String compactDisplay = OptionHelpers.resolveStringOption(options, "compactDisplay", new String[]{"short", "long"}, "short");
        if (mResolvedNotation == IPlatformNumberFormatter.Notation.compact) {
            mResolvedCompactDisplay = OptionHelpers.searchEnum(IPlatformNumberFormatter.CompactDisplay.class, compactDisplay);
        }

        mGroupingUsed = OptionHelpers.resolveBooleanOption(options, "useGrouping", true);
        mResolvedSignDisplay = Enum.valueOf(IPlatformNumberFormatter.SignDisplay.class, OptionHelpers.resolveStringOption(options, "signDisplay", new String[]{"auto", "never", "always", "exceptZero"}, "auto"));
    }

    public NumberFormat(List<String> locales, Map<String, Object> options) throws JSRangeErrorException {
        initializeNumberFormat(locales, options);
        mResolvedNumberingSystem = PlatformNumberFormatterICU.configureNumberingSystem(mResolvedNumberingSystem, mResolvedLocaleObject);

        IPlatformNumberFormatter platformNumberFormatter;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N)
            mPlatformNumberFormatter = PlatformNumberFormatterICU.createDecimalFormat(mResolvedLocaleObject, mResolvedStyle, mResolvedCurrencySign, mResolvedNotation, mResolvedCompactDisplay);
        else
            mPlatformNumberFormatter = PlatformNumberFormatterAndroid.createDecimalFormat(mResolvedLocaleObject, mResolvedStyle, mResolvedCurrencySign, mResolvedNotation, mResolvedCompactDisplay);

        mPlatformNumberFormatter.configureCurrency(mResolvedCurrency, mResolvedCurrencyDisplay)
                .configureGrouping(mGroupingUsed)
                .configureMinIntergerDigits(mResolvedMinimumIntegerDigits)
                .configureSignificantDigits(mRoundingType, mResolvedMinimumSignificantDigits, mResolvedMaximumSignificantDigits)
                .configureFractinDigits(mRoundingType, mResolvedMinimumFractionDigits, mResolvedMaximumFractionDigits)
                .configureSignDisplay(mResolvedSignDisplay)
                .configureUnits(mResolvedUnit, mResolvedUnitDisplay);
    }

    // options are localeMatcher:string
    //
    // Implementer note: This method corresponds roughly to
    // https://tc39.es/ecma402/#sec-intl.numberformat.supportedlocalesof
    //
    // The notes on DateTimeFormat#DateTimeFormat() for Locales and
    // Options also apply here.
    public static List<String> supportedLocalesOf(List<String> locales, Map<String, Object> options) throws JSRangeErrorException {
        ArrayList<String> supportedLocales = new ArrayList<>();

        String localeMatcher = Constants.LOCALEMATCHER_BESTFIT;
        if (options.containsKey(Constants.LOCALEMATCHER)) {
            localeMatcher = (String) options.get(Constants.LOCALEMATCHER);
        }

        return PlatformNumberFormatter.filterLocales(locales, localeMatcher);
    }

    // Implementer note: This method corresponds roughly to
    // https://tc39.es/ecma402/#sec-intl.numberformat.prototype.resolvedoptions
    //
    // Also see the implementer notes on DateTimeFormat#resolvedOptions()
    public Map<String, Object> resolvedOptions() throws JSRangeErrorException {
        HashMap<String, Object> finalResolvedOptions = new HashMap<>();

        finalResolvedOptions.put(Constants.LOCALE, mResolvedLocaleObject.toCanonicalTag());
        finalResolvedOptions.put("numberingSystem", mResolvedNumberingSystem);

        finalResolvedOptions.put("style", mResolvedStyle.toString());

        if (mResolvedStyle == currency) {
            finalResolvedOptions.put("currency", mResolvedCurrency);
            finalResolvedOptions.put("currencyDisplay", mResolvedCurrencyDisplay.toString());
            finalResolvedOptions.put("currencySign", mResolvedCurrencySign.toString());
        } else if (mResolvedStyle == unit) {
            finalResolvedOptions.put("unit", mResolvedUnit);
            finalResolvedOptions.put("unitDisplay", mResolvedUnitDisplay.toString().toLowerCase()); // TODO
        }

        if (mResolvedMinimumIntegerDigits != -1)
            finalResolvedOptions.put("minimumIntegerDigits", mResolvedMinimumIntegerDigits);

        if (mRoundingType == IPlatformNumberFormatter.RoundingType.significantDigits) {
            if (mResolvedMaximumSignificantDigits != -1)
                finalResolvedOptions.put("minimumSignificantDigits", mResolvedMaximumSignificantDigits);

            if (mResolvedMinimumSignificantDigits != -1)
                finalResolvedOptions.put("maximumSignificantDigits", mResolvedMinimumSignificantDigits);

        } else if (mRoundingType == IPlatformNumberFormatter.RoundingType.fractionDigits) {

            if (mResolvedMinimumFractionDigits != -1)
                finalResolvedOptions.put("minimumFractionDigits", mResolvedMinimumFractionDigits);

            if (mResolvedMaximumFractionDigits != -1)
                finalResolvedOptions.put("maximumFractionDigits", mResolvedMaximumFractionDigits);

        } else if (mRoundingType == IPlatformNumberFormatter.RoundingType.compactRounding)
            ; // TODO

        finalResolvedOptions.put("useGrouping", mGroupingUsed);

        finalResolvedOptions.put("notation", mResolvedNotation.toString());
        if (mResolvedNotation == IPlatformNumberFormatter.Notation.compact) {
            finalResolvedOptions.put("compactDisplay", mResolvedCompactDisplay.toString().toLowerCase()); // TODO :: FIx these
        }

        finalResolvedOptions.put("signDisplay", mResolvedSignDisplay.toString());


        return finalResolvedOptions;
    }

    // Implementer note: This method corresponds roughly to
    // https://tc39.es/ecma402/#sec-formatnumber
    public String format(double n) throws JSRangeErrorException {
        String result = mPlatformNumberFormatter.format(n);
        return result;
    }

    private static String fieldToString(DecimalFormat.Field field, double x) {
        if (field == DecimalFormat.Field.SIGN) {
            if (Double.compare(x, +0) >= 0) {
                return "plusSign";
            }
            return "minusSign";
        }
        if (field == DecimalFormat.Field.INTEGER) {
            if (Double.isNaN(x)) {
                return "nan";
            }
            if (Double.isInfinite(x)) {
                return "infinity";
            }
            return "integer";
        }
        if (field == DecimalFormat.Field.FRACTION) {
            return "fraction";
        }
        if (field == DecimalFormat.Field.EXPONENT) {
            return "exponentInteger";
        }
        if (field == DecimalFormat.Field.EXPONENT_SIGN) {
            return "exponentMinusSign";
        }
        if (field == DecimalFormat.Field.EXPONENT_SYMBOL) {
            return "exponentSeparator";
        }
        if (field == DecimalFormat.Field.DECIMAL_SEPARATOR) {
            return "decimal";
        }
        if (field == DecimalFormat.Field.GROUPING_SEPARATOR) {
            return "group";
        }
        if (field == DecimalFormat.Field.PERCENT) {
            return "percentSign";
        }
        if (field == DecimalFormat.Field.PERMILLE) {
            return "permilleSign";
        }
        if (field == DecimalFormat.Field.CURRENCY) {
            return "currency";
        }
        // Report unsupported/unexpected number fields as literal.
        return "literal";
    }

    // Implementer note: This method corresponds roughly to
    // https://tc39.es/ecma402/#sec-formatnumbertoparts
    public List<Map<String, String>> formatToParts(double n) throws JSRangeErrorException {
        ArrayList<Map<String, String>> parts = new ArrayList<>();

        AttributedCharacterIterator iterator = mPlatformNumberFormatter.formatToParts(n);
        StringBuilder sb = new StringBuilder();
        for (char ch = iterator.first(); ch != CharacterIterator.DONE; ch = iterator.next()) {
            sb.append(ch);
            if (iterator.getIndex() + 1 == iterator.getRunLimit()) {
                Iterator<AttributedCharacterIterator.Attribute> keyIterator = iterator.getAttributes().keySet().iterator();
                String key;
                if (keyIterator.hasNext()) {
                    key = fieldToString((DecimalFormat.Field) keyIterator.next(), n);
                } else {
                    key = "literal";
                }
                String value = sb.toString();
                sb.setLength(0);

                HashMap<String, String> part = new HashMap<>();
                part.put("type", key);
                part.put("value", value);
                parts.add(part);
            }
        }

        return parts;
    }
}

