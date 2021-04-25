// Scintilla source code edit control
/** @file Editor.h
 ** Defines the main editor class.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

namespace Scintilla {

/**
 */
class Timer {
public:
	bool ticking;
	int ticksToWait;
	enum {
		tickSize = 100
	};
	TickerID tickerID;

	Timer() noexcept;
};

/**
 */
class Idler {
public:
	bool state;
	IdlerID idlerID;

	Idler() noexcept;
};

/**
 * When platform has a way to generate an event before painting,
 * accumulate needed styling range and other work items in
 * WorkNeeded to avoid unnecessary work inside paint handler
 */

enum class WorkItems {
	none = 0,
	style = 1,
	updateUI = 2
};

class WorkNeeded {
public:
	WorkItems items = WorkItems::none;
	Sci::Position upTo = 0;

	WorkNeeded() noexcept = default;
	void Reset() noexcept {
		items = WorkItems::none;
		upTo = 0;
	}
	void Need(WorkItems items_, Sci::Position pos) noexcept {
		if (FlagSet(items_, WorkItems::style) && (upTo < pos))
			upTo = pos;
		items = static_cast<WorkItems>(static_cast<int>(items) | static_cast<int>(items_));
	}
};

/**
 * Hold a piece of text selected for copying or dragging, along with encoding and selection format information.
 */
class SelectionText {
	std::string s;
public:
	bool rectangular = false;
	bool lineCopy = false;
	bool asBinary = false;
	int codePage = 0;
	SelectionText() noexcept = default;
	void Clear() noexcept {
		s.clear();
		rectangular = false;
		lineCopy = false;
		asBinary = false;
		codePage = 0;
	}
	void Copy(const std::string &s_, int codePage_, bool rectangular_, bool lineCopy_) {
		s = s_;
		codePage = codePage_;
		rectangular = rectangular_;
		lineCopy = lineCopy_;
		if (!asBinary) {
			FixSelectionForClipboard();
		}
	}
	const char *Data() const noexcept {
		return s.c_str();
	}
	size_t Length() const noexcept {
		return s.length();
	}
	size_t LengthWithTerminator() const noexcept {
		return s.length() + 1;
	}
	bool Empty() const noexcept {
		return s.empty();
	}
private:
	void FixSelectionForClipboard() {
		// To avoid truncating the contents of the clipboard when pasted where the
		// clipboard contains NUL characters, replace NUL characters by spaces.
		char *first = static_cast<char *>(::memchr(s.data(), 0, s.length()));
		if (first != nullptr) {
			*first++ = ' ';
			const ptrdiff_t diff = first - s.data();
			std::replace(s.begin() + diff, s.end(), '\0', ' ');
		}
	}
};

struct WrapPending {
	// The range of lines that need to be wrapped
	enum {
		lineLarge = 0x7ffffff
	};
	Sci::Line start;	// When there are wraps pending, will be in document range
	Sci::Line end;	// May be lineLarge to indicate all of document after start
	WrapPending() noexcept {
		start = lineLarge;
		end = lineLarge;
	}
	void Reset() noexcept {
		start = lineLarge;
		end = lineLarge;
	}
	void Wrapped(Sci::Line line) noexcept {
		if (start == line)
			start++;
	}
	bool NeedsWrap() const noexcept {
		return start < end;
	}
	bool AddRange(Sci::Line lineStart, Sci::Line lineEnd) noexcept {
		const bool neededWrap = NeedsWrap();
		bool changed = false;
		if (start > lineStart) {
			start = lineStart;
			changed = true;
		}
		if ((end < lineEnd) || !neededWrap) {
			end = lineEnd;
			changed = true;
		}
		return changed;
	}
};

struct CaretPolicy {
	int policy;
	int slop;	// Pixels for X, lines for Y
	CaretPolicy(uptr_t policy_=0, sptr_t slop_=0) noexcept:
		policy(static_cast<int>(policy_)), slop(static_cast<int>(slop_)) {}
};

struct CaretPolicies {
	CaretPolicy x;
	CaretPolicy y;
};

enum class XYScrollOptions {
	none = 0x0,
	useMargin = 0x1,
	vertical = 0x2,
	horizontal = 0x4,
	all = useMargin | vertical | horizontal
};

/**
 */
