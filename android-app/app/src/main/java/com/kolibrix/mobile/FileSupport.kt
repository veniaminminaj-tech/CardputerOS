package com.kolibrix.mobile

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.pdf.PdfRenderer
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import android.text.method.ScrollingMovementMethod
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.ScrollView
import android.widget.TextView
import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import org.apache.commons.compress.archivers.sevenz.SevenZFile
import com.github.junrar.Archive
import java.io.*
import java.util.zip.ZipInputStream

/**
 * Safe, local document and archive handling for KolibriX Mobile.
 * Files are selected with Android's Storage Access Framework; no broad storage permission is needed.
 */
object FileSupport {
    private const val MAX_TEXT_BYTES = 2 * 1024 * 1024
    private const val MAX_FILES = 1000
    private const val MAX_EXTRACTED_BYTES = 256L * 1024L * 1024L

    fun open(activity: Activity, uri: Uri) {
        Thread {
            try {
                val name = displayName(activity, uri)
                val ext = name.substringAfterLast('.', "").lowercase()
                when (ext) {
                    "txt", "rtf" -> {
                        val raw = activity.contentResolver.openInputStream(uri)?.use { readLimited(it, MAX_TEXT_BYTES) }
                            ?: error("Не вдалося прочитати файл")
                        val content = if (ext == "rtf") stripRtf(raw) else raw
                        activity.runOnUiThread { showText(activity, name, content) }
                    }
                    "docx" -> {
                        val temp = copyToCache(activity, uri, name)
                        val content = readDocx(temp)
                        activity.runOnUiThread { showText(activity, name, content.ifBlank { "У документі не знайдено тексту." }) }
                    }
                    "pdf" -> {
                        val temp = copyToCache(activity, uri, name)
                        activity.runOnUiThread { showPdf(activity, name, temp) }
                    }
                    "zip", "rar", "7z" -> {
                        val temp = copyToCache(activity, uri, name)
                        val folder = extractArchive(activity, temp, ext, name)
                        activity.runOnUiThread {
                            AlertDialog.Builder(activity)
                                .setTitle("Kolibri Archive")
                                .setMessage("Архів: $name\n\nФайли розпаковано: ${folder.listFiles()?.size ?: 0}\nПапка: ${folder.absolutePath}\n\nВідкрийте «Файли» у KolibriX, щоб переглянути результат.")
                                .setPositiveButton("Готово", null)
                                .show()
                        }
                    }
                    else -> activity.runOnUiThread {
                        AlertDialog.Builder(activity).setTitle("Формат не підтримується")
                            .setMessage("Поки що KolibriX відкриває TXT, RTF, DOCX, PDF, ZIP, RAR та 7Z.\n\nВибраний файл: $name")
                            .setPositiveButton("OK", null).show()
                    }
                }
            } catch (e: Exception) {
                activity.runOnUiThread {
                    AlertDialog.Builder(activity).setTitle("Не вдалося відкрити файл")
                        .setMessage(e.message ?: e.javaClass.simpleName)
                        .setPositiveButton("OK", null).show()
                }
            }
        }.start()
    }

