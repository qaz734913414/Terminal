/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"

#include "gdirenderer.hpp"

#pragma hdrstop

using namespace Microsoft::Console::Render;

// Routine Description:
// - Prepares internal structures for a painting operation.
// Arguments:
// - <none>
// Return Value:
// - S_OK if we started to paint. S_FALSE if we didn't need to paint. HRESULT error code if painting didn't start successfully.
HRESULT GdiEngine::StartPaint()
{
    // If we have no handle, we don't need to paint. Return quickly.
    RETURN_HR_IF(S_FALSE, INVALID_HANDLE_VALUE == _hwndTargetWindow);

    // If we're already painting, we don't need to paint. Return quickly.
    RETURN_HR_IF(S_FALSE, _fPaintStarted);

    // Signal that we're starting to paint.
    _fPaintStarted = true;

    // At the beginning of a new frame, we have 0 lines ready for painting in PolyTextOut
    _cPolyText = 0;

    // Prepare our in-memory bitmap for double-buffered composition.
    RETURN_IF_FAILED(_PrepareMemoryBitmap(_hwndTargetWindow));

    // We must use Get and Release DC because BeginPaint/EndPaint can only be called in response to a WM_PAINT message (and may hang otherwise)
    // We'll still use the PAINTSTRUCT for information because it's convenient.
    _psInvalidData.hdc = GetDC(_hwndTargetWindow);
    RETURN_LAST_ERROR_IF_NULL(_psInvalidData.hdc);

    _psInvalidData.fErase = TRUE;
    _psInvalidData.rcPaint = _rcInvalid;

    return S_OK;
}

// Routine Description:
// - Scrolls the existing data on the in-memory frame by the scroll region deltas we have collectively received
//   through the Invalidate methods since the last time this was called.
// Arguments:
// - <none>
// Return Value:
// - S_OK, suitable GDI HRESULT error, error from Win32 windowing, or safemath error.
HRESULT GdiEngine::ScrollFrame()
{
    // If we don't have any scrolling to do, return early.
    RETURN_HR_IF(S_OK, 0 == _szInvalidScroll.cx && 0 == _szInvalidScroll.cy);

    RECT rcUpdate = { 0 };

    // We have to limit the region that can be scrolled to not include the gutters.
    // Gutters are defined as sub-character width pixels at the bottom or right of the screen.
    COORD const coordFontSize = _GetFontSize();
    RETURN_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_STATE), coordFontSize.X == 0 || coordFontSize.Y == 0);

    SIZE szGutter;
    szGutter.cx = _szMemorySurface.cx % coordFontSize.X;
    szGutter.cy = _szMemorySurface.cy % coordFontSize.Y;

    RECT rcScrollLimit = { 0 };
    RETURN_IF_FAILED(LongSub(_szMemorySurface.cx, szGutter.cx, &rcScrollLimit.right));
    RETURN_IF_FAILED(LongSub(_szMemorySurface.cy, szGutter.cy, &rcScrollLimit.bottom));

    // Scroll real window and memory buffer in-sync.
    LOG_LAST_ERROR_IF_FALSE(ScrollWindowEx(_hwndTargetWindow,
                                           _szInvalidScroll.cx,
                                           _szInvalidScroll.cy,
                                           &rcScrollLimit,
                                           &rcScrollLimit,
                                           nullptr,
                                           nullptr,
                                           0));

    LOG_LAST_ERROR_IF_FALSE(ScrollDC(_hdcMemoryContext, _szInvalidScroll.cx, _szInvalidScroll.cy, &rcScrollLimit, &rcScrollLimit, nullptr, &rcUpdate));

    _InvalidCombine(&rcUpdate);

    // update invalid rect for the remainder of paint functions
    _psInvalidData.rcPaint = _rcInvalid;

    return S_OK;
}

