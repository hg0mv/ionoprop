/*
 * jni_bridge.c - JNI-híd az app-maghoz (Android: hu.ionoprop.app.Native)
 *
 * A nagy szövegek (JSON) byte[]-ként mennek át mindkét irányba, UTF-8-ban.
 * Így nem kell a JNI "módosított UTF-8" kódolásával foglalkozni.
 * A térkép int[]-be kerül (0xAARRGGBB), amit a Kotlin Bitmap.setPixels-szel tölt be.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "appcore.h"

#define FN(name) Java_hu_ionoprop_app_Native_##name

static AppCore *H(jlong h) { return (AppCore *)(intptr_t)h; }

/* byte[] -> nullával lezárt C szöveg (a hívó szabadítja fel); NULL marad NULL */
static char *bytes_to_cstr(JNIEnv *env, jbyteArray arr)
{
    if (!arr) return NULL;
    jsize n = (*env)->GetArrayLength(env, arr);
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    (*env)->GetByteArrayRegion(env, arr, 0, n, (jbyte *)s);
    s[n] = '\0';
    return s;
}

static jbyteArray cstr_to_bytes(JNIEnv *env, const char *s)
{
    if (!s) return NULL;
    jsize n = (jsize)strlen(s);
    jbyteArray arr = (*env)->NewByteArray(env, n);
    if (arr) (*env)->SetByteArrayRegion(env, arr, 0, n, (const jbyte *)s);
    return arr;
}

/* rövid, ASCII szövegekhez (útvonal, lokátor, host) */
static char *jstr(JNIEnv *env, jstring js)
{
    if (!js) return NULL;
    const char *u = (*env)->GetStringUTFChars(env, js, NULL);
    if (!u) return NULL;
    char *s = strdup(u);
    (*env)->ReleaseStringUTFChars(env, js, u);
    return s;
}

JNIEXPORT jlong JNICALL FN(nInit)(JNIEnv *env, jclass cls, jstring cacheDir, jstring configDir)
{
    (void)cls;
    char *c = jstr(env, cacheDir), *g = jstr(env, configDir);
    AppCore *a = ac_new(c, g);
    free(c);
    free(g);
    return (jlong)(intptr_t)a;
}

JNIEXPORT void JNICALL FN(nFree)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    ac_free(H(h));
}

JNIEXPORT jint JNICALL FN(nSetLocator)(JNIEnv *env, jclass cls, jlong h, jstring loc)
{
    (void)cls;
    char *l = jstr(env, loc);
    int r = l ? ac_set_locator(H(h), l) : -1;
    free(l);
    return r;
}

JNIEXPORT jint JNICALL FN(nLoad)(JNIEnv *env, jclass cls, jlong h, jbyteArray stations,
                                 jbyteArray f107, jbyteArray kp, jbyteArray xray)
{
    (void)cls;
    char *a = bytes_to_cstr(env, stations), *b = bytes_to_cstr(env, f107);
    char *c = bytes_to_cstr(env, kp), *d = bytes_to_cstr(env, xray);
    int r = ac_load_json(H(h), a, b, c, d);
    free(a); free(b); free(c); free(d);
    return r;
}

JNIEXPORT jint JNICALL FN(nLoadKpForecast)(JNIEnv *env, jclass cls, jlong h, jbyteArray json)
{
    (void)cls;
    char *s = bytes_to_cstr(env, json);
    int r = s ? ac_load_kp_forecast(H(h), s) : 0;
    free(s);
    return r;
}

JNIEXPORT void JNICALL FN(nSetLang)(JNIEnv *env, jclass cls, jlong h, jint lang)
{
    (void)env; (void)cls;
    ac_set_lang(H(h), lang);
}

JNIEXPORT void JNICALL FN(nSetMode)(JNIEnv *env, jclass cls, jlong h, jint mode)
{
    (void)env; (void)cls;
    ac_set_mode(H(h), mode);
}

/* a szöveg byte[]-ként jön (UTF-8), mert hívójelben bármi lehet */
JNIEXPORT jint JNICALL FN(nAddTarget)(JNIEnv *env, jclass cls, jlong h, jbyteArray input)
{
    (void)cls;
    char *s = bytes_to_cstr(env, input);
    int r = s ? ac_add_target(H(h), s) : -1;
    free(s);
    return r;
}

JNIEXPORT jint JNICALL FN(nSavePicked)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    return ac_save_picked(H(h));
}

