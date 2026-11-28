package hu.ionoprop.app

import org.json.JSONArray
import org.json.JSONObject

/** Egy célterület eredménye */
class TargetResult(
    val name: String,
    val nvis: Boolean,
    val dist: Double,
    val bearing: Double,
    val muf: Double,
    val luf: Double,
    val hops: Int,
    val polar: Boolean,
    val flare: Boolean,
    val conf: Double,
    val grades: IntArray,     // sávonként 0..3
    val spots: IntArray,      // sávonként 0 = nincs, 1 = gyenge, 2 = erős valódi vétel
    val custom: Boolean = false,   // saját (mentett) célterület
    val picked: Boolean = false,   // a térképen kijelölt pont
    val lp: Boolean = false        // hosszú út
)

/** Javaslat egy csoportra (Közeli / Európa / DX) */
class Recommendation(val group: String, val bands: List<Pair<String, Int>>, val weak: String?)

/** A C-mag teljes állapota (ac_state_json) */
class AppState(
    val locator: String,
    val firstStart: Boolean,
    val now: Long,
    val t: Long,
    val toff: Int,
    val loaded: Int,
    val nref: Int,
    val bands: List<String>,
    val sfi: Double,
    val sfi90: Double,
    val kp: Double,
    val xray: String?,
    val fof2: Double,
    val hmf2: Double,
    val mufd: Double,
    val foe: Double,
    val cosx: Double,
    val conf: Double,
    val dayText: String,
    val recommend: List<Recommendation>,
    val targets: List<TargetResult>,
    val sel: Int,
    val fcT0: Long,
    val fcGrades: Array<IntArray>,   // [óra][sáv]
    val fcMuf: DoubleArray,
    val fcLuf: DoubleArray,
    val fcKp: DoubleArray,
    val fcKpForecast: Boolean,
    // sporadikus E: közeli ionoszonda (foEs) és valódi vételek sávonként
    val esFoes: Double,
    val esStation: String,
    val esAgeMin: Int,
    val esMuf: Double,
    val esPairs: IntArray,
    val esMinPairs: Int,
    val mode: String,
    val modeIdx: Int,
    val lang: String,          // "hu" / "en"
    val langSaved: Boolean,
    val units: String,         // "km" / "mi"
    val meteorText: String,    // meteorraj: az aktív vagy a következő
    val meteorActive: Boolean,
    // napkelte / napnyugta (UTC másodperc); állapot: 0 = van, 1 = egész nap fenn, -1 = lent
    val sunQthState: Int,
    val sunQthRise: Long,
    val sunQthSet: Long,
    val sunTgState: Int?,
    val sunTgRise: Long,
    val sunTgSet: Long,
    val pskOn: Boolean,
    val pskConnected: Boolean,
    val pskStatus: String,
    val pskSpots30: Int,
    val tuned: Boolean
) {
    companion object {
        private fun ints(a: JSONArray?): IntArray =
            if (a == null) IntArray(0) else IntArray(a.length()) { a.optInt(it, 0) }

        private fun doubles(a: JSONArray?): DoubleArray =
            if (a == null) DoubleArray(0) else DoubleArray(a.length()) { a.optDouble(it, Double.NaN) }

        fun parse(json: String): AppState {
            val r = JSONObject(json)
            val so = r.optJSONObject("solar") ?: JSONObject()
            val ly = r.optJSONObject("layers") ?: JSONObject()
            val ps = r.optJSONObject("psk") ?: JSONObject()
            val fc = r.optJSONObject("forecast") ?: JSONObject()
            val es = r.optJSONObject("es") ?: JSONObject()
            val sun = r.optJSONObject("sun") ?: JSONObject()

            val bandArr = r.optJSONArray("bands")
            val bands = if (bandArr == null) emptyList() else List(bandArr.length()) { bandArr.optString(it) }

            val recArr = r.optJSONArray("recommend") ?: JSONArray()
            val recs = List(recArr.length()) { i ->
                val o = recArr.getJSONObject(i)
                val bl = o.optJSONArray("bands") ?: JSONArray()
                val list = List(bl.length()) { k ->
                    val e = bl.getJSONObject(k)
                    Pair(e.optString("name"), e.optInt("grade"))
                }
                val weak = if (o.has("weak") && !o.isNull("weak")) o.optString("weak") else null
                Recommendation(o.optString("group"), list, weak)
            }

            val ta = r.optJSONArray("targets") ?: JSONArray()
            val targets = List(ta.length()) { i ->
                val o = ta.getJSONObject(i)
                TargetResult(
                    o.optString("name"), o.optBoolean("nvis"),
                    o.optDouble("dist", Double.NaN), o.optDouble("bearing", Double.NaN),
                    o.optDouble("muf", Double.NaN), o.optDouble("luf", Double.NaN),
                    o.optInt("hops"), o.optBoolean("polar"), o.optBoolean("flare"),
                    o.optDouble("conf", 0.0),
                    ints(o.optJSONArray("grades")), ints(o.optJSONArray("spots")),
                    o.optBoolean("custom"), o.optBoolean("picked"), o.optBoolean("lp")
                )
            }

            val fg = fc.optJSONArray("grades") ?: JSONArray()
            val fcGrades = Array(fg.length()) { ints(fg.optJSONArray(it)) }

            val xray = if (so.has("xray") && !so.isNull("xray")) so.optString("xray") else null

            return AppState(
                locator = r.optString("locator"),
                firstStart = r.optBoolean("first_start"),
                now = r.optLong("now"),
                t = r.optLong("t"),
                toff = r.optInt("toff"),
                loaded = r.optInt("loaded"),
                nref = r.optInt("nref"),
                bands = bands,
                sfi = so.optDouble("sfi", Double.NaN),
                sfi90 = so.optDouble("sfi90", Double.NaN),
                kp = so.optDouble("kp", Double.NaN),
                xray = xray,
                fof2 = ly.optDouble("fof2", Double.NaN),
                hmf2 = ly.optDouble("hmf2", Double.NaN),
                mufd = ly.optDouble("mufd", Double.NaN),
                foe = ly.optDouble("foe", Double.NaN),
                cosx = ly.optDouble("cosx", 0.0),
                conf = ly.optDouble("conf", 0.0),
                dayText = ly.optString("day"),
                recommend = recs,
                targets = targets,
                sel = r.optInt("sel"),
                fcT0 = fc.optLong("t0"),
                fcGrades = fcGrades,
                fcMuf = doubles(fc.optJSONArray("muf")),
                fcLuf = doubles(fc.optJSONArray("luf")),
                fcKp = doubles(fc.optJSONArray("kp")),
                fcKpForecast = fc.optBoolean("kp_forecast"),
                esFoes = if (es.isNull("foes")) Double.NaN else es.optDouble("foes", Double.NaN),
                esStation = es.optString("station", ""),
                esAgeMin = es.optInt("age", 0),
                esMuf = if (es.isNull("muf")) Double.NaN else es.optDouble("muf", Double.NaN),
                esPairs = ints(es.optJSONArray("pairs")),
                esMinPairs = es.optInt("minpairs", 2),
                mode = r.optString("mode", "SSB"),
                modeIdx = r.optInt("mode_idx", 0),
                lang = r.optString("lang", "hu"),
                langSaved = r.optBoolean("lang_saved", false),
                units = r.optString("units", "km"),
                meteorText = r.optJSONObject("meteor")?.optString("text", "") ?: "",
                meteorActive = r.optJSONObject("meteor")?.optBoolean("active", false) ?: false,
                sunQthState = sun.optInt("qth_state", 0),
                sunQthRise = sun.optLong("qth_rise"),
                sunQthSet = sun.optLong("qth_set"),
                sunTgState = if (sun.has("tg_state")) sun.optInt("tg_state") else null,
                sunTgRise = sun.optLong("tg_rise"),
                sunTgSet = sun.optLong("tg_set"),
                pskOn = ps.optBoolean("on"),
                pskConnected = ps.optBoolean("connected"),
                pskStatus = ps.optString("status"),
                pskSpots30 = ps.optInt("spots30"),
                tuned = r.optBoolean("tuned")
            )
        }
    }
}