// Routine Description:
// - BeginPaint helper to prepare the in-memory bitmap for double-buffering
// Arguments:
// - hwnd - Window handle to use for the DC properties when creating a memory DC and for checking the client area size.
// Return Value:
// - S_OK or suitable GDI HRESULT error.
HRESULT GdiEngine::_PrepareMemoryBitmap(_In_ HWND const hwnd)
{
    RECT rcClient;
    RETURN_LAST_ERROR_IF_FALSE(GetClientRect(hwnd, &rcClient));

    SIZE const szClient = _GetRectSize(&rcClient);

    // Only do work if the existing memory surface is a different size from the client area.
    // Return quickly if they're the same.
    RETURN_HR_IF(S_OK, _szMemorySurface.cx == szClient.cx && _szMemorySurface.cy == szClient.cy);

    wil::unique_hdc hdcRealWindow(GetDC(_hwndTargetWindow));
    RETURN_LAST_ERROR_IF_NULL(hdcRealWindow.get());

    // If we already had a bitmap, Blt the old one onto the new one and clean up the old one.
    if (nullptr != _hbitmapMemorySurface)
    {
        // Make a temporary DC for us to Blt with.
        wil::unique_hdc hdcTemp(CreateCompatibleDC(hdcRealWindow.get()));
        RETURN_LAST_ERROR_IF_NULL(hdcTemp.get());

        // Make the new bitmap we'll use going forward with the new size.
        wil::unique_hbitmap hbitmapNew(CreateCompatibleBitmap(hdcRealWindow.get(), szClient.cx, szClient.cy));
        RETURN_LAST_ERROR_IF_NULL(hbitmapNew.get());

        // Select it into the DC, but hold onto the junky one pixel bitmap (made by default) to give back when we need to Delete.
        wil::unique_hbitmap hbitmapOnePixelJunk(SelectBitmap(hdcTemp.get(), hbitmapNew.get()));
        RETURN_LAST_ERROR_IF_NULL(hbitmapOnePixelJunk.get());
        hbitmapNew.release(); // if SelectBitmap worked, GDI took ownership. Detach from smart object.

        // Blt from the DC/bitmap we're already holding onto into the new one.
        RETURN_LAST_ERROR_IF_FALSE(BitBlt(hdcTemp.get(), 0, 0, _szMemorySurface.cx, _szMemorySurface.cy, _hdcMemoryContext, 0, 0, SRCCOPY));

        // Put the junky bitmap back into the temp DC and get our new one out.
        hbitmapNew.reset(SelectBitmap(hdcTemp.get(), hbitmapOnePixelJunk.get()));
        RETURN_LAST_ERROR_IF_NULL(hbitmapNew.get());
        hbitmapOnePixelJunk.release(); // if SelectBitmap worked, GDI took ownership. Detach from smart object.

        // Move our new bitmap into the long-standing DC we're holding onto.
        wil::unique_hbitmap hbitmapOld(SelectBitmap(_hdcMemoryContext, hbitmapNew.get()));
        RETURN_LAST_ERROR_IF_NULL(hbitmapOld.get());

        // Now save a pointer to our new bitmap into the class state.
        _hbitmapMemorySurface = hbitmapNew.release(); // and prevent it from being freed now that GDI is holding onto it as well.
    }
    else
    {
        _hbitmapMemorySurface = CreateCompatibleBitmap(hdcRealWindow.get(), szClient.cx, szClient.cy);
        RETURN_LAST_ERROR_IF_NULL(_hbitmapMemorySurface);

        wil::unique_hbitmap hOldBitmap(SelectBitmap(_hdcMemoryContext, _hbitmapMemorySurface)); // DC has a default junk bitmap, take it and delete it.
        RETURN_LAST_ERROR_IF_NULL(hOldBitmap.get());
    }

    // Save the new client size.
    _szMemorySurface = szClient;

    return S_OK;
}

// Routine Description:
// - EndPaint helper to perform the final BitBlt copy from the memory bitmap onto the final window bitmap (double-buffering.) Also cleans up structures used while painting.
// Arguments:
// - <none>
// Return Value:
// - S_OK or suitable GDI HRESULT error.
HRESULT GdiEngine::EndPaint()
{
    // If we try to end a paint that wasn't started, it's invalid. Return.
    RETURN_HR_IF_FALSE(HRESULT_FROM_WIN32(ERROR_INVALID_STATE), _fPaintStarted);

    LOG_IF_FAILED(_FlushBufferLines());

    POINT const pt = _GetInvalidRectPoint();
    SIZE const sz = _GetInvalidRectSize();

    LOG_LAST_ERROR_IF_FALSE(BitBlt(_psInvalidData.hdc, pt.x, pt.y, sz.cx, sz.cy, _hdcMemoryContext, pt.x, pt.y, SRCCOPY));

    _rcInvalid = { 0 };
    _fInvalidRectUsed = false;
    _szInvalidScroll = { 0 };

    LOG_LAST_ERROR_IF_FALSE(GdiFlush());
    LOG_LAST_ERROR_IF_FALSE(ReleaseDC(_hwndTargetWindow, _psInvalidData.hdc));
    _psInvalidData.hdc = nullptr;

    _fPaintStarted = false;

    return S_OK;
}

