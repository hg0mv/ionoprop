package hu.ionoprop.app

import android.content.Context
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.io.File
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * A C-mag kezelése egyetlen háttérszálon. Minden munka sorban fut,
 * az eredmény a fő (UI) szálon érkezik vissza.
 * A szál közös: az app és a háttérben futó ellenőrzés (BackgroundJob)
 * egy-egy Engine-je ugyanazon a szálon fut, így a C-mag sosem fut
 * egyszerre két helyről.
 */
class Engine(context: Context) {
    private val exec: ExecutorService = EXEC
    private val main = Handler(Looper.getMainLooper())
    private val fetcher = Fetcher(File(context.cacheDir, "dl"))
    private var h = 0L
    @Volatile var lastLoadMs = 0L
        private set

    init {
        val cache = context.cacheDir.absolutePath
        val conf = context.filesDir.absolutePath
        exec.execute { h = Native.nInit(cache, conf) }
    }

    /** munka a háttérszálon, eredmény a UI szálon */
    fun <T> run(work: () -> T, done: (T) -> Unit) {
        exec.execute {
            val r = try {
                work()
            } catch (t: Throwable) {
                Log.e(TAG, "hiba a háttérszálon", t)
                null
            }
            if (r != null) main.post { done(r) }
        }
    }

    fun post(work: () -> Unit) {
        exec.execute {
            try {
                work()
            } catch (t: Throwable) {
                Log.e(TAG, "hiba a háttérszálon", t)
            }
        }
    }

    // ---- az alábbiakat csak a háttérszálról (run/post belsejéből) szabad hívni ----

    /** letöltés + betöltés; visszatérés: hány forrás sikerült (0..4) */
    fun load(force: Boolean): Int {
        val st = fetcher.get(Fetcher.URL_STATIONS, "kc2g_stations.json", Fetcher.CACHE_KC2G, force)
        val f107 = fetcher.get(Fetcher.URL_F107, "swpc_f107.json", Fetcher.CACHE_SWPC, force)
        val kp = fetcher.get(Fetcher.URL_KP, "swpc_kp.json", Fetcher.CACHE_SWPC, force)
        val xr = fetcher.get(Fetcher.URL_XRAY, "swpc_xray.json", Fetcher.CACHE_SWPC, force)
        val mask = Native.nLoad(h, st, f107, kp, xr)
        // Kp-előrejelzés: óránként elég frissíteni, és ha nincs, sem baj
        val errBefore = fetcher.lastError
        val kpf = fetcher.get(Fetcher.URL_KP_FORECAST, "swpc_kpf.json", Fetcher.CACHE_KPF, force)
        if (kpf != null) Native.nLoadKpForecast(h, kpf)
        fetcher.lastError = errBefore          // ennek a hibája nem "adathiba"
        lastLoadMs = System.currentTimeMillis()
        return Integer.bitCount(mask)
    }

    fun lastError(): String? = fetcher.lastError
    fun dataAgeMinutes(): Long = fetcher.ageMinutes("kc2g_stations.json")

    fun setLocator(loc: String): Boolean = Native.nSetLocator(h, loc) == 0
    fun setTime(hours: Int) = Native.nSetTime(h, hours)
    fun select(idx: Int) = Native.nSelect(h, idx)
    fun targetFromMap(w: Int, hh: Int, x: Int, y: Int): Int = Native.nTargetFromMap(h, w, hh, x, y)
    fun recompute() = Native.nRecompute(h)
    fun setMode(mode: Int) = Native.nSetMode(h, mode)
    fun setLang(en: Boolean) = Native.nSetLang(h, if (en) 1 else 0)
    fun setAlerts(on: Boolean) = Native.nSetAlerts(h, on)
    fun setUnits(mi: Boolean) = Native.nSetUnits(h, mi)
    /** a magyarázatok szövege (az aktuális nyelven) */
    fun helpText(): String = Native.nHelpText()?.let { String(it, Charsets.UTF_8) } ?: ""
    /** az új nyitások szövegei (az ac_recompute után) */
    fun checkAlerts(): List<String> {
        val b = Native.nCheckAlerts(h) ?: return emptyList()
        return try {
            val arr = org.json.JSONObject(String(b, Charsets.UTF_8)).optJSONArray("new") ?: return emptyList()
            (0 until arr.length()).map { arr.getJSONObject(it).optString("text") }.filter { it.isNotEmpty() }
        } catch (e: Exception) {
            emptyList()
        }
    }
    /** visszatérés: az új cél sorszáma, vagy null és a hiba szövege */
    fun addTarget(input: String): Pair<Int, String?> {
        val r = Native.nAddTarget(h, input.toByteArray(Charsets.UTF_8))
        return if (r >= 0) Pair(r, null) else Pair(-1, lastNativeError())
    }
    fun savePicked(): Pair<Int, String?> {
        val r = Native.nSavePicked(h)
        return if (r >= 0) Pair(r, null) else Pair(-1, lastNativeError())
    }
    fun removeTarget(idx: Int) = Native.nRemoveTarget(h, idx)
    fun toggleLp(idx: Int) = Native.nToggleLp(h, idx)
    private fun lastNativeError(): String =
        Native.nLastError(h)?.let { String(it, Charsets.UTF_8) } ?: L.t("Hiba", "Error")

    fun state(): AppState? {
        val b = Native.nState(h) ?: return null
        return AppState.parse(String(b, Charsets.UTF_8))
    }

    fun renderMap(w: Int, hh: Int, mode: Int, spots: Boolean): Bitmap? {
        if (w <= 0 || hh <= 0) return null
        val px = IntArray(w * hh)
        if (Native.nRenderMap(h, px, w, hh, mode, spots) != 0) return null
        val bmp = Bitmap.createBitmap(w, hh, Bitmap.Config.ARGB_8888)
        bmp.setPixels(px, 0, w, 0, 0, w, hh)
        return bmp
    }

    fun pskStart() = Native.nPskStart(h, PSK_HOST, PSK_PORT)
    fun pskStop() = Native.nPskStop(h)
    fun pskPoll(): Int = Native.nPskPoll(h)
    fun report(days: Int): String = Native.nReport(h, days)?.let { String(it, Charsets.UTF_8) } ?: ""
    fun saveSuggestion(days: Int): Int = Native.nSaveSuggestion(h, days)

    /** leállítás: a natív memória felszabadítása (a közös szál fut tovább) */
    fun shutdown() {
        exec.execute {
            if (h != 0L) Native.nFree(h)
            h = 0L
        }
    }

    companion object {
        const val TAG = "IonoProp"
        const val PSK_HOST = "mqtt.pskreporter.info"
        const val PSK_PORT = 1883
        /** a közös háttérszál */
        val EXEC: ExecutorService by lazy { Executors.newSingleThreadExecutor() }
    }
}
