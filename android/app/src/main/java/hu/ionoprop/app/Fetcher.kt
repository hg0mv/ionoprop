package hu.ionoprop.app

import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/**
 * Letöltés gyorsítótárral: ha a fájl még friss, nem tölti le újra
 * (a KC2G kéri, hogy ne terheljük a szerverét). Ha a letöltés nem sikerül,
 * a régi fájlt adja vissza. Háttérszálon kell hívni.
 */
class Fetcher(private val dir: File) {
    var lastError: String? = null

    init {
        dir.mkdirs()
    }

    fun get(url: String, name: String, maxAgeSec: Long, force: Boolean): ByteArray? {
        val f = File(dir, name)
        val ageSec = if (f.exists()) (System.currentTimeMillis() - f.lastModified()) / 1000 else -1
        if (!force && ageSec in 0 until maxAgeSec) return readOrNull(f)
        // a lassan változó adatot (KC2G, 15 perc) kézi frissítéskor sem kérjük le 5 percen belül
        if (force && maxAgeSec > FORCE_MIN_AGE && ageSec in 0 until FORCE_MIN_AGE) return readOrNull(f)
        // nemrég nem sikerült: egyre hosszabb szünet (1, 2, 4, 8, 15 perc)
        val now = System.currentTimeMillis()
        synchronized(backoff) {
            val b = backoff[name]
            if (b != null && now < b.second) return readOrNull(f)
        }

        try {
            val conn = URL(url).openConnection() as HttpURLConnection
            conn.connectTimeout = 10_000
            conn.readTimeout = 30_000
            conn.setRequestProperty("User-Agent", USER_AGENT)
            conn.instanceFollowRedirects = true
            try {
                val code = conn.responseCode
                if (code != 200) throw RuntimeException("HTTP $code")
                val bytes = conn.inputStream.use { it.readBytes() }
                if (bytes.isEmpty()) throw RuntimeException(L.t("üres válasz", "empty response"))
                val tmp = File(dir, "$name.part")
                tmp.writeBytes(bytes)
                if (!tmp.renameTo(f)) {
                    f.writeBytes(bytes)
                    tmp.delete()
                }
                synchronized(backoff) { backoff.remove(name) }
                return bytes
            } finally {
                conn.disconnect()
            }
        } catch (e: Exception) {
            lastError = "$name: ${e.message ?: e.javaClass.simpleName}"
            synchronized(backoff) {
                val fails = (backoff[name]?.first ?: 0) + 1
                val wait = minOf(60_000L shl minOf(fails - 1, 5), BACKOFF_MAX_MS)
                backoff[name] = Pair(fails, now + wait)
            }
        }
        return readOrNull(f)          // a régi adat is jobb a semminél
    }

    /** a legfrissebb fájl kora percben (-1, ha nincs) */
    fun ageMinutes(name: String): Long {
        val f = File(dir, name)
        return if (f.exists()) (System.currentTimeMillis() - f.lastModified()) / 60_000 else -1
    }

    private fun readOrNull(f: File): ByteArray? = try {
        if (f.exists()) f.readBytes() else null
    } catch (e: Exception) {
        null
    }

    companion object {
        const val USER_AGENT = "IonoProp-Android/0.9.1 (amateur radio propagation tool)"
        const val FORCE_MIN_AGE = 5 * 60L
        const val BACKOFF_MAX_MS = 15 * 60_000L
        /** fájlnév -> (sikertelen próbák száma, következő próba ideje); közös az app és a háttér között */
        private val backoff = HashMap<String, Pair<Int, Long>>()
        const val URL_STATIONS = "https://prop.kc2g.com/api/stations.json"
        const val URL_F107 = "https://services.swpc.noaa.gov/json/f107_cm_flux.json"
        const val URL_KP = "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json"
        const val URL_XRAY = "https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json"
        const val URL_KP_FORECAST = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json"
        const val CACHE_KC2G = 15 * 60L
        const val CACHE_SWPC = 5 * 60L
        const val CACHE_KPF = 60 * 60L
    }
}