// Routine Description:
// - Fills the given rectangle with the background color on the drawing context.
// Arguments:
// - prc - Rectangle to fill with color
// Return Value:
// - S_OK or suitable GDI HRESULT error.
HRESULT GdiEngine::_PaintBackgroundColor(_In_ const RECT* const prc)
{
    wil::unique_hbrush hbr(GetStockBrush(DC_BRUSH));
    RETURN_LAST_ERROR_IF_NULL(hbr.get());

    WHEN_DBG(_PaintDebugRect(prc));

    LOG_LAST_ERROR_IF_FALSE(FillRect(_hdcMemoryContext, prc, hbr.get()));

    WHEN_DBG(_DoDebugBlt(prc));

    return S_OK;
}

// Routine Description:
// - Paints the background of the invalid area of the frame.
// Arguments:
// - <none>
// Return Value:
// - S_OK or suitable GDI HRESULT error.
HRESULT GdiEngine::PaintBackground()
{
    if (_psInvalidData.fErase)
    {
        RETURN_IF_FAILED(_PaintBackgroundColor(&_psInvalidData.rcPaint));
    }

    return S_OK;
}

// Routine Description:
// - Draws one line of the buffer to the screen.
// - This will now be cached in a PolyText buffer and flushed periodically instead of drawing every individual segment. Note this means that the PolyText buffer must be flushed before some operations (changing the brush color, drawing lines on top of the characters, inverting for cursor/selection, etc.)
// Arguments:
// - pwsLine - string of text to be written
// - cchLine - length of line to be read
// - coord - character coordinate target to render within viewport
// - cchCharWidths - This is the length of the string before double-wide characters are stripped. Used for determining clipping rectangle size 
//                 - The clipping rectangle is the font width * this many characters expected.
// - fTrimLeft - This specifies whether to trim one character width off the left side of the output. Used for drawing the right-half only of a double-wide character.
// Return Value:
// - S_OK or suitable GDI HRESULT error.
// - HISTORICAL NOTES:
// ETO_OPAQUE will paint the background color before painting the text.
// ETO_CLIPPED required for ClearType fonts. Cleartype rendering can escape bounding rectangle unless clipped.
//   Unclipped rectangles results in ClearType cutting off the right edge of the previous character when adding chars
//   and in leaving behind artifacts when backspace/removing chars.
//   This mainly applies to ClearType fonts like Lucida Console at small font sizes (10pt) or bolded.
// See: Win7: 390673, 447839 and then superseded by http://osgvsowi/638274 when FE/non-FE rendering condensed.
//#define CONSOLE_EXTTEXTOUT_FLAGS ETO_OPAQUE | ETO_CLIPPED
//#define MAX_POLY_LINES 80
HRESULT GdiEngine::PaintBufferLine(_In_reads_(cchLine) PCWCHAR const pwsLine,
                                   _In_ size_t const cchLine,
                                   _In_ COORD const coord,
                                   _In_ size_t const cchCharWidths,
                                   _In_ bool const fTrimLeft)
{
    // Exit early if there are no lines to draw.
    RETURN_HR_IF(S_OK, 0 == cchLine);

    POINT ptDraw = { 0 };
    RETURN_IF_FAILED(_ScaleByFont(&coord, &ptDraw));

    POLYTEXTW* const pPolyTextLine = &_pPolyText[_cPolyText];

    PWCHAR pwsPoly = new wchar_t[cchLine];
    RETURN_IF_NULL_ALLOC(pwsPoly);

    wmemcpy_s(pwsPoly, cchLine, pwsLine, cchLine);

    COORD const coordFontSize = _GetFontSize();

    pPolyTextLine->lpstr = pwsPoly;
    pPolyTextLine->n = (UINT)cchLine;
    pPolyTextLine->x = ptDraw.x;
    pPolyTextLine->y = ptDraw.y;
    pPolyTextLine->uiFlags = ETO_OPAQUE | ETO_CLIPPED;
    pPolyTextLine->rcl.left = pPolyTextLine->x;
    pPolyTextLine->rcl.top = pPolyTextLine->y;
    pPolyTextLine->rcl.right = pPolyTextLine->rcl.left + ((SHORT)cchCharWidths * coordFontSize.X);
    pPolyTextLine->rcl.bottom = pPolyTextLine->rcl.top + coordFontSize.Y;

    if (fTrimLeft)
    {
        pPolyTextLine->rcl.left += coordFontSize.X;
    }

    _cPolyText++;

    if (_cPolyText >= s_cPolyTextCache)
    {
        LOG_IF_FAILED(_FlushBufferLines());
    }

    return S_OK;
}