class Editor : public EditModel, public DocWatcher {
protected:	// ScintillaBase subclass needs access to much of Editor

	/** On GTK+, Scintilla is a container widget holding two scroll bars
	 * whereas on Windows there is just one window with both scroll bars turned on. */
	Window wMain;	///< The Scintilla parent window
	Window wMargin;	///< May be separate when using a scroll view for wMain

	/** Style resources may be expensive to allocate so are cached between uses.
	 * When a style attribute is changed, this cache is flushed. */
	bool stylesValid;
	ViewStyle vs;
	int technology;
	Point sizeRGBAImage;
	float scaleRGBAImage;

	MarginView marginView;
	EditView view;

	int cursorMode;

	bool hasFocus;
	bool mouseDownCaptures;
	bool mouseWheelCaptures;

	int xCaretMargin;	///< Ensure this many pixels visible on both sides of caret
	bool horizontalScrollBarVisible;
	int scrollWidth;
	bool verticalScrollBarVisible;
	int endAtLastLine;
	int caretSticky;
	int marginOptions;
	bool mouseSelectionRectangularSwitch;
	bool multipleSelection;
	bool additionalSelectionTyping;
	int multiPasteMode;

	int virtualSpaceOptions;

	KeyMap kmap;

	Timer timer;
	Timer autoScrollTimer;
	enum {
		autoScrollDelay = 200
	};

	Idler idler;

	Point lastClick;
	unsigned int lastClickTime;
	Point doubleClickCloseThreshold;
	int dwellDelay;
	int ticksToDwell;
	bool dwelling;
	enum class TextUnit {
		character, word, subLine, wholeLine
	} selectionUnit;
	Point ptMouseLast;
	enum class DragDrop {
		none, initial, dragging
	} inDragDrop;
	bool dropWentOutside;
	SelectionPosition posDrop;
	Sci::Position hotSpotClickPos;
	int lastXChosen;
	Sci::Position lineAnchorPos;
	Sci::Position originalAnchorPos;
	Sci::Position wordSelectAnchorStartPos;
	Sci::Position wordSelectAnchorEndPos;
	Sci::Position wordSelectInitialCaretPos;
	SelectionSegment targetRange;
	int searchFlags;
	Sci::Line topLine;
	Sci::Position posTopLine;
	Sci::Position lengthForEncode;

	int needUpdateUI;

	enum class PaintState {
		notPainting, painting, abandoned
	} paintState;
	bool paintAbandonedByStyling;
	PRectangle rcPaint;
	bool paintingAllText;
	bool willRedrawAll;
	WorkNeeded workNeeded;
	int idleStyling;
	bool needIdleStyling;

	int modEventMask;
	bool commandEvents;

	SelectionText drag;

	CaretPolicies caretPolicies;

	CaretPolicy visiblePolicy;

	Sci::Position searchAnchor;

	bool recordingMacro;

	int foldAutomatic;

	// Wrapping support
	WrapPending wrapPending;
	ActionDuration durationWrapOneLine;

	bool convertPastes;

	Editor();
	// ~Editor() in public section
	virtual void Initialise() noexcept = 0;
	virtual void Finalise() noexcept;

	void InvalidateStyleData();
	void InvalidateStyleRedraw();
	void RefreshStyleData();
	void SetRepresentations();
	void DropGraphics() noexcept;

	// The top left visible point in main window coordinates. Will be (0, 0) except for
	// scroll views where it will be equivalent to the current scroll position.
	Point GetVisibleOriginInMain() const noexcept override;
	PointDocument SCICALL DocumentPointFromView(Point ptView) const noexcept;  // Convert a point from view space to document
	Sci::Line TopLineOfMain() const noexcept override;   // Return the line at Main's y coordinate 0
	virtual PRectangle GetClientRectangle() const noexcept;
	virtual PRectangle GetClientDrawingRectangle() const noexcept;
	PRectangle GetTextRectangle() const noexcept;

