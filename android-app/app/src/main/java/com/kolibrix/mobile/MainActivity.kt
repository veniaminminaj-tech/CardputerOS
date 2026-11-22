package com.kolibrix.mobile

import android.app.Activity
import android.os.Bundle
import android.graphics.*
import android.view.*
import kotlin.math.*

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
        setContentView(KXView())
    }

    override fun onBackPressed() {
        val v = window.decorView.findViewById<KXView>(android.R.id.content)?.getChildAt(0) as? KXView
        if (v?.closeTopLayer() != true) super.onBackPressed()
    }
}

private data class AppInfo(val name: String, val icon: String, val color: Int, val detail: String)

private class KXView : View {
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val apps = listOf(
        AppInfo("Settings", "⚙", Color.rgb(132,130,132), "Personalize your KolibriX mobile simulator."),
        AppInfo("Browser", "◎", Color.rgb(24,83,255), "A local browser demo. Internet browsing is not enabled in this prototype."),
        AppInfo("Shell", ">_", Color.rgb(0,105,0), "KolibriX Shell\n\n$ help\n  help     Show commands\n  about    System information\n  clear    Clear terminal\n\nKX:~$"),
        AppInfo("Telegram", "➤", Color.rgb(48,179,255), "Messaging app placeholder — no account or network connection."),
        AppInfo("Device", "▣", Color.rgb(0,255,132), "KolibriX Mobile Simulator\nPlatform: Android\nProfile: Touchscreen\nCore concept: KolibriOS-inspired"),
        AppInfo("Files", "▰", Color.rgb(0,125,255), "Internal storage (demo)\n\n📁 Documents\n📁 Downloads\n📁 Pictures\n📁 KolibriX")
    )
    private var startOpen = false
    private var active: AppInfo? = null
    private var cardputerMode = false
    private var toastText = ""
    private var toastUntil = 0L
    private var sx = 1f
    private var sy = 1f
    private var w = 360f
    private var h = 740f
    private val bgTop = Color.rgb(8, 35, 105)
    private val bgBottom = Color.rgb(31, 104, 219)

    constructor(context: android.content.Context) : super(context) {
        isFocusable = true
        isClickable = true
    }

    fun closeTopLayer(): Boolean {
        if (active != null) { active = null; invalidate(); return true }
        if (startOpen) { startOpen = false; invalidate(); return true }
        return false
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        w = width.toFloat().coerceAtLeast(1f)
        h = height.toFloat().coerceAtLeast(1f)
        sx = w / 360f
        sy = h / 740f
        canvas.save()
        canvas.scale(sx, sy)
        drawWallpaper(canvas)
        drawTopBar(canvas)
        if (cardputerMode) drawCardputer(canvas) else drawDesktop(canvas)
        drawDock(canvas)
        if (startOpen) drawStart(canvas)
        if (active != null) drawWindow(canvas, active!!)
        if (toastText.isNotEmpty() && System.currentTimeMillis() < toastUntil) {
            rounded(canvas, 50f, 690f, 260f, 32f, 16f, Color.argb(220,10,18,38))
            text(canvas, toastText, 180f, 711f, 12f, Color.WHITE, true, Paint.Align.CENTER)
            postInvalidateDelayed(300)
        }
        canvas.restore()
    }

    private fun drawWallpaper(c: Canvas) {
        p.shader = LinearGradient(0f, 0f, 360f, 740f, bgTop, bgBottom, Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, 360f, 740f, p)
        p.shader = null
        p.color = Color.argb(35, 110, 190, 255)
        c.drawCircle(180f, 345f, 220f, p)
        p.color = Color.argb(28, 180, 220, 255)
        c.drawCircle(180f, 345f, 165f, p)
        p.color = Color.argb(22, 210, 235, 255)
        c.drawCircle(180f, 345f, 112f, p)
        // Soft Windows-like light beam.
        p.shader = LinearGradient(0f, 230f, 360f, 520f,
            intArrayOf(Color.TRANSPARENT, Color.argb(45,120,190,255), Color.TRANSPARENT), null, Shader.TileMode.CLAMP)
        c.drawOval(25f, 250f, 335f, 485f, p)
        p.shader = null
    }

    private fun drawTopBar(c: Canvas) {
        rounded(c, 10f, 10f, 340f, 34f, 14f, Color.argb(150,15,27,58))
        text(c, "KX", 25f, 32f, 14f, Color.WHITE, true)
        text(c, "File   Edit   View", 63f, 31f, 11f, Color.WHITE)
        text(c, if (cardputerMode) "CARDPUTER" else "MOBILE", 275f, 31f, 9f, Color.rgb(190,220,255), true, Paint.Align.CENTER)
        text(c, "●  ▰", 329f, 31f, 10f, Color.WHITE, false, Paint.Align.RIGHT)
    }

