package hu.ionoprop.app

/**
 * A C-mag (libionoprop.so) függvényei. A megvalósítás: src/jni_bridge.c
 * Mindet ugyanarról a háttérszálról kell hívni (lásd Engine).
 * A nagy szövegek (JSON) UTF-8 byte tömbként mennek át.
 */
object Native {
    init {
        System.loadLibrary("ionoprop")
    }

    @JvmStatic external fun nInit(cacheDir: String, configDir: String): Long
    @JvmStatic external fun nFree(h: Long)
    @JvmStatic external fun nSetLocator(h: Long, loc: String): Int
    @JvmStatic external fun nLoad(h: Long, stations: ByteArray?, f107: ByteArray?, kp: ByteArray?, xray: ByteArray?): Int
    @JvmStatic external fun nLoadKpForecast(h: Long, json: ByteArray?): Int
    @JvmStatic external fun nSetMode(h: Long, mode: Int)
    @JvmStatic external fun nSetLang(h: Long, lang: Int)
    @JvmStatic external fun nSetAlerts(h: Long, on: Boolean)
    @JvmStatic external fun nSetUnits(h: Long, mi: Boolean)
    @JvmStatic external fun nHelpText(): ByteArray?
    @JvmStatic external fun nCheckAlerts(h: Long): ByteArray?
    @JvmStatic external fun nAddTarget(h: Long, input: ByteArray): Int
    @JvmStatic external fun nSavePicked(h: Long): Int
    @JvmStatic external fun nRemoveTarget(h: Long, idx: Int): Int
    @JvmStatic external fun nToggleLp(h: Long, idx: Int): Int
    @JvmStatic external fun nLastError(h: Long): ByteArray?
    @JvmStatic external fun nSetTime(h: Long, hours: Int)
    @JvmStatic external fun nSelect(h: Long, idx: Int)
    @JvmStatic external fun nTargetFromMap(h: Long, w: Int, hh: Int, x: Int, y: Int): Int
    @JvmStatic external fun nRecompute(h: Long)
    @JvmStatic external fun nState(h: Long): ByteArray?
    @JvmStatic external fun nRenderMap(h: Long, out: IntArray, w: Int, hh: Int, mode: Int, spots: Boolean): Int
    @JvmStatic external fun nPskStart(h: Long, host: String, port: Int): Int
    @JvmStatic external fun nPskStop(h: Long)
    @JvmStatic external fun nPskPoll(h: Long): Int
    @JvmStatic external fun nReport(h: Long, days: Int): ByteArray?
    @JvmStatic external fun nSaveSuggestion(h: Long, days: Int): Int
}
