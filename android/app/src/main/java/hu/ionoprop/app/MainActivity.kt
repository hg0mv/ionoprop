package hu.ionoprop.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputFilter
import android.text.InputType
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * IonoProp fő képernyő. Minden számítás a C-magban fut (Engine háttérszál),
 * itt csak a megjelenítés és a vezérlés van.
 */
class MainActivity : Activity() {

    private lateinit var engine: Engine
    private val ui = Handler(Looper.getMainLooper())
    private val prefs by lazy { getSharedPreferences("ionoprop", Context.MODE_PRIVATE) }

    // állapot
    private var state: AppState? = null
    private var mapMode = -2
    private var toff = 0
    private var pskEnabled = true
    private var resumed = false
    private var busy = false
    private var pendingUpdate = false
    private var lastFullUpdate = 0L
    private var lastSpotUpdate = 0L
    private var newSpots = 0
    private var locatorDialogShown = false
    private var statusMsg = ""

    // nézetek
    private lateinit var qthView: TextView
    private lateinit var timeView: TextView
    private lateinit var statusView: TextView
    private lateinit var map: MapPanel
    private lateinit var modeRow: LinearLayout
    private val modeButtons = ArrayList<Pair<Int, Button>>()
    private lateinit var seek: SeekBar
    private lateinit var seekLabel: TextView
    private lateinit var solarView: TextView
    private lateinit var layersView: TextView
    private lateinit var recView: TextView
    private lateinit var table: TargetTable
    private lateinit var fcTitle: TextView
    private lateinit var modeBtn: Button
    private lateinit var forecast: ForecastView
    private lateinit var pskSwitch: Switch
    private lateinit var alertSwitch: Switch

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        mapMode = prefs.getInt("mode", -2)
        pskEnabled = prefs.getBoolean("psk", true)
        // nyelv: a mentett, első indításkor a telefoné
        L.en = (prefs.getString("lang", null) ?: if (Locale.getDefault().language == "hu") "hu" else "en") == "en"
        engine = Engine(this)
        val alertsOn = prefs.getBoolean(BackgroundJob.PREF_ALERTS, false) && BackgroundJob.canNotify(this)
        // távolság: a mentett; első indításkor a telefon országa (USA, Egyesült Királyság: mérföld)
        L.mi = (prefs.getString("units", null)
            ?: if (Locale.getDefault().country in setOf("US", "GB", "LR", "MM")) "mi" else "km") == "mi"
        val mi = L.mi
        engine.post { engine.setLang(L.en); engine.setAlerts(alertsOn); engine.setUnits(mi) }
        enableEdgeToEdge()
        setContentView(buildUi())
        BackgroundJob.schedule(this)
        selectModeButton()
        statusMsg = L.t("Adatok letöltése…", "Downloading data…")
        updateStatus()
        // első betöltés: a gyorsítótárból vagy a netről
        busy = true
        engine.run({
            val n = engine.load(false)
            engine.recompute()
            n
        }) { n ->
            busy = false
            statusMsg = loadStatus(n)
            update(full = false)
        }
    }

    override fun onResume() {
        super.onResume()
        resumed = true
        if (pskEnabled) engine.post { engine.pskStart() }
        ui.removeCallbacks(tick)
        ui.postDelayed(tick, 5_000)
        if (state != null) update(full = true)
    }

    override fun onPause() {
        super.onPause()
        resumed = false
        ui.removeCallbacks(tick)
        // háttérben nem gyűjtünk (akkumulátor)
        engine.post { engine.pskStop() }
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacksAndMessages(null)
        engine.shutdown()
    }

    /** időzítő: 5 mp-enként spotok, percenként újraszámolás, 15 percenként letöltés */
    private val tick = object : Runnable {
        override fun run() {
            if (!resumed) return
            val now = System.currentTimeMillis()
            if (pskEnabled) {
                engine.run({ engine.pskPoll() }) { n ->
                    newSpots += n
                    // a spotok miatt legfeljebb 30 mp-enként frissítünk
                    if (newSpots > 0 && now - lastSpotUpdate > 30_000 && toff == 0) {
                        lastSpotUpdate = now
                        newSpots = 0
                        update(full = false, mapAlso = mapMode >= 0)
                    }
                }
            }
            val st = state
            if (pskEnabled && st != null && !st.pskConnected && !busy)
                update(full = false, mapAlso = false)     // csak az állapotsorhoz
            when {
                engine.lastLoadMs == 0L -> {}          // az első letöltés még tart
                now - engine.lastLoadMs >= 15 * 60_000 -> refresh(force = false)
                now - lastFullUpdate >= 60_000 -> update(full = true)
            }
            ui.postDelayed(this, 5_000)
        }
    }

    // ---------- frissítés ----------

    /**
     * full = true: újraszámolás (modell, célok, előrejelzés) is.
     * mapAlso: a térkép újrarajzolása.
     */
    private fun update(full: Boolean, mapAlso: Boolean = true) {
        if (busy) { pendingUpdate = true; return }
        busy = true
        map.busy = mapAlso
        val (w, h) = map.renderSize()
        val mode = mapMode
        val spots = pskEnabled
        val hours = toff
        engine.run({
            engine.setTime(hours)
            engine.recompute()
            val st = engine.state()
            val bmp = if (mapAlso) engine.renderMap(w, h, mode, spots) else null
            Pair(st, bmp)
        }) { (st, bmp) ->
            busy = false
            map.busy = false
            if (full || mapAlso) lastFullUpdate = System.currentTimeMillis()
            if (st != null) apply(st)
            if (bmp != null) map.bitmap = bmp
            if (pendingUpdate) {
                pendingUpdate = false
                update(full = true)
            }
        }
    }

    private fun refresh(force: Boolean) {
        statusMsg = L.t("Frissítés…", "Refreshing…")
        updateStatus()
        engine.run({ engine.load(force) }) { n ->
            statusMsg = loadStatus(n)
            update(full = true)
        }
    }

    private fun loadStatus(n: Int): String = when {
        n == 4 -> L.t("Adatok rendben", "Data OK")
        n > 0 -> L.t("Néhány forrás nem érhető el ($n/4)", "Some sources unavailable ($n/4)")
        else -> L.t("Nincs adat – nincs internet?", "No data – no internet?")
    }

    /** Nyelvváltás: a C-mag elmenti, a felület újraépül */
    private fun toggleLang() {
        L.en = !L.en
        prefs.edit().putString("lang", if (L.en) "en" else "hu").apply()
        engine.run({ engine.setLang(L.en) }) { recreate() }
    }

    private fun apply(s: AppState) {
        state = s
        qthView.text = "QTH: ${s.locator} ✎"
        updateStatus()
        solarView.text = solarText(s)
        layersView.text = layersText(s)
        recView.text = recText(s)
        table.state = s
        forecast.state = s
        map.mode = mapMode
        map.showSpotsLegend = pskEnabled && toff == 0
        modeBtn.text = s.mode
        val t = s.targets.getOrNull(s.sel)
        val fb = SpannableStringBuilder()
        when {
            t == null -> fb.add(L.t("24 órás előrejelzés", "24-hour forecast"), Colors.HEAD, true)
            t.nvis -> fb.add(L.t("24 órás előrejelzés: ${t.name}", "24-hour forecast: ${t.name}"), Colors.HEAD, true)
            else -> {
                fb.add(L.t("24 órás előrejelzés: ${t.name}", "24-hour forecast: ${t.name}"), Colors.HEAD, true)
                if (t.lp) fb.add(L.t(" – HOSSZÚ ÚT", " – LONG PATH"), Colors.WARN, true)
                fb.add(L.t(" (%.0f %s, %.0f°, %d ugrás)", " (%.0f %s, %.0f°, %d hops)").format(L.dist(t.dist), L.du(), t.bearing, t.hops), Colors.HEAD, true)
            }
        }
        // napkelte / napnyugta helyi idő szerint (a szürkevonalhoz)
        fun sunText(state: Int?, rise: Long, set: Long): String = when (state) {
            0 -> "${hhmm(rise)} / ${hhmm(set)}"
            1 -> L.t("egész nap fenn", "up all day")
            -1 -> L.t("egész nap lent", "down all day")
            else -> ""
        }
        fb.add(L.t("\n☀ fel / le:  QTH ${sunText(s.sunQthState, s.sunQthRise, s.sunQthSet)}", "\n☀ rise / set:  QTH ${sunText(s.sunQthState, s.sunQthRise, s.sunQthSet)}"), 0xFFFFC14D.toInt())
        if (t != null && !t.nvis && s.sunTgState != null)
            fb.add(L.t("   cél ${sunText(s.sunTgState, s.sunTgRise, s.sunTgSet)}", "   target ${sunText(s.sunTgState, s.sunTgRise, s.sunTgSet)}"), 0xFFFFC14D.toInt())
        fcTitle.text = fb
        if (toff == 0) BandWidget.render(this, s)
        if (s.firstStart && !locatorDialogShown) {
            locatorDialogShown = true
            showLocatorDialog(first = true)
        }
    }

    private fun updateStatus() {
        val s = state
        val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault())
        val utc = SimpleDateFormat("HH:mm", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }
        val t = Date(((s?.now ?: (System.currentTimeMillis() / 1000)) + toff * 3600L) * 1000L)
        val ts = "${fmt.format(t)} (${utc.format(t)} UTC)"
        timeView.text = if (toff == 0) ts else L.t("$ts  előrejelzés: +$toff óra", "$ts  forecast: +$toff h")
        timeView.setTextColor(if (toff == 0) Colors.TEXT else Colors.WARN)

        val sb = StringBuilder()
        val age = engine.dataAgeMinutes()
        if (age >= 0) sb.append(L.t("Adatok: $age perce – ", "Data: $age min old – "))
        sb.append(statusMsg)
        if (s != null && pskEnabled) {
            sb.append("\n")
            when {
                s.pskConnected -> sb.append(L.t("PSK Reporter: kapcsolódva – ${s.pskSpots30} spot / 30 perc", "PSK Reporter: connected – ${s.pskSpots30} spots / 30 min"))
                (s.pskStatus.contains("újrapróbálja") || s.pskStatus.contains("retrying")) -> sb.append(s.pskStatus)
                else -> sb.append(L.t("PSK Reporter: kapcsolódás…", "PSK Reporter: connecting…"))
            }
            if (!s.pskConnected && s.pskSpots30 > 0) sb.append(L.t(" (eddig ${s.pskSpots30} spot / 30 perc)", " (so far ${s.pskSpots30} spots / 30 min)"))
        }
        statusView.text = sb
    }

    // ---------- szövegek ----------

    private fun SpannableStringBuilder.add(s: String, color: Int, bold: Boolean = false): SpannableStringBuilder {
        val start = length
        append(s)
        setSpan(ForegroundColorSpan(color), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (bold) setSpan(StyleSpan(Typeface.BOLD), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        return this
    }

    private fun solarText(s: AppState): CharSequence {
        val b = SpannableStringBuilder()
        b.add(L.t("Napaktivitás\n", "Solar activity\n"), Colors.HEAD, true)
        if (!s.sfi.isNaN()) {
            val col = if (s.sfi < 80) Colors.BAD else if (s.sfi < 120) Colors.WARN else Colors.GOOD
            b.add("SFI ", Colors.DIM).add("%.0f".format(s.sfi), col, true)
            if (!s.sfi90.isNaN()) b.add(L.t("  (90 napos átlag: %.0f)", "  (90-day avg: %.0f)").format(s.sfi90), Colors.DIM)
        } else b.add(L.t("SFI: nincs adat", "SFI: no data"), Colors.DIM)
        b.append("\n")
        if (!s.kp.isNaN()) {
            val col = if (s.kp < 3) Colors.GOOD else if (s.kp < 5) Colors.WARN else Colors.BAD
            val txt = when {
                s.kp < 3 -> L.t("nyugodt", "quiet"); s.kp < 4 -> L.t("enyhén zavart", "unsettled"); s.kp < 5 -> L.t("zavart", "active")
                else -> L.t("geomágneses vihar", "geomagnetic storm")
            }
            b.add("Kp ", Colors.DIM).add("%.1f".format(s.kp), col, true).add("  $txt", Colors.DIM)
        } else b.add(L.t("Kp: nincs adat", "Kp: no data"), Colors.DIM)
        b.append("\n")
        val x = s.xray
        if (x != null && x.isNotEmpty()) {
            val col = when (x[0]) { 'X' -> Colors.BAD; 'M' -> Colors.WARN; else -> Colors.GOOD }
            val txt = when (x[0]) { 'X' -> L.t("erős napkitörés!", "strong solar flare!"); 'M' -> L.t("napkitörés", "solar flare"); else -> L.t("nyugodt", "quiet") }
            b.add(L.t("Röntgen ", "X-ray "), Colors.DIM).add(x, col, true).add("  $txt", Colors.DIM)
        } else b.add(L.t("Röntgen: nincs adat", "X-ray: no data"), Colors.DIM)
        return b
    }

    private fun layersText(s: AppState): CharSequence {
        val b = SpannableStringBuilder()
        b.add(L.t("Rétegek a QTH fölött", "Layers above QTH"), Colors.HEAD, true)
        b.add(if (toff > 0) L.t(" (előrejelzés)", " (forecast)") else "", Colors.WARN)
        b.add("  ${s.dayText}\n", Colors.DIM)
        b.add(if (s.cosx > 0.05) L.t("D:  van – elnyeli az alsó sávokat\n", "D:  present – absorbs low bands\n") else L.t("D:  nincs – alsó sávok nyitva\n", "D:  none – low bands open\n"), Colors.TEXT)
        b.add("E:  foE %.1f MHz\n".format(s.foe), Colors.TEXT)
        b.add("F2: foF2 %.2f MHz   hmF2 %.0f km   MUF %.1f\n".format(s.fof2, s.hmf2, s.mufd), Colors.TEXT)
        when {
            toff > 0 -> b.add(L.t("Es: előre nem jelezhető\n", "Es: cannot be forecast\n"), Colors.DIM)
            !s.esFoes.isNaN() -> b.add(L.t("Es: foEs %.1f MHz (%s, %d perce) – Es-MUF ~%.0f MHz\n", "Es: foEs %.1f MHz (%s, %d min ago) – Es-MUF ~%.0f MHz\n")
                    .format(s.esFoes, s.esStation, s.esAgeMin, s.esMuf),
                    if (s.esMuf >= 28.0) Colors.ES else Colors.TEXT)
            else -> b.add(L.t("Es: nincs friss foEs-mérés a közelben\n", "Es: no recent foEs measurement nearby\n"), Colors.DIM)
        }
        b.add(L.t("Alapja: ${s.nref} ionoszonda, mérési arány %.0f%%", "Based on: ${s.nref} ionosondes, measured share %.0f%%").format(s.conf * 100), Colors.DIM)
        if (s.tuned) b.add(L.t("  · hangolt paraméterek", "  · tuned parameters"), Colors.DIM)
        return b
    }

    private fun recText(s: AppState): CharSequence {
        val b = SpannableStringBuilder()
        b.add(L.t("Javaslat\n", "Suggestion\n"), Colors.HEAD, true)
        for ((i, r) in s.recommend.withIndex()) {
            b.add(r.group.padEnd(8), Colors.DIM)
            if (r.bands.isEmpty()) {
                if (r.weak != null) b.add("■ ", Colors.grade(1)).add(r.weak, Colors.TEXT, true)
                    .add(L.t("  gyenge, de próbálható", "  weak, worth a try"), Colors.DIM)
                else b.add(L.t("nincs használható sáv", "no usable band"), Colors.DIM)
            } else {
                for ((name, g) in r.bands) b.add("■ ", Colors.grade(g)).add("$name   ", Colors.TEXT, true)
            }
            b.append("\n")
        }
        // sporadikus E most: valódi vételekből, vagy a közeli ionoszonda szerint
        b.add(L.t("Es most", "Es now").padEnd(8), Colors.DIM)
        var shown = 0
        if (toff == 0) {
            for (bi in s.esPairs.indices.reversed()) {
                if (bi < 5 || shown >= 4) continue
                val n = s.esPairs[bi]
                if (n < s.esMinPairs) continue
                b.add("■ ", Colors.ES).add(s.bands.getOrElse(bi) { "" }, Colors.TEXT, true).add(" ($n)   ", Colors.DIM)
                shown++
            }
            if (shown == 0 && !s.esMuf.isNaN() && s.esMuf >= 28.0) {
                val best = when { s.esMuf >= 50.3 -> "6m"; s.esMuf >= 40.7 -> "8m"; else -> "10m" }
                b.add("■ ", Colors.ES).add(best, Colors.TEXT, true).add(L.t("  lehet (ionoszonda szerint)", "  possible (ionosonde)"), Colors.DIM)
                shown = 1
            }
        }
        if (shown == 0) b.add(when {
            toff > 0 -> L.t("előre nem jelezhető", "cannot be forecast")
            s.pskOn -> L.t("nincs jele", "no sign")
            else -> L.t("nincs jele (PSK Reporter ki)", "no sign (PSK Reporter off)")
        }, Colors.DIM)
        // meteorraj: az aktív vagy a következő, a rádiáns legjobb UTC-óráival
        if (s.meteorText.isNotEmpty()) {
            b.append("\n").add("Meteor".padEnd(8), Colors.DIM)
            if (s.meteorActive) b.add("■ ", Colors.METEOR).add(s.meteorText, Colors.TEXT, true)
            else b.add(s.meteorText, Colors.DIM)
        }
        return b
    }

    // ---------- párbeszédablakok ----------

    /** magyarázat a kijelzett értékekhez (a szöveg a C-magból jön, mint az asztali változatban) */
    private fun showHelp() {
        engine.run({ engine.helpText() }) { txt ->
            val b = SpannableStringBuilder()
            for (line in txt.split("\n")) {
                val i = line.indexOf(" - ")
                if (b.isNotEmpty()) b.append("\n\n")
                if (i > 0) b.add(line.substring(0, i), Colors.ACCENT, true).add("\n" + line.substring(i + 3), Colors.TEXT)
                else b.add(line, Colors.TEXT)
            }
            val tv = TextView(this).apply {
                text = b
                textSize = 14f
                setPadding(dpi(20f), dpi(8f), dpi(20f), dpi(8f))
            }
            AlertDialog.Builder(this)
                .setTitle(L.t("Mit jelentenek az értékek?", "What do the values mean?"))
                .setView(ScrollView(this).apply { addView(tv) })
                .setPositiveButton(L.t("Bezár", "Close"), null)
                .show()
        }
    }

    private fun showLocatorDialog(first: Boolean = false, error: String? = null) {
        val edit = EditText(this).apply {
            setText(state?.locator ?: "")
            setSelection(text.length)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            filters = arrayOf(InputFilter.LengthFilter(8))
            setSingleLine()
            textSize = 22f
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val p = dpi(20f)
            setPadding(p, dpi(8f), p, 0)
            addView(TextView(context).apply {
                text = (if (first) L.t("Üdv! Add meg a saját QTH lokátorodat.\n", "Welcome! Enter your QTH locator.\n") else "") +
                       L.t("Maidenhead lokátor: 4, 6 vagy 8 karakter, pl. KN07sp", "Maidenhead locator: 4, 6 or 8 characters, e.g. KN07sp")
            })
            addView(edit)
            if (error != null) addView(TextView(context).apply { text = error; setTextColor(Colors.BAD) })
        }
        AlertDialog.Builder(this)
            .setTitle(L.t("Saját QTH lokátor", "My QTH locator"))
            .setView(box)
            .setCancelable(!first)
            .setPositiveButton(L.t("Mentés", "Save")) { _, _ ->
                val loc = edit.text.toString().trim()
                engine.run({ engine.setLocator(loc) }) { ok ->
                    if (ok) {
                        Toast.makeText(this, L.t("QTH: $loc elmentve", "QTH: $loc saved"), Toast.LENGTH_SHORT).show()
                        update(full = true)
                    } else {
                        showLocatorDialog(first, L.t("Hibás lokátor: „$loc” (2 betű A-R, 2 szám, 2 betű A-X)", "Invalid locator: \"$loc\" (2 letters A-R, 2 digits, 2 letters A-X)"))
                    }
                }
            }
            .apply { if (!first) setNegativeButton(L.t("Mégse", "Cancel"), null) }
            .show()
    }

    private fun hhmm(utcSec: Long): String =
        SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(utcSec * 1000L))

    private fun showAddTargetDialog(error: String? = null, prefill: String = "") {
        val edit = EditText(this).apply {
            setText(prefill)
            setSelection(text.length)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            filters = arrayOf(InputFilter.LengthFilter(20))
            setSingleLine()
            textSize = 22f
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val p = dpi(20f)
            setPadding(p, dpi(8f), p, 0)
            addView(TextView(context).apply {
                text = L.t("Lokátor (pl. PM95), hívójel (pl. JA1ABC) vagy prefix (pl. VK6, ZS).\n", "Locator (e.g. PM95), callsign (e.g. JA1ABC) or prefix (e.g. VK6, ZS).\n") +
                       L.t("Legfeljebb 10 saját célterület lehet.", "Up to 10 custom targets.")
            })
            addView(edit)
            if (error != null) addView(TextView(context).apply { text = error; setTextColor(Colors.BAD) })
        }
        AlertDialog.Builder(this)
            .setTitle(L.t("Új célterület", "New target"))
            .setView(box)
            .setPositiveButton(L.t("Hozzáadás", "Add")) { _, _ ->
                val input = edit.text.toString().trim()
                engine.run({ engine.addTarget(input) }) { (idx, err) ->
                    if (idx >= 0) update(full = true, mapAlso = true)
                    else showAddTargetDialog(err, input)
                }
            }
            .setNegativeButton(L.t("Mégse", "Cancel"), null)
            .show()
    }

    /** hosszú nyomás egy célterületen: hosszú út, mentés, törlés */
    private fun showTargetMenu(idx: Int) {
        val t = state?.targets?.getOrNull(idx) ?: return
        val items = mutableListOf<Pair<String, () -> Unit>>()
        if (!t.nvis) items.add(Pair(if (t.lp) L.t("Rövid út", "Short path") else L.t("Hosszú út (long path)", "Long path")) {
            engine.run({ engine.toggleLp(idx); engine.select(idx) }) { update(full = true, mapAlso = true) }
        })
        if (t.picked) items.add(Pair(L.t("Mentés saját célterületként", "Save as custom target")) {
            engine.run({ engine.savePicked() }) { (i, err) ->
                if (i >= 0) update(full = true, mapAlso = true)
                else Toast.makeText(this, err ?: L.t("Hiba", "Error"), Toast.LENGTH_LONG).show()
            }
        })
        if (t.custom) items.add(Pair(L.t("Törlés", "Delete")) {
            AlertDialog.Builder(this)
                .setMessage(L.t("Törlöd ezt a célterületet: ${t.name}?", "Delete this target: ${t.name}?"))
                .setPositiveButton(L.t("Törlés", "Delete")) { _, _ ->
                    engine.run({ engine.removeTarget(idx) }) { update(full = true, mapAlso = true) }
                }
                .setNegativeButton(L.t("Mégse", "Cancel"), null)
                .show()
        })
        items.add(Pair(L.t("Új célterület…", "New target…")) { showAddTargetDialog() })
        AlertDialog.Builder(this)
            .setTitle(t.name)
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> items[which].second() }
            .show()
    }

    private fun showReport() {
        engine.run({ engine.report(7) }) { txt ->
            // a betűméretet a leghosszabb sorhoz igazítjuk, hogy ne kelljen oldalra görgetni
            val probe = android.graphics.Paint().apply {
                typeface = Typeface.MONOSPACE
                textSize = android.util.TypedValue.applyDimension(
                    android.util.TypedValue.COMPLEX_UNIT_SP, 11f, resources.displayMetrics)
            }
            val longest = txt.lines().maxOfOrNull { probe.measureText(it) } ?: 0f
            val avail = resources.displayMetrics.widthPixels * 0.86f - dpi(24f + 48f)
            val fit = if (longest > avail && longest > 0f) 11f * avail / longest else 11f
            val tv = TextView(this).apply {
                text = txt
                typeface = Typeface.MONOSPACE
                textSize = fit.coerceIn(8f, 11f)
                setTextColor(Colors.TEXT)
                setPadding(dpi(12f), dpi(8f), dpi(12f), dpi(8f))
            }
            val scroll = ScrollView(this).apply {
                addView(HorizontalScrollView(context).apply { addView(tv) })
            }
            AlertDialog.Builder(this)
                .setTitle(L.t("Ellenőrzés valódi vételekkel (7 nap)", "Check against real spots (7 days)"))
                .setView(scroll)
                .setPositiveButton(L.t("Bezár", "Close"), null)
                .setNeutralButton(L.t("Javaslat mentése", "Save suggestion")) { _, _ ->
                    engine.run({ engine.saveSuggestion(7) }) { r ->
                        val msg = when (r) {
                            0 -> L.t("A hangolt paraméterek elmentve", "Tuned parameters saved")
                            1 -> L.t("Még kevés az adat a javaslathoz", "Not enough data for a suggestion yet")
                            else -> L.t("Hiba: nem sikerült menteni", "Error: could not save")
                        }
                        Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
                        if (r == 0) update(full = true)
                    }
                }
                .show()
        }
    }

    // ---------- felület felépítése ----------

    /**
     * A tartalom a teljes kijelzőre rajzol (Android 15-től ez kötelező),
     * ezért minden verzión így kérjük, és a rendszersávok helyét
     * (állapotsor, navigációs sáv, kijelzőkivágás) paddinggel hagyjuk ki.
     */
    @Suppress("DEPRECATION")
    private fun enableEdgeToEdge() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
        } else {
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
        window.statusBarColor = Colors.BG_TOP
        window.navigationBarColor = Colors.BG_TOP
    }

    @Suppress("DEPRECATION")
    private fun applyInsets(v: View, insets: WindowInsets): WindowInsets {
        if (Build.VERSION.SDK_INT >= 30) {
            val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout()
                                     or WindowInsets.Type.ime())
            v.setPadding(i.left, i.top, i.right, i.bottom)
        } else {
            v.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                         insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
        }
        return insets
    }

    private fun panelBg(): GradientDrawable = GradientDrawable().apply {
        setColor(Colors.PANEL)
        setStroke(dpi(1f), Colors.BORDER)
        cornerRadius = dp(6f)
    }

    private fun smallButton(label: String, onClick: () -> Unit): Button = Button(this).apply {
        text = label
        isAllCaps = false
        textSize = 13f
        setTextColor(Colors.TEXT)
        background = GradientDrawable().apply {
            setColor(Colors.BUTTON); setStroke(dpi(1f), Colors.BORDER); cornerRadius = dp(4f)
        }
        minWidth = 0
        minimumWidth = 0
        minHeight = 0
        minimumHeight = 0
        setPadding(dpi(10f), dpi(6f), dpi(10f), dpi(6f))
        setOnClickListener { onClick() }
    }

    private fun lp(w: Int = ViewGroup.LayoutParams.MATCH_PARENT, h: Int = ViewGroup.LayoutParams.WRAP_CONTENT,
                   top: Float = 0f): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(w, h).apply { topMargin = dpi(top) }

    private fun panelText(): TextView = TextView(this).apply {
        setTextColor(Colors.TEXT)
        textSize = 13f
        setLineSpacing(0f, 1.15f)
        background = panelBg()
        setPadding(dpi(12f), dpi(8f), dpi(12f), dpi(8f))
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Colors.BG_TOP)        // a rendszersávok mögött
            setOnApplyWindowInsetsListener { v, insets -> applyInsets(v, insets) }
        }

        // fejléc
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(Colors.BG_TOP)
            setPadding(dpi(12f), dpi(8f), dpi(8f), dpi(8f))
        }
        header.addView(TextView(this).apply {
            text = "IonoProp"
            textSize = 18f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Colors.HEAD)
        })
        qthView = TextView(this).apply {
            text = "QTH: … ✎"
            textSize = 15f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Colors.ACCENT)
            setPadding(dpi(14f), dpi(6f), dpi(8f), dpi(6f))
            setOnClickListener { showLocatorDialog() }
        }
        header.addView(qthView, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        // üzemmód: koppintásra SSB -> CW -> FT8
        modeBtn = smallButton("SSB") {
            val next = ((state?.modeIdx ?: 0) + 1) % 3
            engine.run({ engine.setMode(next) }) { update(full = true, mapAlso = true) }
        }.apply { setTextColor(Colors.ACCENT); setTypeface(typeface, Typeface.BOLD) }
        header.addView(modeBtn, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { rightMargin = dpi(6f) })
        header.addView(smallButton("⟳") { refresh(force = true) })
        header.addView(smallButton("Stat") { showReport() }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { leftMargin = dpi(6f) })
        root.addView(header)

        // görgethető tartalom
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Colors.BG)
            setPadding(dpi(8f), dpi(6f), dpi(8f), dpi(16f))
        }
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            addView(content)
        }
        root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))

        timeView = TextView(this).apply { textSize = 13f; setTextColor(Colors.TEXT) }
        content.addView(timeView)
        statusView = TextView(this).apply { textSize = 11f; setTextColor(Colors.DIM) }
        content.addView(statusView)

        // térkép
        map = MapPanel(this, onTap = { bw, bh, x, y ->
            engine.run({ engine.targetFromMap(bw, bh, x, y) }) { idx ->
                if (idx >= 0) {
                    Toast.makeText(this, L.t("Új cél a térképről – lent az előrejelzése", "New target from the map – forecast below"), Toast.LENGTH_SHORT).show()
                    update(full = true)
                }
            }
        }, onSize = { if (state != null) update(full = false) })
        content.addView(map, lp(top = 6f))

        // módgombok
        modeRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val modes = listOf(-2 to "MUF", -1 to "foF2") + MapPanel.BAND_NAMES.mapIndexed { i, n -> i to n.removeSuffix("m") }
        for ((m, label) in modes) {
            val b = smallButton(label) {
                mapMode = m
                prefs.edit().putInt("mode", m).apply()
                selectModeButton()
                update(full = false)
            }
            modeButtons.add(m to b)
            modeRow.addView(b, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT).apply { rightMargin = dpi(4f) })
        }
        content.addView(HorizontalScrollView(this).apply {
            isHorizontalScrollBarEnabled = false
            addView(modeRow)
        }, lp(top = 6f))

        // időcsúszka
        val timeRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        timeRow.addView(smallButton("◀") { setToff(toff - 1) })
        seek = SeekBar(this).apply {
            max = 23
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                    if (fromUser) { toff = p; updateSeekLabel(); updateStatus() }
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) { setToff(sb.progress) }
            })
        }
        timeRow.addView(seek, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        timeRow.addView(smallButton("▶") { setToff(toff + 1) })
        seekLabel = TextView(this).apply {
            textSize = 13f
            setTextColor(Colors.TEXT)
            gravity = Gravity.CENTER
            minWidth = dpi(64f)
            setOnClickListener { setToff(0) }
        }
        timeRow.addView(seekLabel)
        content.addView(timeRow, lp(top = 4f))
        updateSeekLabel()

        // panelek
        solarView = panelText()
        content.addView(solarView, lp(top = 8f))
        layersView = panelText()
        content.addView(layersView, lp(top = 6f))
        recView = panelText()
        content.addView(recView, lp(top = 6f))

        // célterületek
        val tablePanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = panelBg()
            setPadding(dpi(6f), dpi(8f), dpi(6f), dpi(4f))
        }
        val tHead = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        tHead.addView(TextView(this).apply {
            text = L.t("Célterületek\nkoppintás: előrejelzés · hosszan: menü", "Targets\ntap: forecast · long press: menu")
            textSize = 12f
            setTextColor(Colors.HEAD)
            setPadding(dpi(6f), 0, 0, dpi(4f))
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        tHead.addView(smallButton(L.t("＋ Új cél", "＋ Target")) { showAddTargetDialog() })
        tablePanel.addView(tHead)
        table = TargetTable(this, { idx ->
            engine.run({ engine.select(idx) }) { update(full = true, mapAlso = true) }
        }, { idx -> showTargetMenu(idx) })
        tablePanel.addView(table)
        content.addView(tablePanel, lp(top = 6f))

        // 24 órás előrejelzés
        val fcPanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = panelBg()
            setPadding(dpi(8f), dpi(8f), dpi(8f), dpi(8f))
        }
        fcTitle = TextView(this).apply {
            textSize = 13f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Colors.HEAD)
        }
        fcPanel.addView(fcTitle)
        forecast = ForecastView(this) { h -> setToff(h) }
        fcPanel.addView(forecast, lp(top = 6f))
        content.addView(fcPanel, lp(top = 6f))

        // PSK Reporter kapcsoló
        pskSwitch = Switch(this).apply {
            text = L.t("Valódi vételek (PSK Reporter)", "Real spots (PSK Reporter)")
            setTextColor(Colors.TEXT)
            isChecked = pskEnabled
            setOnCheckedChangeListener { _, on ->
                pskEnabled = on
                prefs.edit().putBoolean("psk", on).apply()
                engine.post { if (on) engine.pskStart() else engine.pskStop() }
                update(full = false)
            }
        }
        content.addView(pskSwitch, lp(top = 10f))
        alertSwitch = Switch(this).apply {
            text = L.t("Értesítés sávnyitáskor (félóránként néz rá)", "Notify on band openings (checks every 30 min)")
            setTextColor(Colors.TEXT)
            isChecked = prefs.getBoolean(BackgroundJob.PREF_ALERTS, false) && BackgroundJob.canNotify(this@MainActivity)
            setOnCheckedChangeListener { _, on ->
                if (on && !BackgroundJob.canNotify(this@MainActivity) && Build.VERSION.SDK_INT >= 33) {
                    requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), REQ_NOTIF)
                } else setAlerts(on)
            }
        }
        content.addView(alertSwitch, lp(top = 4f))
        content.addView(TextView(this).apply {
            text = L.t("Sporadikus E 10/8/6 m-en, F2 nyitás 8/6 m-en, és ha a saját (★) célterületeid felé " +
                       "20–10 m között kinyílik egy sáv. Ugyanarra legfeljebb 3 óránként szól.",
                       "Sporadic E on 10/8/6 m, F2 openings on 8/6 m, and a band from 20 to 10 m opening " +
                       "towards your own (★) targets. At most once per 3 hours for the same opening.")
            textSize = 11f
            setTextColor(Colors.DIM)
        }, lp(top = 2f))
        content.addView(TextView(this).apply {
            text = L.t("Adatok: prop.kc2g.com (GIRO ionoszondák), NOAA SWPC, PSK Reporter.\n", "Data: prop.kc2g.com (GIRO ionosondes), NOAA SWPC, PSK Reporter.\n") +
                   L.t("A térképen koppintva új célt jelölhetsz ki.", "Tap the map to pick a new target.")
            textSize = 11f
            setTextColor(Colors.DIM)
        }, lp(top = 8f))
        // nyelvváltás
        // távolság mértékegysége
        content.addView(Switch(this).apply {
            text = L.t("Távolság mérföldben (mi)", "Distances in miles (mi)")
            setTextColor(Colors.TEXT)
            isChecked = L.mi
            setOnCheckedChangeListener { _, on ->
                L.mi = on
                prefs.edit().putString("units", if (on) "mi" else "km").apply()
                engine.run({ engine.setUnits(on) }) { update(full = false, mapAlso = false) }
            }
        }, lp(top = 10f))
        content.addView(smallButton(L.t("❓ Mit jelentenek az értékek?", "❓ What do the values mean?")) { showHelp() },
            lp(w = ViewGroup.LayoutParams.WRAP_CONTENT, top = 10f))
        content.addView(smallButton(L.t("🌐 English", "🌐 Magyar")) { toggleLang() }, lp(w = ViewGroup.LayoutParams.WRAP_CONTENT, top = 6f))
        return root
    }

    private fun setAlerts(on: Boolean) {
        prefs.edit().putBoolean(BackgroundJob.PREF_ALERTS, on).apply()
        engine.post { engine.setAlerts(on) }
        BackgroundJob.schedule(this)
        if (alertSwitch.isChecked != on) alertSwitch.isChecked = on
    }

    @Deprecated("Deprecated in Java")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        @Suppress("DEPRECATION")
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_NOTIF) return
        val ok = grantResults.isNotEmpty() && grantResults[0] == android.content.pm.PackageManager.PERMISSION_GRANTED
        setAlerts(ok)
        if (!ok) Toast.makeText(this, L.t("Értesítés nélkül nem tud szólni. Engedélyezd a beállításokban.",
            "Cannot notify without permission. Allow it in the settings."), Toast.LENGTH_LONG).show()
    }

    private fun selectModeButton() {
        for ((m, b) in modeButtons) {
            val on = m == mapMode
            b.setTextColor(if (on) 0xFF081018.toInt() else Colors.TEXT)
            (b.background as? GradientDrawable)?.setColor(if (on) Colors.ACCENT else Colors.BUTTON)
        }
    }

    private fun setToff(h: Int) {
        toff = h.coerceIn(0, 23)
        seek.progress = toff
        updateSeekLabel()
        updateStatus()
        update(full = true)
    }

    private fun updateSeekLabel() {
        seekLabel.text = if (toff == 0) L.t("Most", "Now") else L.t("+$toff óra", "+$toff h")
        seekLabel.setTextColor(if (toff == 0) Colors.TEXT else Colors.WARN)
    }

    companion object {
        private const val REQ_NOTIF = 7
    }
}