    private fun displayName(activity: Activity, uri: Uri): String {
        var name = "document"
        activity.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) name = it.getString(0) ?: name
        }
        return name
    }

    private fun copyToCache(activity: Activity, uri: Uri, name: String): File {
        val safe = name.replace(Regex("[^A-Za-z0-9._-]"), "_").take(100)
        val out = File(activity.cacheDir, "kx_${System.currentTimeMillis()}_$safe")
        activity.contentResolver.openInputStream(uri)?.use { input ->
            FileOutputStream(out).use { output -> input.copyTo(output) }
        } ?: error("Не вдалося відкрити файл")
        return out
    }

    private fun readLimited(input: InputStream, max: Int): String {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        var total = 0
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            total += n
            require(total <= max) { "Файл завеликий для попереднього перегляду (ліміт 2 МБ)." }
            out.write(buffer, 0, n)
        }
        return out.toString("UTF-8")
    }

    private fun readDocx(file: File): String {
        val xml = java.util.zip.ZipFile(file).use { zip ->
            val entry = zip.getEntry("word/document.xml") ?: error("У DOCX немає word/document.xml")
            zip.getInputStream(entry).bufferedReader(Charsets.UTF_8).use { it.readText() }
        }
        val parser = Xml.newPullParser()
        parser.setInput(StringReader(xml))
        val result = StringBuilder()
        var event = parser.eventType
        while (event != XmlPullParser.END_DOCUMENT) {
            if (event == XmlPullParser.START_TAG && parser.name == "t") {
                result.append(parser.nextText())
            } else if (event == XmlPullParser.END_TAG && parser.name == "p") {
                result.append('\n')
            }
            event = parser.next()
        }
        return result.toString()
    }

    private fun stripRtf(value: String): String {
        return value.replace(Regex("(?i)\\\\par[d]?"), "\n")
            .replace(Regex("\\\\'[0-9a-f]{2}"), " ")
            .replace(Regex("\\\\[a-z]+-?\\d* ?"), "")
            .replace(Regex("[{}]"), "")
            .replace(Regex("\\\\u-?\\d+\\??"), "")
    }

    private fun showText(activity: Activity, title: String, content: String) {
        val view = TextView(activity).apply {
            text = content
            textSize = 15f
            setTextColor(Color.rgb(30, 35, 45))
            setPadding(24, 20, 24, 20)
            movementMethod = ScrollingMovementMethod()
        }
        val scroll = ScrollView(activity).apply {
            addView(view, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        AlertDialog.Builder(activity).setTitle(title).setView(scroll)
            .setPositiveButton("Закрити", null).show()
    }

    private fun showPdf(activity: Activity, title: String, file: File) {
        val descriptor = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
        val renderer = PdfRenderer(descriptor)
        if (renderer.pageCount == 0) {
            renderer.close(); descriptor.close(); error("PDF не містить сторінок")
        }
        val page = renderer.openPage(0)
        val width = 900
        val height = (width.toFloat() * page.height / page.width).toInt().coerceAtLeast(1)
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(Color.WHITE)
        page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
        page.close()
        renderer.close()
        descriptor.close()
        val image = ImageView(activity).apply {
            setImageBitmap(bitmap)
            adjustViewBounds = true
            scaleType = ImageView.ScaleType.FIT_CENTER
        }
        val scroll = ScrollView(activity).apply { addView(image) }
        AlertDialog.Builder(activity).setTitle("$title • сторінка 1")
            .setView(scroll).setPositiveButton("Закрити", null).show()
    }

    private fun extractArchive(activity: Activity, file: File, ext: String, originalName: String): File {
        val base = originalName.substringBeforeLast('.', originalName)
            .replace(Regex("[^A-Za-z0-9._-]"), "_").take(60).ifBlank { "Archive" }
        val parent = File(activity.getExternalFilesDir(android.os.Environment.DIRECTORY_DOCUMENTS), "KolibriX/Extracted")
        val target = File(parent, base)
        parent.mkdirs()
        target.mkdirs()
        var count = 0
        var total = 0L

        fun outputFor(entryName: String): File {
            val normalized = entryName.replace('\\', '/')
            require(!normalized.startsWith("/") && !normalized.split('/').contains("..")) { "Небезпечний шлях у архіві" }
            val out = File(target, normalized)
            require(out.canonicalPath.startsWith(target.canonicalPath + File.separator) || out.canonicalPath == target.canonicalPath) {
                "Архів містить некоректний шлях"
            }
            out.parentFile?.mkdirs()
            return out
        }
        fun copyEntry(input: InputStream, out: File) {
            FileOutputStream(out).use { dest ->
                val buffer = ByteArray(8192)
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    total += n
                    require(total <= MAX_EXTRACTED_BYTES) { "Архів перевищує ліміт розпакування 256 МБ" }
                    dest.write(buffer, 0, n)
                }
            }
        }

        when (ext) {
            "zip" -> ZipInputStream(BufferedInputStream(FileInputStream(file))).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    count++; require(count <= MAX_FILES) { "В архіві забагато файлів (ліміт 1000)" }
                    val out = outputFor(entry.name)
                    if (entry.isDirectory) out.mkdirs() else copyEntry(zip, out)
                    zip.closeEntry()
                }
            }
            "rar" -> {
                val archive = Archive(file)
                try {
                    for (header in archive.fileHeaders) {
                        count++; require(count <= MAX_FILES) { "В архіві забагато файлів (ліміт 1000)" }
                        val out = outputFor(header.fileNameString)
                        if (header.isDirectory) out.mkdirs() else FileOutputStream(out).use { archive.extractFile(header, it) }
                        total += if (out.isFile) out.length() else 0
                        require(total <= MAX_EXTRACTED_BYTES) { "Архів перевищує ліміт розпакування 256 МБ" }
                    }
                } finally { archive.close() }
            }
            "7z" -> SevenZFile.builder().setFile(file).get().use { seven ->
                while (true) {
                    val entry = seven.nextEntry ?: break
                    count++; require(count <= MAX_FILES) { "В архіві забагато файлів (ліміт 1000)" }
                    val out = outputFor(entry.name)
                    if (entry.isDirectory) out.mkdirs() else {
                        FileOutputStream(out).use { dest ->
                            val buffer = ByteArray(8192)
                            while (true) {
                                val n = seven.read(buffer)
                                if (n < 0) break
                                total += n
                                require(total <= MAX_EXTRACTED_BYTES) { "Архів перевищує ліміт розпакування 256 МБ" }
                                dest.write(buffer, 0, n)
                            }
                        }
                    }
                }
            }
        }
        return target
    }
}