    private fun drawDesktop(c: Canvas) {
        val positions = arrayOf(58f to 116f, 180f to 116f, 302f to 116f,
            58f to 252f, 180f to 252f, 302f to 252f)
        for (i in apps.indices) {
            val (x,y) = positions[i]
            drawAppIcon(c, apps[i], x, y, 46f)
            text(c, apps[i].name, x, y + 66f, 12f, Color.WHITE, true, Paint.Align.CENTER)
        }
        text(c, "KolibriX", 180f, 390f, 23f, Color.WHITE, true, Paint.Align.CENTER)
        text(c, "iOS × macOS × Windows 11", 180f, 413f, 11f, Color.argb(210,235,245,255), false, Paint.Align.CENTER)
    }

    private fun drawCardputer(c: Canvas) {
        // 320x240 device display preview scaled into the phone.
        rounded(c, 18f, 105f, 324f, 280f, 20f, Color.rgb(22,25,37))
        rounded(c, 27f, 114f, 306f, 262f, 8f, Color.rgb(8,45,120))
        text(c, "KolibriX • Cardputer ADV", 180f, 137f, 12f, Color.WHITE, true, Paint.Align.CENTER)
        val labels = arrayOf("Settings","Browser","Shell","Files","Device","Telegram")
        for (i in labels.indices) {
            val col = i % 3; val row = i / 3
            val x = 82f + col * 98f; val y = 194f + row * 85f
            rounded(c, x-17f, y-20f, 34f, 34f, 7f, apps[i].color)
            text(c, apps[i].icon, x, y+3f, 19f, Color.WHITE, true, Paint.Align.CENTER)
            text(c, labels[i], x, y+27f, 9f, Color.WHITE, false, Paint.Align.CENTER)
        }
        rounded(c, 94f, 342f, 172f, 20f, 8f, Color.rgb(35,53,96))
        text(c, "START     • • •      SD", 180f, 356f, 9f, Color.WHITE, true, Paint.Align.CENTER)
        text(c, "Tap an app below to open its demo", 180f, 421f, 12f, Color.WHITE, false, Paint.Align.CENTER)
        for (i in apps.indices) {
            val x = 36f + i*58f
            drawAppIcon(c, apps[i], x, 515f, 36f)
            text(c, apps[i].name, x, 548f, 9f, Color.WHITE, false, Paint.Align.CENTER)
        }
    }

    private fun drawDock(c: Canvas) {
        rounded(c, 48f, 640f, 264f, 58f, 20f, Color.argb(205,27,43,77))
        rounded(c, 49f, 641f, 262f, 56f, 19f, Color.argb(65,190,215,255))
        // Windows 11 Start glyph + macOS dock tiles with iOS-like rounded corners.
        rounded(c, 58f, 651f, 36f, 36f, 11f, Color.argb(100,255,255,255))
        drawWindowsMark(c, 76f, 669f, 4.4f)
        val picks = intArrayOf(0,1,2,5)
        for (i in picks.indices) drawAppIcon(c, apps[picks[i]], 117f + i*47f, 669f, 34f)
    }

    private fun drawStart(c: Canvas) {
        rounded(c, 34f, 425f, 292f, 202f, 22f, Color.argb(238,20,31,58))
        rounded(c, 35f, 426f, 290f, 200f, 21f, Color.argb(70,170,205,255))
        text(c, "Pinned", 54f, 451f, 14f, Color.WHITE, true)
        for (i in apps.indices) {
            val col = i % 3; val row = i / 3
            val x = 82f + col*98f; val y = 488f + row*60f
            drawAppIcon(c, apps[i], x, y, 28f)
            text(c, apps[i].name, x, y+24f, 9f, Color.WHITE, false, Paint.Align.CENTER)
        }
        text(c, "KolibriX     Power", 180f, 612f, 10f, Color.rgb(195,215,255), false, Paint.Align.CENTER)
    }

    private fun drawWindow(c: Canvas, app: AppInfo) {
        rounded(c, 15f, 88f, 330f, 525f, 22f, Color.argb(245,18,27,48))
        rounded(c, 16f, 89f, 328f, 523f, 21f, Color.argb(65,190,215,255))
        rounded(c, 25f, 99f, 310f, 48f, 13f, Color.argb(130,65,88,132))
        drawAppIcon(c, app, 48f, 123f, 28f)
        text(c, app.name, 73f, 127f, 15f, Color.WHITE, true)
        rounded(c, 302f, 108f, 25f, 27f, 8f, Color.rgb(190,62,76))
        text(c, "×", 314f, 128f, 18f, Color.WHITE, true, Paint.Align.CENTER)
        if (app.name == "Shell") {
            textBlock(c, app.detail, 38f, 180f, 14f, Color.rgb(115,255,155), 23f)
        } else {
            textBlock(c, app.detail, 39f, 188f, 14f, Color.WHITE, 24f)
            if (app.name == "Settings") {
                val rows = arrayOf("Appearance", "Wallpaper", "Display", "About KolibriX")
                for (i in rows.indices) {
                    rounded(c, 34f, 280f+i*55f, 292f, 43f, 12f, Color.argb(95,120,150,205))
                    text(c, rows[i], 51f, 307f+i*55f, 13f, Color.WHITE)
                    text(c, "›", 307f, 307f+i*55f, 18f, Color.WHITE, false, Paint.Align.CENTER)
                }
            } else if (app.name == "Files") {
                val rows = arrayOf("📁  Documents", "📁  Downloads", "📁  Pictures", "📁  KolibriX")
                for (i in rows.indices) text(c, rows[i], 52f, 290f+i*44f, 14f, Color.WHITE)
            } else if (app.name == "Browser") {
                rounded(c, 34f, 275f, 292f, 40f, 12f, Color.rgb(43,57,87))
                text(c, "Search or enter address", 48f, 300f, 11f, Color.LTGRAY)
            }
        }
        rounded(c, 100f, 570f, 160f, 28f, 14f, Color.rgb(43,63,103))
        text(c, "Close window", 180f, 589f, 11f, Color.WHITE, true, Paint.Align.CENTER)
    }

