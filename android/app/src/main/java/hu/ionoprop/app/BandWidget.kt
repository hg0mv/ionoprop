package hu.ionoprop.app

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.widget.RemoteViews
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Kezdőképernyő-widget: napaktivitás, sávjavaslat (helyi / Európa / DX),
 * sporadikus E. A háttér-ellenőrzés (BackgroundJob) félóránként frissíti,
 * és az app is, amikor nyitva van. Koppintásra megnyílik az app.
 */
class BandWidget : AppWidgetProvider() {

    override fun onUpdate(ctx: Context, mgr: AppWidgetManager, ids: IntArray) {
        L.en = BackgroundJob.langIsEnglish(ctx)
        // amíg nincs adat: üres keret a megnyitó koppintással
        for (id in ids) mgr.updateAppWidget(id, views(ctx, null))
        BackgroundJob.schedule(ctx)
        BackgroundJob.runNow(ctx)
    }

    override fun onDisabled(ctx: Context) {
        BackgroundJob.schedule(ctx)          // ha a riasztás sincs bekapcsolva, leáll
    }

    companion object {
        fun hasWidgets(ctx: Context): Boolean = try {
            AppWidgetManager.getInstance(ctx)
                .getAppWidgetIds(ComponentName(ctx, BandWidget::class.java)).isNotEmpty()
        } catch (e: Exception) {
            false
        }

        fun render(ctx: Context, s: AppState) {
            if (!hasWidgets(ctx)) return
            val mgr = AppWidgetManager.getInstance(ctx)
            mgr.updateAppWidget(ComponentName(ctx, BandWidget::class.java), views(ctx, s))
        }

        private fun SpannableStringBuilder.add(t: String, color: Int, bold: Boolean = false): SpannableStringBuilder {
            val start = length
            append(t)
            setSpan(ForegroundColorSpan(color), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            if (bold) setSpan(StyleSpan(Typeface.BOLD), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            return this
        }

        private fun views(ctx: Context, s: AppState?): RemoteViews {
            val v = RemoteViews(ctx.packageName, R.layout.widget)
            val open = PendingIntent.getActivity(ctx, 0,
                Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            v.setOnClickPendingIntent(R.id.w_root, open)
            if (s == null) {
                v.setTextViewText(R.id.w_title, "IonoProp")
                v.setTextViewText(R.id.w_solar, "")
                v.setTextViewText(R.id.w_rec, L.t("Adatok letöltése…", "Downloading data…"))
                v.setTextViewText(R.id.w_time, "")
                return v
            }
            v.setTextViewText(R.id.w_title, SpannableStringBuilder()
                .add("IonoProp ", Colors.HEAD, true).add(s.locator, Colors.ACCENT, true)
                .add("  ${s.mode}", Colors.DIM))

            val sol = SpannableStringBuilder()
            if (!s.sfi.isNaN()) sol.add("SFI ", Colors.DIM)
                .add("%.0f".format(s.sfi), if (s.sfi < 80) Colors.BAD else if (s.sfi < 120) Colors.WARN else Colors.GOOD, true)
            if (!s.kp.isNaN()) sol.add("   Kp ", Colors.DIM)
                .add("%.1f".format(s.kp), if (s.kp < 3) Colors.GOOD else if (s.kp < 5) Colors.WARN else Colors.BAD, true)
            val x = s.xray
            if (!x.isNullOrEmpty()) sol.add("   ", Colors.DIM)
                .add(x, when (x[0]) { 'X' -> Colors.BAD; 'M' -> Colors.WARN; else -> Colors.GOOD }, true)
            v.setTextViewText(R.id.w_solar, sol)

            val b = SpannableStringBuilder()
            for (r in s.recommend) {
                if (b.isNotEmpty()) b.append("\n")
                b.add(r.group.padEnd(8), Colors.DIM)
                if (r.bands.isEmpty()) {
                    if (r.weak != null) b.add("■ ", Colors.grade(1)).add(r.weak, Colors.TEXT, true)
                    else b.add("–", Colors.DIM)
                } else for ((name, g) in r.bands.take(4)) b.add("■ ", Colors.grade(g)).add("$name  ", Colors.TEXT, true)
            }
            // sporadikus E: valódi vételek (ha az app gyűjti) vagy a közeli ionoszonda
            var es: String? = null
            for (bi in s.esPairs.indices.reversed()) {
                if (bi >= 5 && s.esPairs[bi] >= s.esMinPairs) { es = s.bands.getOrElse(bi) { "" }; break }
            }
            if (es == null && !s.esMuf.isNaN() && s.esMuf >= 28.0)
                es = when { s.esMuf >= 50.3 -> "6m"; s.esMuf >= 40.7 -> "8m"; else -> "10m" }
            if (es != null) b.append("\n").add("Es".padEnd(8), Colors.DIM).add("■ ", Colors.ES).add(es, Colors.TEXT, true)
            v.setTextViewText(R.id.w_rec, b)

            val t = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(s.now * 1000L))
            v.setTextViewText(R.id.w_time, L.t("frissítve $t", "updated $t"))
            return v
        }
    }
}
