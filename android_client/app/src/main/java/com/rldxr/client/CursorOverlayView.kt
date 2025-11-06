package com.rldxr.client

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.util.AttributeSet
import android.view.View

class CursorOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private var cursorX: Float = 0f
    private var cursorY: Float = 0f
    private var showCursor: Boolean = false
    
    private val cursorPaint = Paint().apply {
        color = Color.WHITE
        style = Paint.Style.FILL
        isAntiAlias = true
        strokeWidth = 2f
    }
    
    private val cursorOutlinePaint = Paint().apply {
        color = Color.BLACK
        style = Paint.Style.STROKE
        isAntiAlias = true
        strokeWidth = 1f
    }

    fun updateCursorPosition(x: Float, y: Float) {
        cursorX = x
        cursorY = y
        showCursor = true
        invalidate()  // Trigger redraw
    }

    fun hideCursor() {
        showCursor = false
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        
        if (showCursor) {
            // Draw a simple arrow cursor
            val cursorSize = 20f
            val path = Path().apply {
                moveTo(cursorX, cursorY)
                lineTo(cursorX, cursorY + cursorSize)
                lineTo(cursorX + cursorSize * 0.3f, cursorY + cursorSize * 0.7f)
                lineTo(cursorX + cursorSize * 0.7f, cursorY + cursorSize * 0.7f)
                close()
            }
            
            // Draw cursor with outline for visibility
            canvas.drawPath(path, cursorOutlinePaint)
            canvas.drawPath(path, cursorPaint)
        }
    }
}