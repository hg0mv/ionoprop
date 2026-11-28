package hu.ionoprop.app

import android.content.Context
import android.util.TypedValue

/** Színek és méret-segédek (ugyanaz a színvilág, mint az asztali programban) */
object Colors {
    const val BG = 0xFF0F151D.toInt()
    const val BG_TOP = 0xFF0B1017.toInt()
    const val PANEL = 0xFF161E28.toInt()
    const val BORDER = 0xFF2A3646.toInt()
    const val TEXT = 0xFFDFE6EE.toInt()
    const val DIM = 0xFF8494A7.toInt()
    const val HEAD = 0xFFFFFFFF.toInt()
    const val ACCENT = 0xFF4FB3FF.toInt()
    const val GOOD = 0xFF3CCF6E.toInt()
    const val WARN = 0xFFF2C94C.toInt()
    const val BAD = 0xFFE5533D.toInt()
    const val ES = 0xFFD65CFF.toInt()         // sporadikus E (lila)
    const val METEOR = 0xFF7FD6FF.toInt()     // meteorraj (világoskék)
    const val CELL_EMPTY = 0xFF1F2833.toInt()
    const val SELECTED = 0xFF24364A.toInt()
    const val BUTTON = 0xFF223040.toInt()

    private val GRADE = intArrayOf(0xFF2A3340.toInt(), 0xFFE07B39.toInt(), 0xFFF2C94C.toInt(), 0xFF3CCF6E.toInt())
    fun grade(g: Int): Int = GRADE[g.coerceIn(0, 3)]
    fun gradeCell(g: Int): Int = if (g <= 0) CELL_EMPTY else grade(g)

    private val MUF_STOP = doubleArrayOf(0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0)
    private val MUF_COL = intArrayOf(0x1b2a6b, 0x2463b5, 0x1fa3b8, 0x3cbf5a, 0xd8d43a, 0xf29a2e, 0xe0432b, 0xc01d6a,
                                     0x8a2bc2, 0xb98af0, 0xf4eeff)

    /** MUF-színskála 0..50 MHz (mint a C-oldali muf_color) */
    fun muf(v: Double): Int {
        if (v.isNaN() || v <= MUF_STOP[0]) return 0xFF000000.toInt() or MUF_COL[0]
        for (i in 1 until MUF_STOP.size) {
            if (v <= MUF_STOP[i]) {
                val t = (v - MUF_STOP[i - 1]) / (MUF_STOP[i] - MUF_STOP[i - 1])
                return mix(MUF_COL[i - 1], MUF_COL[i], t)
            }
        }
        return 0xFF000000.toInt() or MUF_COL.last()
    }

    private fun mix(a: Int, b: Int, t: Double): Int {
        fun ch(c: Int, s: Int) = (c shr s) and 0xFF
        val r = (ch(a, 16) * (1 - t) + ch(b, 16) * t + 0.5).toInt()
        val g = (ch(a, 8) * (1 - t) + ch(b, 8) * t + 0.5).toInt()
        val bl = (ch(a, 0) * (1 - t) + ch(b, 0) * t + 0.5).toInt()
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or bl
    }
}

fun Context.dp(v: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, resources.displayMetrics)
fun Context.sp(v: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, resources.displayMetrics)
fun Context.dpi(v: Float): Int = dp(v).toInt()
