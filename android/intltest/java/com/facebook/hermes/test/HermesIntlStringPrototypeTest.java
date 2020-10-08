/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import android.content.res.AssetManager;
import android.icu.util.ULocale;
import android.test.InstrumentationTestCase;
import android.text.TextUtils;
import android.util.Log;

import static org.fest.assertions.api.Assertions.assertThat;

import com.facebook.hermes.test.JSRuntime;

import org.junit.Test;

import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

// Run "./gradlew :intltest:preparetest262" from the root to download and copy the test files to the APK assets.
public class HermesIntlStringPrototypeTest extends HermesIntlTest262Base {

    private static final String LOG_TAG = "HermesIntlStringPrototypeTest";

   @Test
   public void testIntlStringToLocaleLowerCase() throws IOException {

       String basePath = "test262-main/test/intl402/String/prototype/toLocaleLowerCase";
       Set<String> whilteList = new HashSet<>();
       Set<String> blackList = new HashSet<>();

       runTests(basePath, blackList, whilteList);
   }

   @Test
   public void testIntlStringToLocaleUpperCase() throws IOException {

       String basePath = "test262-main/test/intl402/String/prototype/toLocaleUpperCase";
       Set<String> whilteList = new HashSet<>();
       Set<String> blackList = new HashSet<>();

       runTests(basePath, blackList, whilteList);
   }

   @Test
   public void testIntlStringLocaleCompare() throws IOException {

       String basePath = "test262-main/test/intl402/String/prototype/localeCompare";
       Set<String> whilteList = new HashSet<>();
       Set<String> blackList = new HashSet<>();

       runTests(basePath, blackList, whilteList);
   }


}