JNIEXPORT jint JNICALL FN(nRemoveTarget)(JNIEnv *env, jclass cls, jlong h, jint idx)
{
    (void)env; (void)cls;
    return ac_remove_target(H(h), idx);
}

JNIEXPORT jint JNICALL FN(nToggleLp)(JNIEnv *env, jclass cls, jlong h, jint idx)
{
    (void)env; (void)cls;
    return ac_toggle_lp(H(h), idx);
}

JNIEXPORT jbyteArray JNICALL FN(nLastError)(JNIEnv *env, jclass cls, jlong h)
{
    (void)cls;
    return cstr_to_bytes(env, ac_last_error(H(h)));
}

JNIEXPORT void JNICALL FN(nSetTime)(JNIEnv *env, jclass cls, jlong h, jint hours)
{
    (void)env; (void)cls;
    ac_set_time_offset(H(h), hours);
}

JNIEXPORT void JNICALL FN(nSelect)(JNIEnv *env, jclass cls, jlong h, jint idx)
{
    (void)env; (void)cls;
    ac_select_target(H(h), idx);
}

JNIEXPORT jint JNICALL FN(nTargetFromMap)(JNIEnv *env, jclass cls, jlong h, jint w, jint hh,
                                          jint x, jint y)
{
    (void)env; (void)cls;
    return ac_target_from_map(H(h), w, hh, x, y);
}

JNIEXPORT void JNICALL FN(nRecompute)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    ac_recompute(H(h));
}

JNIEXPORT jbyteArray JNICALL FN(nState)(JNIEnv *env, jclass cls, jlong h)
{
    (void)cls;
    char *s = ac_state_json(H(h));
    jbyteArray r = cstr_to_bytes(env, s);
    free(s);
    return r;
}

JNIEXPORT jint JNICALL FN(nRenderMap)(JNIEnv *env, jclass cls, jlong h, jintArray out,
                                      jint w, jint hh, jint mode, jboolean spots)
{
    (void)cls;
    if (!out || w <= 0 || hh <= 0) return -1;
    if ((*env)->GetArrayLength(env, out) < w * hh) return -1;
    jint *px = (*env)->GetIntArrayElements(env, out, NULL);
    if (!px) return -1;
    int r = ac_render_map(H(h), (uint32_t *)px, w, hh, w, mode, spots ? 1 : 0, 1);
    (*env)->ReleaseIntArrayElements(env, out, px, 0);      /* 0 = visszamásolás */
    return r;
}

JNIEXPORT jint JNICALL FN(nPskStart)(JNIEnv *env, jclass cls, jlong h, jstring host, jint port)
{
    (void)cls;
    char *s = jstr(env, host);
    int r = ac_psk_start(H(h), s, port);
    free(s);
    return r;
}

JNIEXPORT void JNICALL FN(nPskStop)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    ac_psk_stop(H(h));
}

JNIEXPORT jint JNICALL FN(nPskPoll)(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    return ac_psk_poll(H(h));
}

JNIEXPORT jbyteArray JNICALL FN(nReport)(JNIEnv *env, jclass cls, jlong h, jint days)
{
    (void)cls;
    char *s = ac_report_text(H(h), days);
    jbyteArray r = cstr_to_bytes(env, s ? s : "");
    free(s);
    return r;
}

JNIEXPORT jint JNICALL FN(nSaveSuggestion)(JNIEnv *env, jclass cls, jlong h, jint days)
{
    (void)env; (void)cls;
    return ac_save_suggestion(H(h), days);
}

JNIEXPORT void JNICALL FN(nSetAlerts)(JNIEnv *env, jclass cls, jlong h, jboolean on)
{
    (void)env; (void)cls;
    ac_set_alerts(H(h), on ? 1 : 0);
}

JNIEXPORT jbyteArray JNICALL FN(nCheckAlerts)(JNIEnv *env, jclass cls, jlong h)
{
    (void)cls;
    char *s = ac_check_alerts(H(h));
    jbyteArray r = cstr_to_bytes(env, s ? s : "{}");
    free(s);
    return r;
}

JNIEXPORT void JNICALL FN(nSetUnits)(JNIEnv *env, jclass cls, jlong h, jboolean mi)
{
    (void)env; (void)cls;
    ac_set_units(H(h), mi ? 1 : 0);
}

JNIEXPORT jbyteArray JNICALL FN(nHelpText)(JNIEnv *env, jclass cls)
{
    (void)cls;
    return cstr_to_bytes(env, ac_help_text());
}
