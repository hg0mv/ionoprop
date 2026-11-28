package hu.ionoprop.app

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.job.JobInfo
import android.app.job.JobParameters
import android.app.job.JobScheduler
import android.app.job.JobService
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import java.util.Locale

/**
 * Háttérben fut félóránként (JobScheduler): letölti az adatokat,
 * újraszámol, frissíti a widgetet, és ha valami kinyílt (sporadikus E,
 * 6/8 m F2, saját célterület felé sáv), értesítést küld.
 * Csak akkor van ütemezve, ha a riasztás be van kapcsolva vagy van widget.
 */
class BackgroundJob : JobService() {

    override fun onStartJob(params: JobParameters): Boolean {
        val ctx = applicationContext
        val prefs = ctx.getSharedPreferences("ionoprop", Context.MODE_PRIVATE)
        L.en = langIsEnglish(ctx)
        val alertsOn = prefs.getBoolean(PREF_ALERTS, false)
        if (!alertsOn && !BandWidget.hasWidgets(ctx)) {
            schedule(ctx)                    // már nem kell: lemondja
            return false
        }
        val eng = Engine(ctx)
        val en = L.en
        eng.run({
            var st: AppState? = null
            var alerts: List<String> = emptyList()
            try {
                eng.setLang(en)
                eng.setAlerts(alertsOn)
                eng.load(false)
                eng.recompute()
                st = eng.state()
                if (alertsOn) alerts = eng.checkAlerts()
            } catch (t: Throwable) {
                android.util.Log.e(Engine.TAG, "háttér-ellenőrzés", t)
            }
            Pair(st, alerts)
        }) { (st, alerts) ->
            if (st != null) BandWidget.render(ctx, st)
            if (alerts.isNotEmpty()) notify(ctx, alerts)
            eng.shutdown()
            jobFinished(params, false)
        }
        return true
    }

    override fun onStopJob(params: JobParameters): Boolean = true

    companion object {
        const val PREF_ALERTS = "alerts"
        private const val JOB_PERIODIC = 1
        private const val JOB_NOW = 2
        private const val CHANNEL = "openings"
        private const val NOTIF_ID = 1

        fun langIsEnglish(ctx: Context): Boolean {
            val p = ctx.getSharedPreferences("ionoprop", Context.MODE_PRIVATE)
            return (p.getString("lang", null) ?: if (Locale.getDefault().language == "hu") "hu" else "en") == "en"
        }

        /** a félórás ellenőrzés be- vagy kikapcsolása a beállítások szerint */
        fun schedule(ctx: Context) {
            val js = ctx.getSystemService(JobScheduler::class.java) ?: return
            val prefs = ctx.getSharedPreferences("ionoprop", Context.MODE_PRIVATE)
            val need = prefs.getBoolean(PREF_ALERTS, false) || BandWidget.hasWidgets(ctx)
            if (!need) { js.cancel(JOB_PERIODIC); return }
            if (js.getPendingJob(JOB_PERIODIC) != null) return
            val info = JobInfo.Builder(JOB_PERIODIC, ComponentName(ctx, BackgroundJob::class.java))
                .setPeriodic(30 * 60_000L, 10 * 60_000L)
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                .setPersisted(true)
                .build()
            js.schedule(info)
        }

        /** egy ellenőrzés most (pl. új widget után) */
        fun runNow(ctx: Context) {
            val js = ctx.getSystemService(JobScheduler::class.java) ?: return
            val info = JobInfo.Builder(JOB_NOW, ComponentName(ctx, BackgroundJob::class.java))
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                .setOverrideDeadline(60_000L)
                .build()
            js.schedule(info)
        }

        fun canNotify(ctx: Context): Boolean =
            Build.VERSION.SDK_INT < 33 ||
                ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

        private fun notify(ctx: Context, lines: List<String>) {
            if (!canNotify(ctx)) return
            val nm = ctx.getSystemService(NotificationManager::class.java) ?: return
            if (Build.VERSION.SDK_INT >= 26) {
                nm.createNotificationChannel(NotificationChannel(
                    CHANNEL, L.t("Sávnyitás", "Band openings"), NotificationManager.IMPORTANCE_DEFAULT).apply {
                    description = L.t("Értesítés, ha egy sáv kinyílik", "Notify when a band opens")
                })
            }
            val open = PendingIntent.getActivity(ctx, 0,
                Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
            val text = lines.joinToString("\n")
            @Suppress("DEPRECATION")
            val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(ctx, CHANNEL) else Notification.Builder(ctx)
            b.setSmallIcon(R.drawable.ic_notif)
                .setContentTitle(L.t("Sávnyitás", "Band opening"))
                .setContentText(lines.first() + if (lines.size > 1) L.t(" (+${lines.size - 1})", " (+${lines.size - 1})") else "")
                .setStyle(Notification.BigTextStyle().bigText(text))
                .setColor(Colors.ES)
                .setContentIntent(open)
                .setAutoCancel(true)
            try {
                nm.notify(NOTIF_ID, b.build())
            } catch (e: SecurityException) {
                // az engedélyt közben visszavonták
            }
        }
    }
}