    private fun drawAppIcon(c: Canvas, app: AppInfo, x: Float, y: Float, size: Float) {
        val r = size * 0.29f
        rounded(c, x-size/2, y-size/2, size, size, r, app.color)
        rounded(c, x-size/2+1f, y-size/2+1f, size-2f, size-2f, r-1f, Color.argb(45,255,255,255))
        text(c, app.icon, x, y+size*0.18f, size*0.53f, Color.WHITE, true, Paint.Align.CENTER)
    }

    private fun drawWindowsMark(c: Canvas, x: Float, y: Float, s: Float) {
        p.color = Color.WHITE
        c.drawRect(x-s*2.3f,y-s*2.1f,x-s*0.3f,y-s*0.25f,p)
        c.drawRect(x+s*0.25f,y-s*2.1f,x+s*2.3f,y-s*0.25f,p)
        c.drawRect(x-s*2.3f,y+s*0.25f,x-s*0.3f,y+s*2.1f,p)
        c.drawRect(x+s*0.25f,y+s*0.25f,x+s*2.3f,y+s*2.1f,p)
    }

    private fun rounded(c: Canvas, x: Float, y: Float, ww: Float, hh: Float, r: Float, color: Int) {
        p.shader = null; p.color = color; p.style = Paint.Style.FILL
        c.drawRoundRect(x,y,x+ww,y+hh,r,r,p)
    }

    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float,
                     color: Int, bold: Boolean = false, align: Paint.Align = Paint.Align.LEFT) {
        p.shader = null; p.color = color; p.textSize = size; p.typeface =
            if (bold) Typeface.create("sans-serif-medium", Typeface.BOLD) else Typeface.create("sans-serif", Typeface.NORMAL)
        p.textAlign = align; p.style = Paint.Style.FILL
        c.drawText(value, x, y, p)
    }

    private fun textBlock(c: Canvas, value: String, x: Float, y: Float, size: Float, color: Int, line: Float) {
        var yy = y
        value.split("\n").forEach { part ->
            if (part.isEmpty()) yy += line * 0.55f else {
                var current = ""
                part.split(" ").forEach { word ->
                    val candidate = if (current.isEmpty()) word else "$current $word"
                    p.textSize = size
                    if (p.measureText(candidate) > 270f && current.isNotEmpty()) {
                        text(c,current,x,yy,size,color); yy += line; current = word
                    } else current = candidate
                }
                if (current.isNotEmpty()) { text(c,current,x,yy,size,color); yy += line }
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (event.action != MotionEvent.ACTION_UP) return true
        val x = event.x / sx
        val y = event.y / sy
        if (active != null) {
            if (x > 285f && y in 95f..155f || y > 560f && y < 610f) {
                active = null
            } else if (active!!.name == "Shell" && y > 200f) {
                toast("Shell demo: commands are shown in the window")
            }
            invalidate(); return true
        }
        if (startOpen) {
            if (y in 455f..585f) {
                val col = ((x - 35f) / 97f).toInt().coerceIn(0,2)
                val row = ((y - 458f) / 61f).toInt().coerceIn(0,1)
                active = apps[(row*3+col).coerceIn(0,5)]
            }
            startOpen = false; invalidate(); return true
        }
        if (y < 48f) {
            if (x > 225f) { cardputerMode = !cardputerMode; toast(if(cardputerMode) "Cardputer preview" else "Mobile desktop") }
            else { startOpen = true }
            invalidate(); return true
        }
        if (y > 635f) {
            if (x < 100f) { startOpen = !startOpen }
            else {
                val i = ((x - 100f) / 47f).toInt().coerceIn(0,3)
                active = apps[listOf(0,1,2,5)[i]]
            }
            invalidate(); return true
        }
        if (cardputerMode) {
            if (y in 485f..565f) {
                val i = ((x - 7f) / 58f).toInt().coerceIn(0,5); active = apps[i]
            } else if (y in 155f..330f) {
                val col = ((x-33f)/98f).toInt().coerceIn(0,2)
                val row = if(y<235f) 0 else 1
                active = apps[row*3+col]
            }
        } else if (y in 80f..330f) {
            val col = ((x+2f)/120f).toInt().coerceIn(0,2)
            val row = if (y < 190f) 0 else 1
            active = apps[row*3+col]
        }
        invalidate()
        return true
    }

    private fun toast(message: String) {
        toastText = message; toastUntil = System.currentTimeMillis() + 1800
    }
}
