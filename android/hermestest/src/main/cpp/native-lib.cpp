#include <jni.h>

#include <jsi/jsi.h>
#include <hermes/hermes.h>

using namespace facebook::hermes;
using namespace facebook::jsi;

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hermestest_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++ with hermes ";

    ::hermes::vm::RuntimeConfig runtimeConfig_;
    std::unique_ptr<HermesRuntime> hermesRuntime = makeHermesRuntime(runtimeConfig_);

    Value jvalResult = hermesRuntime->evaluateJavaScript(std::make_shared<StringBuffer>("var x = 'Hello.. Im from Javascript !';x;"), "");
    if(jvalResult.isString()) {
        std::string result = jvalResult.asString(*hermesRuntime).utf8(*hermesRuntime);
        return env->NewStringUTF(result.c_str());
    } else {
        return env->NewStringUTF("Failed to get string from Javascript ..");
    }
}