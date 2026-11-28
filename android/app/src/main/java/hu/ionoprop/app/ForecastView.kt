package hu.ionoprop.app

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import java.util.Calendar

/**
 * 24 órás előrejelzés a kijelölt célra: sávok x órák, alatta MUF és LUF.
 * Koppintás egy oszlopra: ugrás arra az órára (onHour).
 */
@SuppressLint("ViewConstructor")
class ForecastView(context: Context, private val onHour: (Int) -> Unit) : View(context) {

    var state: AppState? = null
        set(v) { field = v; invalidate() }

    private val labelW = context.dp(38f)
    private val headH = context.dp(18f)
    private val cellH = context.dp(16f)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Colors.TEXT; textSize = context.sp(10f) }
    private val dim = Paint(text).apply { color = Colors.DIM }
    private val bold = Paint(text).apply { color = Colors.HEAD; typeface = Typeface.DEFAULT_BOLD }
    private val kpWarn = Paint(bold).apply { color = Colors.WARN }
    private val kpBad = Paint(bold).apply { color = Colors.BAD }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val frame = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; color = Colors.HEAD; strokeWidth = context.dp(1.5f)
    }
    private var downX = 0f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val h = headH + cellH * (nBands() + 3) + context.dp(6f)   // sávok + MUF + LUF + Kp
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), h.toInt())
    }

    private fun nBands(): Int = state?.bands?.size?.takeIf { it > 0 } ?: MapPanel.BAND_NAMES.size

    private fun cellW(): Float = (width - labelW - context.dp(4f)) / 24f

    override fun onDraw(c: Canvas) {
        val s = state ?: return
        if (s.fcGrades.isEmpty()) return
        val cw = cellW()
        val cal = Calendar.getInstance()
        // órák (helyi idő), keskeny kijelzőn csak minden másodikat írjuk ki
        val every = if (cw < text.measureText("88") + context.dp(2f)) 2 else 1
        for (h in 0 until 24) {
            cal.timeInMillis = (s.fcT0 + h * 3600L) * 1000L
            val hr = cal.get(Calendar.HOUR_OF_DAY)
            if (h % every != 0 && h != s.toff) continue
            val lbl = hr.toString()
            val p = if (h == s.toff) bold else dim
            c.drawText(lbl, labelW + h * cw + (cw - p.measureText(lbl)) / 2, headH - context.dp(5f), p)
        }
        // sávok: felül 6 m, alul 160 m
        val nb = nBands()
        for (row in 0 until nb) {
            val b = nb - 1 - row
            val y = headH + row * cellH
            c.drawText(s.bands.getOrElse(b) { "" }, 0f, y + cellH * 0.75f, text)
            for (h in 0 until 24) {
                val g = s.fcGrades.getOrNull(h)?.getOrElse(b) { 0 } ?: 0
                fill.color = Colors.gradeCell(g)
                c.drawRect(labelW + h * cw + 1, y + 1, labelW + (h + 1) * cw - 1, y + cellH - 1, fill)
            }
        }
        // MUF és LUF
        val yM = headH + nb * cellH + cellH * 0.8f
        val yL = yM + cellH
        c.drawText("MUF", 0f, yM, bold)
        c.drawText("LUF", 0f, yL, bold)
        for (h in 0 until 24) {
            val m = s.fcMuf.getOrElse(h) { Double.NaN }
            val l = s.fcLuf.getOrElse(h) { Double.NaN }
            val ms = if (m.isNaN()) "-" else "%.0f".format(m)
            val ls = if (l.isNaN() || l <= 0) "-" else "%.0f".format(l)
            c.drawText(ms, labelW + h * cw + (cw - text.measureText(ms)) / 2, yM, text)
            c.drawText(ls, labelW + h * cw + (cw - dim.measureText(ls)) / 2, yL, dim)
        }
        // Kp: a mért, később a NOAA előrejelzése; 4 fölött kiemelve
        val yK = yL + cellH
        c.drawText(if (s.fcKpForecast) "Kp" else "Kp*", 0f, yK, bold)
        for (h in 0 until 24) {
            val k = s.fcKp.getOrElse(h) { Double.NaN }
            val ks = if (k.isNaN()) "-" else "%.0f".format(k)
            val p = when {
                k.isNaN() || k < 4.0 -> dim
                k < 5.0 -> kpWarn
                else -> kpBad
            }
            c.drawText(ks, labelW + h * cw + (cw - p.measureText(ks)) / 2, yK, p)
        }
        // a kiválasztott óra kerete
        val x0 = labelW + s.toff * cw
        c.drawRect(x0, context.dp(2f), x0 + cw, yK + context.dp(4f), frame)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.action) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; return true }
            MotionEvent.ACTION_UP -> {
                if (kotlin.math.abs(e.x - downX) < context.dp(10f) && e.x >= labelW) {
                    val h = ((e.x - labelW) / cellW()).toInt()
                    if (h in 0 until 24) {
                        onHour(h)
                        performClick()
                    }
                }
                return true
            }
        }
        return super.onTouchEvent(e)
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }
}
