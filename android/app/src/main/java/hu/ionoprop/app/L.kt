package hu.ionoprop.app

/**
 * Kétnyelvű felület: L.t("magyar", "English").
 * A nyelvet a C-mag menti (settings.conf: lang=hu/en); első indításkor
 * a telefon nyelve dönt.
 */
object L {
    @Volatile var en = false
    fun t(hu: String, en: String): String = if (this.en) en else hu

    /** távolság km-ben vagy mérföldben (settings.conf "units") */
    @Volatile var mi = false
    fun dist(km: Double): Double = if (mi) km * 0.621371 else km
    fun du(): String = if (mi) "mi" else "km"
}