// Routine Description:
// - Flushes any buffer lines in the PolyTextOut cache by drawing them and freeing the strings.
// - See also: PaintBufferLine
// Arguments:
// - <none>
// Return Value:
// - S_OK or E_FAIL if GDI failed.
HRESULT GdiEngine::_FlushBufferLines()
{
    HRESULT hr = S_OK;

    if (_cPolyText > 0)
    {
        if (!PolyTextOutW(_hdcMemoryContext, _pPolyText, (UINT)_cPolyText))
        {
            hr = E_FAIL;
        }

        for (size_t iPoly = 0; iPoly < _cPolyText; iPoly++)
        {
            if (nullptr != _pPolyText[iPoly].lpstr)
            {
                delete[] _pPolyText[iPoly].lpstr;
            }
        }

        _cPolyText = 0;
    }

    RETURN_HR(hr);
}

// Routine Description:
// - Draws up to one line worth of grid lines on top of characters.
// Arguments:
// - lines - Enum defining which edges of the rectangle to draw
// - color - The color to use for drawing the edges.
// - cchLine - How many characters we should draw the grid lines along (left to right in a row)
// - coordTarget - The starting X/Y position of the first character to draw on.
// Return Value:
// - S_OK or suitable GDI HRESULT error or E_FAIL for GDI errors in functions that don't reliably return a specific error code.
HRESULT GdiEngine::PaintBufferGridLines(_In_ GridLines const lines, _In_ COLORREF const color, _In_ size_t const cchLine, _In_ COORD const coordTarget)
{
    // Return early if there are no lines to paint.
    RETURN_HR_IF(S_OK, GridLines::None == lines);

    LOG_IF_FAILED(_FlushBufferLines());

    // Convert the target from characters to pixels.
    POINT ptTarget;
    RETURN_IF_FAILED(_ScaleByFont(&coordTarget, &ptTarget));
    // Set the brush color as requested and save the previous brush to restore at the end.
    wil::unique_hbrush hbr(CreateSolidBrush(color));
    RETURN_LAST_ERROR_IF_NULL(hbr.get());

    wil::unique_hbrush hbrPrev(SelectBrush(_hdcMemoryContext, hbr.get()));
    RETURN_LAST_ERROR_IF_NULL(hbrPrev.get());
    hbr.release(); // If SelectBrush was successful, GDI owns the brush. Release for now.

    // On exit, be sure we try to put the brush back how it was originally.
    auto restoreBrushOnExit = wil::ScopeExit([&] { hbr.reset(SelectBrush(_hdcMemoryContext, hbrPrev.get())); });

    // Get the font size so we know the size of the rectangle lines we'll be inscribing.
    COORD const coordFontSize = _GetFontSize();

    // For each length of the line, inscribe the various lines as specified by the enum
    for (size_t i = 0; i < cchLine; i++)
    {
        if (lines & GridLines::Top)
        {
            RETURN_HR_IF_FALSE(E_FAIL, PatBlt(_hdcMemoryContext, ptTarget.x, ptTarget.y, coordFontSize.X, 1, PATCOPY));
        }

        if (lines & GridLines::Left)
        {
            RETURN_HR_IF_FALSE(E_FAIL, PatBlt(_hdcMemoryContext, ptTarget.x, ptTarget.y, 1, coordFontSize.Y, PATCOPY));
        }

        // NOTE: Watch out for inclusive/exclusive rectangles here.
        // We have to remove 1 from the font size for the bottom and right lines to ensure that the
        // starting point remains within the clipping rectangle. 
        // For example, if we're drawing a letter at 0,0 and the font size is 8x16....
        // The bottom left corner inclusive is at 0,15 which is Y (0) + Font Height (16) - 1 = 15.
        // The top right corner inclusive is at 7,0 which is X (0) + Font Height (8) - 1 = 7.

        if (lines & GridLines::Bottom)
        {
            RETURN_HR_IF_FALSE(E_FAIL, PatBlt(_hdcMemoryContext, ptTarget.x, ptTarget.y + coordFontSize.Y - 1, coordFontSize.X, 1, PATCOPY));
        }

        if (lines & GridLines::Right)
        {
            RETURN_HR_IF_FALSE(E_FAIL, PatBlt(_hdcMemoryContext, ptTarget.x + coordFontSize.X - 1, ptTarget.y, 1, coordFontSize.Y, PATCOPY));
        }

        // Move to the next character in this run.
        ptTarget.x += coordFontSize.X;
    }

    return S_OK;
}

