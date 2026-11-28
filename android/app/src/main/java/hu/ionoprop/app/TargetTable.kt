package hu.ionoprop.app

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View

/**
 * Célterületek táblázata. Soronként: felül a név, alatta kisebben
 * "távolság · irány · MUF", jobbra sávonként egy színes cella.
 * Fehér pötty = erős valódi vétel, szürke karika = csak gyenge (FT8),
 * piros pont a név után = zavar (sarki fény / napkitörés).
 * Koppintás egy sorra: kijelölés (onSelect). Hosszú nyomás: menü (onLongPress).
 * ★ = saját célterület, (LP) = hosszú út.
 */
@SuppressLint("ViewConstructor")
class TargetTable(
    context: Context,
    private val onSelect: (Int) -> Unit,
    private val onLongPress: (Int) -> Unit = {}
) : View(context) {

    var state: AppState? = null
        set(v) { field = v; requestLayout(); invalidate() }

    private val rowH = context.dp(38f)
    private val headH = context.dp(20f)
    private val legendH = context.dp(24f)
    private val name = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Colors.TEXT; textSize = context.sp(13f) }
    private val nameBold = Paint(name).apply { typeface = Typeface.DEFAULT_BOLD; color = Colors.HEAD }
    private val sub = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Colors.DIM; textSize = context.sp(10.5f) }
    private val dim = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Colors.DIM; textSize = context.sp(10f) }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private var downY = 0f

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val n = state?.targets?.size ?: 0
        val h = headH + n * rowH + legendH
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), h.toInt())
    }

    private fun nBands(): Int = state?.bands?.size?.takeIf { it > 0 } ?: MapPanel.BAND_NAMES.size

    private fun cellW(): Float =
        minOf(context.dp(24f), (width - context.dp(110f)) / nBands()).coerceAtLeast(context.dp(12f))

    private fun ellipsize(s: String, p: Paint, maxW: Float): String {
        if (p.measureText(s) <= maxW) return s
        var t = s
        while (t.isNotEmpty() && p.measureText("$t…") > maxW) t = t.dropLast(1)
        return "$t…"
    }

    override fun onDraw(c: Canvas) {
        val s = state ?: return
        val cell = cellW()
        val nb = nBands()
        val bandX = width - cell * nb - context.dp(2f)
        val pad = context.dp(6f)
        val textMax = bandX - pad * 2

        // fejléc: sávok
        val hy = headH - context.dp(6f)
        for (b in 0 until nb) {
            val lbl = s.bands.getOrElse(b) { "" }.removeSuffix("m")
            c.drawText(lbl, bandX + b * cell + (cell - dim.measureText(lbl)) / 2, hy, dim)
        }

        for ((i, t) in s.targets.withIndex()) {
            val top = headH + i * rowH
            if (i == s.sel) {
                fill.color = Colors.SELECTED
                c.drawRect(0f, top, width.toFloat(), top + rowH, fill)
            }
            // név
            val p = if (i == s.sel) nameBold else name
            p.color = when {
                t.lp -> Colors.WARN
                t.custom -> Colors.ACCENT
                i == s.sel -> Colors.HEAD
                else -> Colors.TEXT
            }
            val warn = t.polar || t.flare
            val label = (if (t.custom) "★ " else "") + t.name + (if (t.lp) " (LP)" else "")
            val nm = ellipsize(label, p, textMax - if (warn) context.dp(12f) else 0f)
            val y1 = top + context.dp(16f)
            c.drawText(nm, pad, y1, p)
            if (warn) {
                fill.color = Colors.BAD
                c.drawCircle(pad + p.measureText(nm) + context.dp(7f), y1 - context.dp(4.5f), context.dp(3f), fill)
            }
            // alatta: távolság, irány, MUF
            val muf = if (t.muf.isNaN()) "-" else "%.1f".format(t.muf)
            val info = if (t.nvis) (if (L.mi) "0–190 mi · MUF $muf" else "0–300 km · MUF $muf")
                       else "%.0f %s · %.0f° · MUF %s".format(L.dist(t.dist), L.du(), t.bearing, muf)
            c.drawText(ellipsize(info, sub, textMax), pad, top + context.dp(31f), sub)

            // sávcellák
            for (b in 0 until nb) {
                val g = t.grades.getOrElse(b) { 0 }
                val x = bandX + b * cell
                fill.color = Colors.gradeCell(g)
                c.drawRect(x + 1, top + context.dp(4f), x + cell - 1, top + rowH - context.dp(4f), fill)
                val sp = t.spots.getOrElse(b) { 0 }
                val cx = x + cell / 2
                val cy = top + rowH / 2
                if (sp == 2) {
                    fill.color = 0xFF000000.toInt(); c.drawCircle(cx, cy, context.dp(3.8f), fill)
                    fill.color = 0xFFFFFFFF.toInt(); c.drawCircle(cx, cy, context.dp(2.8f), fill)
                } else if (sp == 1) {
                    fill.color = 0xFF9AA6B4.toInt(); c.drawCircle(cx, cy, context.dp(3.3f), fill)
                    fill.color = Colors.gradeCell(g); c.drawCircle(cx, cy, context.dp(1.9f), fill)
                }
            }
        }

        // jelmagyarázat
        val y = headH + s.targets.size * rowH + legendH * 0.72f
        var x = pad
        if (s.pskOn) {
            fill.color = 0xFFFFFFFF.toInt(); c.drawCircle(x + context.dp(4f), y - context.dp(4f), context.dp(2.8f), fill)
            x += context.dp(10f)
            c.drawText(L.t("erős vétel", "strong spot"), x, y, dim); x += dim.measureText(L.t("erős vétel", "strong spot")) + context.dp(10f)
            fill.color = 0xFF9AA6B4.toInt(); c.drawCircle(x + context.dp(4f), y - context.dp(4f), context.dp(3.3f), fill)
            fill.color = Colors.PANEL; c.drawCircle(x + context.dp(4f), y - context.dp(4f), context.dp(1.9f), fill)
            x += context.dp(10f)
            c.drawText(L.t("csak gyenge (FT8)", "weak only (FT8)"), x, y, dim); x += dim.measureText(L.t("csak gyenge (FT8)", "weak only (FT8)")) + context.dp(10f)
        }
        fill.color = Colors.BAD; c.drawCircle(x + context.dp(4f), y - context.dp(4f), context.dp(3f), fill)
        x += context.dp(10f)
        c.drawText(L.t("zavar", "disturbed"), x, y, dim)
    }

    private var longFired = false
    private val longRunnable = Runnable {
        val st = state ?: return@Runnable
        val idx = ((downY - headH) / rowH).toInt()
        if (downY >= headH && idx in st.targets.indices) {
            longFired = true
            performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
            onLongPress(idx)
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val s = state ?: return false
        when (e.action) {
            MotionEvent.ACTION_DOWN -> {
                downY = e.y
                longFired = false
                postDelayed(longRunnable, android.view.ViewConfiguration.getLongPressTimeout().toLong())
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (kotlin.math.abs(e.y - downY) > context.dp(10f)) removeCallbacks(longRunnable)
                return true
            }
            MotionEvent.ACTION_CANCEL -> { removeCallbacks(longRunnable); return true }
            MotionEvent.ACTION_UP -> {
                removeCallbacks(longRunnable)
                if (!longFired && kotlin.math.abs(e.y - downY) < context.dp(10f)) {
                    val idx = ((e.y - headH) / rowH).toInt()
                    if (e.y >= headH && idx in s.targets.indices) {
                        onSelect(idx)
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