	Sci::Line LinesOnScreen() const noexcept override;
	Sci::Line LinesToScroll() const noexcept;
	Sci::Line MaxScrollPos() const noexcept;
	SelectionPosition ClampPositionIntoDocument(SelectionPosition sp) const noexcept;
	Point LocationFromPosition(SelectionPosition pos, PointEnd pe = PointEnd::start);
	Point LocationFromPosition(Sci::Position pos, PointEnd pe = PointEnd::start);
	int XFromPosition(SelectionPosition sp);
	SelectionPosition SCICALL SPositionFromLocation(Point pt, bool canReturnInvalid = false, bool charPosition = false, bool virtualSpace = true);
	Sci::Position SCICALL PositionFromLocation(Point pt, bool canReturnInvalid = false, bool charPosition = false);
	SelectionPosition SPositionFromLineX(Sci::Line lineDoc, int x);
	Sci::Position PositionFromLineX(Sci::Line lineDoc, int x);
	Sci::Line SCICALL LineFromLocation(Point pt) const noexcept;
	void SetTopLine(Sci::Line topLineNew) noexcept;

	virtual bool AbandonPaint() noexcept;
	virtual void SCICALL RedrawRect(PRectangle rc) noexcept;
	virtual void DiscardOverdraw() noexcept;
	virtual void Redraw() noexcept;
	void RedrawSelMargin(Sci::Line line = -1, bool allAfter = false) noexcept;
	PRectangle RectangleFromRange(Range r, int overlap) const noexcept;
	void InvalidateRange(Sci::Position start, Sci::Position end) noexcept;