// Routine Description:
// - Draws the cursor on the screen
// Arguments:
// - coord - Coordinate position where the cursor should be drawn
// - ulHeightPercent - The cursor will be drawn at this percentage of the current font height.
// - fIsDoubleWidth - The cursor should be drawn twice as wide as usual.
// Return Value:
// - S_OK, suitable GDI HRESULT error, or safemath error, or E_FAIL in a GDI error where a specific error isn't set.
HRESULT GdiEngine::PaintCursor(_In_ COORD const coord, _In_ ULONG const ulHeightPercent, _In_ bool const fIsDoubleWidth)
{
    LOG_IF_FAILED(_FlushBufferLines());

    COORD const coordFontSize = _GetFontSize();
    RETURN_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_STATE), coordFontSize.X == 0 || coordFontSize.Y == 0);

    // First set up a block cursor the size of the font.
    RECT rcInvert;
    RETURN_IF_FAILED(LongMult(coord.X, coordFontSize.X, &rcInvert.left));
    RETURN_IF_FAILED(LongMult(coord.Y, coordFontSize.Y, &rcInvert.top));
    RETURN_IF_FAILED(LongAdd(rcInvert.left, coordFontSize.X, &rcInvert.right));
    RETURN_IF_FAILED(LongAdd(rcInvert.top, coordFontSize.Y, &rcInvert.bottom));

    // If we're double-width cursor, make it an extra font wider.
    if (fIsDoubleWidth)
    {
        RETURN_IF_FAILED(LongAdd(rcInvert.right, coordFontSize.X, &rcInvert.right));
    }

    // Now adjust the cursor height
    // enforce min/max cursor height
    ULONG ulHeight = ulHeightPercent;
    ulHeight = max(ulHeight, s_ulMinCursorHeightPercent); // No smaller than 25%
    ulHeight = min(ulHeight, s_ulMaxCursorHeightPercent); // No larger than 100%

    ulHeight = MulDiv(coordFontSize.Y, ulHeight, 100); // divide by 100 because percent.

    // Reduce the height of the top to be relative to the bottom by the height we want.
    RETURN_IF_FAILED(LongSub(rcInvert.bottom, ulHeight, &rcInvert.top));

    RETURN_HR_IF_FALSE(E_FAIL, InvertRect(_hdcMemoryContext, &rcInvert));

    // Save inverted cursor position so we can clear it.
    _rcCursorInvert = rcInvert;

    return S_OK;
}

// Routine Description:
// - Clears out the cursor that was set in the previous PaintCursor call.
// Arguments:
// - <none>
// Return Value:
// - S_OK, suitable GDI HRESULT error, or E_FAIL in a GDI error where a specific error isn't set.
HRESULT GdiEngine::ClearCursor()
{
    if (!IsRectEmpty(&_rcCursorInvert))
    {
        // We inverted to set the cursor, so invert the same rect to clear it out.
        RETURN_HR_IF_FALSE(E_FAIL, InvertRect(_hdcMemoryContext, &_rcCursorInvert));

        _rcCursorInvert = { 0 };
    }

    return S_OK;
}

