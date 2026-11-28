package hu.ionoprop.app

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/**
 * A világtérkép. A képet a C-mag rajzolja (Engine.renderMap), ez a nézet
 * kiteszi, ráírja a jelmagyarázatot, és jelzi a koppintást.
 * Méretaránya fix: 360° x 140° (80°É - 60°D).
 */
@SuppressLint("ViewConstructor")
class MapPanel(context: Context, private val onTap: (bmpW: Int, bmpH: Int, x: Int, y: Int) -> Unit,
               private val onSize: () -> Unit) : View(context) {

    var bitmap: Bitmap? = null
        set(v) { field = v; invalidate() }
    var mode: Int = -2
        set(v) { field = v; invalidate() }
    var showSpotsLegend = false
        set(v) { field = v; invalidate() }
    var busy = false
        set(v) { field = v; invalidate() }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Colors.TEXT; textSize = context.sp(11f) }
    private val bold = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Colors.HEAD; textSize = context.sp(11f); typeface = Typeface.DEFAULT_BOLD
    }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val src = Rect()
    private val dst = Rect()
    private var downX = 0f
    private var downY = 0f

    /** a térkép rajzolásához ajánlott bitmap-méret (legfeljebb 1600 px széles) */
    fun renderSize(): Pair<Int, Int> {
        val w = min(width, 1600)
        return Pair(w, (w * 140.0 / 360.0).toInt())
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, (w * 140.0 / 360.0).toInt())
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w > 0 && w != oldw) onSize()
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Colors.BG)
        val b = bitmap
        if (b != null) {
            src.set(0, 0, b.width, b.height)
            dst.set(0, 0, width, height)
            canvas.drawBitmap(b, src, dst, paint)
        } else {
            canvas.drawText(L.t("Térkép számolása…", "Computing map…"), context.dp(16f), height / 2f, text)
        }
        drawLegend(canvas)
        if (busy) {
            val s = L.t("számolás…", "computing…")
            val w = text.measureText(s)
            fill.color = 0x99000000.toInt()
            canvas.drawRect(width - w - context.dp(16f), 0f, width.toFloat(), context.dp(22f), fill)
            canvas.drawText(s, width - w - context.dp(8f), context.dp(15f), text)
        }
    }

    private fun drawLegend(c: Canvas) {
        val pad = context.dp(6f)
        val lineH = context.sp(14f)
        val x0 = context.dp(6f)
        if (mode < 0) {
            if (showSpotsLegend && mode == -2) {
                // a MUF-térképen az Es-felhők (10 m-től felfelé) is látszanak
                val lbl = L.t("sporadikus E (10 m+)", "sporadic E (10 m+)")
                val lw = text.measureText(lbl) + context.dp(22f)
                fill.color = 0x99000000.toInt()
                c.drawRoundRect(RectF(x0, context.dp(4f), x0 + lw, context.dp(4f) + lineH + context.dp(6f)), pad, pad, fill)
                val yy = context.dp(4f) + lineH
                fill.color = 0x46D65CFF
                c.drawCircle(x0 + context.dp(9f), yy - context.dp(4f), context.dp(5.5f), fill)
                fill.color = Colors.ES
                c.drawCircle(x0 + context.dp(9f), yy - context.dp(4f), context.dp(2.2f), fill)
                c.drawText(lbl, x0 + context.dp(17f), yy, text)
            }
            val barW = min(context.dp(if (mode == -2) 200f else 170f), width * 0.5f)
            val boxH = lineH * 2 + context.dp(14f)
            val y0 = height - boxH - context.dp(4f)
            fill.color = 0x99000000.toInt()
            c.drawRoundRect(RectF(x0, y0, x0 + barW + pad * 2, y0 + boxH), pad, pad, fill)
            val maxV = if (mode == -2) 50.0 else 15.0
            val cMax = if (mode == -2) 50.0 else 35.0      // foF2: 15 MHz = a 35 MHz-es szín
            c.drawText(if (mode == -2) "MUF(3000), MHz" else "foF2, MHz", x0 + pad, y0 + lineH, bold)
            val by = y0 + lineH + context.dp(4f)
            val steps = barW.toInt().coerceAtLeast(1)
            for (i in 0 until steps) {
                fill.color = Colors.muf(i.toDouble() / steps * cMax)
                c.drawRect(x0 + pad + i, by, x0 + pad + i + 1.5f, by + context.dp(6f), fill)
            }
            val step = if (mode == -2) 5 else 3
            var v = 0
            while (v <= maxV.toInt()) {
                val x = x0 + pad + (v / maxV * barW).toFloat()
                val s = v.toString()
                c.drawText(s, x - text.measureText(s) / 2, by + context.dp(6f) + lineH * 0.9f, text)
                v += step
            }
        } else {
            val lbl = if (L.en) arrayOf("good", "fair", "weak") else arrayOf("jó", "közepes", "gyenge")
            val title = L.t("Hová jut el a jel: ", "Where the signal reaches: ") + BAND_NAMES.getOrElse(mode) { "?" }
            var w = bold.measureText(title)
            var row = 0f
            for (s in lbl) row += context.dp(16f) + text.measureText(s)
            if (row > w) w = row
            if (showSpotsLegend) {
                val sw = context.dp(29f) + text.measureText(L.t("valódi vétel (30 perc)", "real spot (30 min)")) + context.dp(13f) +
                        text.measureText(L.t("sporadikus E", "sporadic E"))
                if (sw > w) w = sw
            }
            val lines = if (showSpotsLegend) 3 else 2
            val boxH = lineH * lines + context.dp(8f)
            val y0 = height - boxH - context.dp(4f)
            fill.color = 0x99000000.toInt()
            c.drawRoundRect(RectF(x0, y0, x0 + w + pad * 2, y0 + boxH), pad, pad, fill)
            c.drawText(title, x0 + pad, y0 + lineH, bold)
            var x = x0 + pad
            val y = y0 + lineH * 2
            for ((i, s) in lbl.withIndex()) {
                fill.color = Colors.grade(3 - i)
                c.drawRect(x, y - context.dp(9f), x + context.dp(9f), y, fill)
                x += context.dp(12f)
                c.drawText(s, x, y, text)
                x += text.measureText(s) + context.dp(8f)
            }
            if (showSpotsLegend) {
                val yy = y0 + lineH * 3
                fill.color = 0xFF000000.toInt()
                c.drawCircle(x0 + pad + context.dp(4f), yy - context.dp(4f), context.dp(4f), fill)
                fill.color = 0xFFFFFFFF.toInt()
                c.drawCircle(x0 + pad + context.dp(4f), yy - context.dp(4f), context.dp(3f), fill)
                c.drawText(L.t("valódi vétel (30 perc)", "real spot (30 min)"), x0 + pad + context.dp(12f), yy, text)
                val ex = x0 + pad + context.dp(16f) + text.measureText(L.t("valódi vétel (30 perc)", "real spot (30 min)"))
                fill.color = 0x46D65CFF
                c.drawCircle(ex + context.dp(5f), yy - context.dp(4f), context.dp(5.5f), fill)
                fill.color = Colors.ES
                c.drawCircle(ex + context.dp(5f), yy - context.dp(4f), context.dp(2.2f), fill)
                c.drawText(L.t("sporadikus E", "sporadic E"), ex + context.dp(13f), yy, text)
            }
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.action) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; downY = e.y; return true }
            MotionEvent.ACTION_UP -> {
                val slop = context.dp(10f)
                if (abs(e.x - downX) < slop && abs(e.y - downY) < slop) {
                    val b = bitmap ?: return true
                    val bx = (e.x / width * b.width).toInt()
                    val by = (e.y / height * b.height).toInt()
                    onTap(b.width, b.height, bx, by)
                    performClick()
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

    companion object {
        val BAND_NAMES = arrayOf("160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "11m", "10m", "8m", "6m")
    }
}