	bool UserVirtualSpace() const noexcept {
		return ((virtualSpaceOptions & SCVS_USERACCESSIBLE) != 0);
	}
	Sci::Position CurrentPosition() const noexcept;
	bool SelectionEmpty() const noexcept;
	SelectionPosition SelectionStart() noexcept;
	SelectionPosition SelectionEnd() noexcept;
	void SetRectangularRange();
	void ThinRectangularRange();
	void InvalidateSelection(SelectionRange newMain, bool invalidateWholeSelection = false) noexcept;
	void InvalidateWholeSelection() noexcept;
	SelectionRange LineSelectionRange(SelectionPosition currentPos_, SelectionPosition anchor_, bool withEOL = false) const noexcept;
	void SetSelection(SelectionPosition currentPos_, SelectionPosition anchor_);
	void SetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void SetSelection(SelectionPosition currentPos_);
	void SetEmptySelection(SelectionPosition currentPos_);
	void SetEmptySelection(Sci::Position currentPos_);
	enum class AddNumber {
		one, each
	};
	void MultipleSelectAdd(AddNumber addNumber);
	bool RangeContainsProtected(Sci::Position start, Sci::Position end) const noexcept;
	bool SelectionContainsProtected() const noexcept;
	Sci::Position MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir, bool checkLineEnd = true) const noexcept;
	SelectionPosition MovePositionOutsideChar(SelectionPosition pos, Sci::Position moveDir, bool checkLineEnd = true) const noexcept;
	void MovedCaret(SelectionPosition newPos, SelectionPosition previousPos,
		bool ensureVisible, CaretPolicies policies);
	void MovePositionTo(SelectionPosition newPos, Selection::SelTypes selt = Selection::SelTypes::none, bool ensureVisible = true);
	void MovePositionTo(Sci::Position newPos, Selection::SelTypes selt = Selection::SelTypes::none, bool ensureVisible = true);
	SelectionPosition MovePositionSoVisible(SelectionPosition pos, int moveDir) noexcept;
	SelectionPosition MovePositionSoVisible(Sci::Position pos, int moveDir) noexcept;
	Point PointMainCaret();
	void SetLastXChosen();

	void ScrollTo(Sci::Line line, bool moveThumb = true);
	virtual void ScrollText(Sci::Line linesToMove);
	void HorizontalScrollTo(int xPos);
	void VerticalCentreCaret() noexcept;
	void MoveSelectedLines(int lineDelta);
	void MoveSelectedLinesUp();
	void MoveSelectedLinesDown();
	void MoveCaretInsideView(bool ensureVisible = true);
	Sci::Line DisplayFromPosition(Sci::Position pos);

	struct XYScrollPosition {
		int xOffset;
		Sci::Line topLine;
		XYScrollPosition(int xOffset_, Sci::Line topLine_) noexcept : xOffset(xOffset_), topLine(topLine_) {}
		bool operator==(const XYScrollPosition &other) const noexcept {
			return (xOffset == other.xOffset) && (topLine == other.topLine);
		}
	};
	XYScrollPosition XYScrollToMakeVisible(SelectionRange range, XYScrollOptions options, CaretPolicies policies);
	void SetXYScroll(XYScrollPosition newXY);
	void EnsureCaretVisible(bool useMargin = true, bool vert = true, bool horiz = true);
	void ScrollRange(SelectionRange range);
	void ShowCaretAtCurrentPosition();
	void DropCaret();
	void CaretSetPeriod(int period);
	void InvalidateCaret();
	virtual void NotifyCaretMove() noexcept;
	virtual void UpdateSystemCaret();

	bool Wrapping() const noexcept;
	void NeedWrapping(Sci::Line docLineStart = 0, Sci::Line docLineEnd = WrapPending::lineLarge) noexcept;
	bool WrapOneLine(Surface *surface, Sci::Line lineToWrap);
	enum class WrapScope {
		wsAll, wsVisible, wsIdle
	};
	bool WrapLines(WrapScope ws);
	void LinesJoin();
	void LinesSplit(int pixelWidth);

	void SCICALL PaintSelMargin(Surface *surfaceWindow, PRectangle rc);
	void RefreshPixMaps(Surface *surfaceWindow);
	void SCICALL Paint(Surface *surfaceWindow, PRectangle rcArea);
	Sci::Position FormatRange(bool draw, const Sci_RangeToFormat *pfr);
	long TextWidth(uptr_t style, const char *text);

	virtual void SetVerticalScrollPos() = 0;
	virtual void SetHorizontalScrollPos() = 0;
	virtual bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) = 0;
	virtual void ReconfigureScrollBars() noexcept;
	void SetScrollBars();
	void ChangeSize();

	void FilterSelections();
	Sci::Position RealizeVirtualSpace(Sci::Position position, Sci::Position virtualSpace);
	SelectionPosition RealizeVirtualSpace(SelectionPosition position);
	void AddChar(char ch);
	virtual void InsertCharacter(std::string_view sv, CharacterSource charSource);
	void ClearBeforeTentativeStart();
	void InsertPaste(const char *text, Sci::Position len);
	enum class PasteShape {
		stream = 0, rectangular = 1, line = 2
	};
	void InsertPasteShape(const char *text, Sci::Position len, PasteShape shape);
	void ClearSelection(bool retainMultipleSelections = false);
	void ClearAll();
	void ClearDocumentStyle();
	virtual void Cut(bool asBinary, bool lineCopy = false);
	void PasteRectangular(SelectionPosition pos, const char *ptr, Sci::Position len);
	virtual void Copy(bool asBinary) = 0;
	void CopyAllowLine();
	virtual bool CanPaste() noexcept;
	virtual void Paste(bool asBinary) = 0;
	void Clear();
	virtual void SelectAll();
	virtual void Undo();
	virtual void Redo();
	bool BackspaceUnindent(Sci::Position lineCurrentPos, Sci::Position caretPosition, Sci::Position *posSelect);
	void DelCharBack(bool allowLineStartDeletion);
	virtual void ClaimSelection() noexcept = 0;

	static constexpr int ModifierFlags(bool shift, bool ctrl, bool alt, bool meta = false, bool super = false) noexcept {
		return
			(shift ? SCI_SHIFT : 0) |
			(ctrl ? SCI_CTRL : 0) |
			(alt ? SCI_ALT : 0) |
			(meta ? SCI_META : 0) |
			(super ? SCI_SUPER : 0);
	}

	virtual void NotifyChange() noexcept = 0;
	virtual void NotifyFocus(bool focus);
	virtual void SetCtrlID(int identifier) noexcept;
	virtual int GetCtrlID() const noexcept {
		return ctrlID;
	}
	virtual void NotifyParent(SCNotification scn) noexcept = 0;
	virtual void NotifyStyleToNeeded(Sci::Position endStyleNeeded);
	void NotifyChar(int ch, CharacterSource charSource) noexcept;
	void NotifySavePoint(bool isSavePoint) noexcept;
	void NotifyModifyAttempt() noexcept;
	virtual void NotifyDoubleClick(Point pt, int modifiers);
	void NotifyHotSpotClicked(Sci::Position position, int modifiers) noexcept;
	void NotifyHotSpotDoubleClicked(Sci::Position position, int modifiers) noexcept;
	void NotifyHotSpotReleaseClick(Sci::Position position, int modifiers) noexcept;
	bool NotifyUpdateUI() noexcept;
	void NotifyPainted() noexcept;
	void NotifyIndicatorClick(bool click, Sci::Position position, int modifiers) noexcept;
	bool NotifyMarginClick(Point pt, int modifiers);
	bool NotifyMarginRightClick(Point pt, int modifiers);
	void NotifyNeedShown(Sci::Position pos, Sci::Position len) noexcept;
	void NotifyCodePageChanged(int oldCodePage) noexcept;
	void NotifyDwelling(Point pt, bool state);
	void NotifyZoom() noexcept;

	void NotifyModifyAttempt(Document *document, void *userData) noexcept override;
	void NotifySavePoint(Document *document, void *userData, bool atSavePoint) noexcept override;
	void CheckModificationForWrap(DocModification mh);
	void NotifyModified(Document *document, DocModification mh, void *userData) override;
	void NotifyDeleted(Document *document, void *userData) noexcept override;
	void NotifyStyleNeeded(Document *doc, void *userData, Sci::Position endStyleNeeded) override;
	void NotifyLexerChanged(Document *doc, void *userData) override;
	void NotifyErrorOccurred(Document *doc, void *userData, int status) noexcept override;
	void NotifyMacroRecord(unsigned int iMessage, uptr_t wParam, sptr_t lParam) noexcept;

	void ContainerNeedsUpdate(int flags) noexcept;
	void PageMove(int direction, Selection::SelTypes selt = Selection::SelTypes::none, bool stuttered = false);
	enum class CaseMapping {
		same, upper, lower
	};
	virtual std::string CaseMapString(const std::string &s, CaseMapping caseMapping);
	void ChangeCaseOfSelection(CaseMapping caseMapping);
	void LineTranspose();
	void LineReverse();
	void Duplicate(bool forLine);
	virtual void CancelModes() noexcept;
	void NewLine();
	SelectionPosition PositionUpOrDown(SelectionPosition spStart, int direction, int lastX);
	void CursorUpOrDown(int direction, Selection::SelTypes selt);
	void ParaUpOrDown(int direction, Selection::SelTypes selt);
	Range RangeDisplayLine(Sci::Line lineVisible);
	Sci::Position StartEndDisplayLine(Sci::Position pos, bool start);
	Sci::Position VCHomeDisplayPosition(Sci::Position position);
	Sci::Position VCHomeWrapPosition(Sci::Position position);
	Sci::Position LineEndWrapPosition(Sci::Position position);
	int HorizontalMove(unsigned int iMessage);
	int DelWordOrLine(unsigned int iMessage);
	virtual int KeyCommand(unsigned int iMessage);
	virtual int KeyDefault(int /* key */, int /*modifiers*/) noexcept;
	int KeyDownWithModifiers(int key, int modifiers, bool *consumed);

	void Indent(bool forwards);

	virtual std::unique_ptr<CaseFolder> CaseFolderForEncoding();
	Sci::Position FindText(uptr_t wParam, sptr_t lParam);
	void SearchAnchor() noexcept;
	Sci::Position SearchText(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	Sci::Position SearchInTarget(const char *text, Sci::Position length);
	void GoToLine(Sci::Line lineNo);

	virtual void CopyToClipboard(const SelectionText &selectedText) = 0;
	std::string RangeText(Sci::Position start, Sci::Position end) const;
	void CopySelectionRange(SelectionText *ss, bool allowLineCopy = false);
	void CopyRangeToClipboard(Sci::Position start, Sci::Position end, bool lineCopy = false);
	void CopyText(size_t length, const char *text);
	void SetDragPosition(SelectionPosition newPos);
	virtual void DisplayCursor(Window::Cursor c) noexcept;
	virtual bool SCICALL DragThreshold(Point ptStart, Point ptNow) noexcept;
	virtual void StartDrag();
	void DropAt(SelectionPosition position, const char *value, size_t lengthValue, bool moving, bool rectangular);
	void DropAt(SelectionPosition position, const char *value, bool moving, bool rectangular);
	/** PositionInSelection returns true if position in selection. */
	bool PositionInSelection(Sci::Position pos) noexcept;
	bool SCICALL PointInSelection(Point pt);
	bool SCICALL PointInSelMargin(Point pt) const noexcept;
	Window::Cursor GetMarginCursor(Point pt) const noexcept;
	void TrimAndSetSelection(Sci::Position currentPos_, Sci::Position anchor_);
	void LineSelection(Sci::Position lineCurrentPos_, Sci::Position lineAnchorPos_, bool wholeLine);
	void WordSelection(Sci::Position pos);
	void DwellEnd(bool mouseMoved);
	void MouseLeave();
	virtual void ButtonDownWithModifiers(Point pt, unsigned int curTime, int modifiers);
	virtual void RightButtonDownWithModifiers(Point pt, unsigned int curTime, int modifiers);
	void ButtonMoveWithModifiers(Point pt, unsigned int curTime, int modifiers);
	void ButtonUpWithModifiers(Point pt, unsigned int curTime, int modifiers);

	bool Idle();
	enum class TickReason {
		caret, scroll, widen, dwell, platform
	};
	virtual void TickFor(TickReason reason);
	virtual bool FineTickerRunning(TickReason reason) noexcept;
	virtual void FineTickerStart(TickReason reason, int millis, int tolerance) noexcept;
	virtual void FineTickerCancel(TickReason reason) noexcept;
	virtual bool SetIdle(bool) noexcept {
		return false;
	}
	virtual void SetMouseCapture(bool on) noexcept = 0;
	virtual bool HaveMouseCapture() noexcept = 0;
	void SetFocusState(bool focusState);

	Sci::Position SCICALL PositionAfterArea(PRectangle rcArea) const noexcept;
	void StyleToPositionInView(Sci::Position pos);
	Sci::Position PositionAfterMaxStyling(Sci::Position posMax, bool scrolling) const noexcept;
	void StartIdleStyling(bool truncatedLastStyling) noexcept;
	void SCICALL StyleAreaBounded(PRectangle rcArea, bool scrolling);
	constexpr bool SynchronousStylingToVisible() const noexcept {
		return (idleStyling == SC_IDLESTYLING_NONE) || (idleStyling == SC_IDLESTYLING_AFTERVISIBLE);
	}
	void IdleStyling();
	virtual void IdleWork();
	virtual void QueueIdleWork(WorkItems items, Sci::Position upTo = 0) noexcept;

	virtual int SupportsFeature(int feature) const;
	virtual bool SCICALL PaintContains(PRectangle rc) const noexcept;
	bool PaintContainsMargin() const noexcept;
	void CheckForChangeOutsidePaint(Range r) noexcept;
	void SetBraceHighlight(Sci::Position pos0, Sci::Position pos1, int matchStyle) noexcept;

	void SetAnnotationHeights(Sci::Line start, Sci::Line end);
	virtual void SetDocPointer(Document *document);

	void SetAnnotationVisible(int visible);
	void SetEOLAnnotationVisible(int visible) noexcept;

	Sci::Line ExpandLine(Sci::Line line);
	void SetFoldExpanded(Sci::Line lineDoc, bool expanded);
	void FoldLine(Sci::Line line, int action);
	void FoldExpand(Sci::Line line, int action, int level);
	Sci::Line ContractedFoldNext(Sci::Line lineStart) const noexcept;
	void EnsureLineVisible(Sci::Line lineDoc, bool enforcePolicy);
	void FoldChanged(Sci::Line line, int levelNow, int levelPrev);
	void NeedShown(Sci::Position pos, Sci::Position len);
	void FoldAll(int action);

	Sci::Position GetTag(char *tagValue, int tagNumber);
	Sci::Position ReplaceTarget(bool replacePatterns, const char *text, Sci::Position length = -1);

	bool PositionIsHotspot(Sci::Position position) const noexcept;
	bool SCICALL PointIsHotspot(Point pt);
	void SetHotSpotRange(const Point *pt);
	Range GetHotSpotRange() const noexcept override;
	void SetHoverIndicatorPosition(Sci::Position position) noexcept;
	void SCICALL SetHoverIndicatorPoint(Point pt);

	int CodePage() const noexcept;
	virtual bool ValidCodePage(int /* codePage */) const noexcept {
		return true;
	}
	virtual std::string UTF8FromEncoded(std::string_view encoded) const = 0;
	virtual std::string EncodedFromUTF8(std::string_view utf8) const = 0;

	Sci::Line WrapCount(Sci::Line line);
	void AddStyledText(const char *buffer, Sci::Position appendLength);

	virtual sptr_t DefWndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam) = 0;
	bool ValidMargin(uptr_t wParam) const noexcept;
	void StyleSetMessage(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	sptr_t StyleGetMessage(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	void SetSelectionNMessage(unsigned int iMessage, uptr_t wParam, sptr_t lParam) noexcept;

	static const char *StringFromEOLMode(int eolMode) noexcept;

	// Coercion functions for transforming WndProc parameters into pointers
	static void *PtrFromSPtr(sptr_t lParam) noexcept {
		return reinterpret_cast<void *>(lParam);
	}
	static const char *ConstCharPtrFromSPtr(sptr_t lParam) noexcept {
		return static_cast<const char *>(PtrFromSPtr(lParam));
	}
	static const unsigned char *ConstUCharPtrFromSPtr(sptr_t lParam) noexcept {
		return static_cast<const unsigned char *>(PtrFromSPtr(lParam));
	}
	static char *CharPtrFromSPtr(sptr_t lParam) noexcept {
		return static_cast<char *>(PtrFromSPtr(lParam));
	}
	static unsigned char *UCharPtrFromSPtr(sptr_t lParam) noexcept {
		return static_cast<unsigned char *>(PtrFromSPtr(lParam));
	}
	static void *PtrFromUPtr(uptr_t wParam) noexcept {
		return reinterpret_cast<void *>(wParam);
	}
	static const char *ConstCharPtrFromUPtr(uptr_t wParam) noexcept {
		return static_cast<const char *>(PtrFromUPtr(wParam));
	}

	static sptr_t StringResult(sptr_t lParam, const char *val) noexcept;
	static sptr_t BytesResult(sptr_t lParam, const unsigned char *val, size_t len) noexcept;

	// Set a variable controlling appearance to a value and invalidates the display
	// if a change was made. Avoids extra text and the possibility of mistyping.
	template <typename T>
	bool SetAppearance(T &variable, T value) {
		// Using ! and == as more types have == defined than !=.
		const bool changed = !(variable == value);
		if (changed) {
			variable = value;
			InvalidateStyleRedraw();
		}
		return changed;
	}

public:
	~Editor() override;

	// Deleted so Editor objects can not be copied.
	Editor(const Editor &) = delete;
	Editor(Editor &&) = delete;
	Editor &operator=(const Editor &) = delete;
	Editor &operator=(Editor &&) = delete;
	// Public so the COM thunks can access it.
	bool IsUnicodeMode() const noexcept;
	// Public so scintilla_send_message can use it.
	virtual sptr_t WndProc(unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	// Public so scintilla_set_id can use it.
	int ctrlID;
	// Public so COM methods for drag and drop can set it.
	int errorStatus;
	friend class AutoSurface;
};

/**
 * A smart pointer class to ensure Surfaces are set up and deleted correctly.
 */
class AutoSurface {
private:
	std::unique_ptr<Surface> surf;
public:
	AutoSurface(const Editor *ed, int technology = -1) {
		if (ed->wMain.GetID()) {
			surf = Surface::Allocate(technology != -1 ? technology : ed->technology);
			surf->Init(ed->wMain.GetID());
			surf->SetMode(SurfaceMode(ed->CodePage(), ed->BidirectionalR2L()));
		}
	}
	AutoSurface(SurfaceID sid, const Editor *ed, int technology = -1, bool printing = false) {
		if (ed->wMain.GetID()) {
			surf = Surface::Allocate(technology != -1 ? technology : ed->technology);
			surf->Init(sid, ed->wMain.GetID(), printing);
			surf->SetMode(SurfaceMode(ed->CodePage(), ed->BidirectionalR2L()));
		}
	}
	// Deleted so AutoSurface objects can not be copied.
	AutoSurface(const AutoSurface &) = delete;
	AutoSurface(AutoSurface &&) = delete;
	void operator=(const AutoSurface &) = delete;
	void operator=(AutoSurface &&) = delete;
	~AutoSurface() = default;
	Surface *operator->() const noexcept {
		return surf.get();
	}
	operator Surface *() const noexcept {
		return surf.get();
	}
	operator bool() const noexcept {
		return surf.operator bool();
	}
};

}