// Routine Description:
//  - Inverts the selected region on the current screen buffer.
//  - Reads the selected area, selection mode, and active screen buffer
//    from the global properties and dispatches a GDI invert on the selected text area.
// Arguments:
//  - rgsrSelection - Array of rectangles, one per line, that should be inverted to make the selection area
// - cRectangles - Count of rectangle array length
// Return Value:
// - S_OK or suitable GDI HRESULT error.
HRESULT GdiEngine::PaintSelection(_In_reads_(cRectangles) SMALL_RECT* const rgsrSelection, _In_ UINT const cRectangles)
{
    LOG_IF_FAILED(_FlushBufferLines());

    // Get a region ready
    wil::unique_hrgn hrgnSelection(CreateRectRgn(0, 0, 0, 0));
    RETURN_LAST_ERROR_IF_NULL(hrgnSelection.get());

    // Adjust the selected region to invert
    RETURN_IF_FAILED(_PaintSelectionCalculateRegion(rgsrSelection, cRectangles, hrgnSelection.get()));

    // Save the painted region for the next paint
    int rgnType = CombineRgn(_hrgnGdiPaintedSelection, hrgnSelection.get(), nullptr, RGN_COPY);

    // Don't paint if there was an error in the region or it's empty.
    if (ERROR != rgnType && NULLREGION != rgnType)
    {
        // Do the invert
        RETURN_LAST_ERROR_IF_FALSE(InvertRgn(_hdcMemoryContext, hrgnSelection.get()));
    }

    return S_OK;
}

// Routine Description:
//  - Composes a GDI region representing the area of the buffer that
//    is currently selected based on member variable (selection rectangle) state.
// Arguments:
//  - rgsrSelection - Array of rectangles, one per line, that should be inverted to make the selection area
// - cRectangles - Count of rectangle array length
//  - hrgnSelection - Handle to empty GDI region. Will be filled with selection region information.
// Return Value:
//  - HRESULT S_OK or Expect GDI-based errors or memory errors.
HRESULT GdiEngine::_PaintSelectionCalculateRegion(_In_reads_(cRectangles) SMALL_RECT* const rgsrSelection,
                                                  _In_ UINT const cRectangles,
                                                  _Inout_ HRGN const hrgnSelection) const
{
    // for each row in the selection
    for (UINT iRect = 0; iRect < cRectangles; iRect++)
    {
        // multiply character counts by font size to obtain pixels
        RECT rectHighlight;
        RETURN_IF_FAILED(_ScaleByFont(&rgsrSelection[iRect], &rectHighlight));

        // create region for selection rectangle
        wil::unique_hrgn hrgnLine(CreateRectRgn(rectHighlight.left, rectHighlight.top, rectHighlight.right, rectHighlight.bottom));
        RETURN_LAST_ERROR_IF_NULL(hrgnLine.get());

        // compose onto given selection region
        LOG_LAST_ERROR_IF_FALSE(CombineRgn(hrgnSelection, hrgnSelection, hrgnLine.get(), RGN_OR));
    }

    return S_OK;
}

#ifdef DBG
// Routine Description:
// - Will fill a given rectangle with a gray shade to help identify which portion of the screen is being debugged.
// - Will attempt immediate BLT so you can see it.
// - NOTE: You must set _fDebug flag for this to operate using a debugger.
// - NOTE: This only works in Debug (DBG) builds.
// Arguments:
// - prc - Pointer to rectangle to fill
// Return Value:
// - <none>
void GdiEngine::_PaintDebugRect(_In_ const RECT* const prc) const
{
    if (_fDebug)
    {
        if (!IsRectEmpty(prc))
        {
            wil::unique_hbrush hbr(GetStockBrush(GRAY_BRUSH));
            if (nullptr != LOG_LAST_ERROR_IF_NULL(hbr.get()))
            {
                LOG_LAST_ERROR_IF_FALSE(FillRect(_hdcMemoryContext, prc, hbr.get()));

                _DoDebugBlt(prc);
            }
        }
    }
}

// Routine Description:
// - Will immediately Blt the given rectangle to the screen for aid in debugging when it is tough to see
//   what is occuring with the in-memory DC.
// - This will pause the thread for 200ms when called to give you an opportunity to see the paint.
// - NOTE: You must set _fDebug flag for this to operate using a debugger.
// - NOTE: This only works in Debug (DBG) builds.
// Arguments:
// - prc - Pointer to region to immediately Blt to the real screen DC.
// Return Value:
// - <none>
void GdiEngine::_DoDebugBlt(_In_ const RECT* const prc) const
{
    if (_fDebug)
    {
        if (!IsRectEmpty(prc))
        {
            LOG_LAST_ERROR_IF_FALSE(BitBlt(_psInvalidData.hdc, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, _hdcMemoryContext, prc->left, prc->top, SRCCOPY));
            Sleep(200);
        }
    }
}
#endif