// Scintilla source code edit control
/** @file Document.cxx
 ** Text document that handles notifications, DBCS, styling, words and end of line.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <climits>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <memory>
#include <chrono>

#ifndef NO_CXX11_REGEX
#include <regex>
#endif

#include "Debugging.h"

#include "ILoader.h"
#include "ILexer.h"
#include "Scintilla.h"

#include "CharacterSet.h"
//#include "CharacterCategory.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "RESearch.h"
#include "UniConversion.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;

void LexInterface::Colourise(Sci::Position start, Sci::Position end) {
	if (pdoc && instance && !performingStyle) {
		// Protect against reentrance, which may occur, for example, when
		// fold points are discovered while performing styling and the folding
		// code looks for child lines which may trigger styling.
		performingStyle = true;

		const Sci::Position lengthDoc = pdoc->Length();
		if (end == -1)
			end = lengthDoc;
		const Sci::Position len = end - start;

		PLATFORM_ASSERT(len >= 0);
		PLATFORM_ASSERT(start + len <= lengthDoc);

		int styleStart = 0;
		if (start > 0)
			styleStart = pdoc->StyleAt(start - 1);

		if (len > 0) {
			instance->Lex(start, len, styleStart, pdoc);
			instance->Fold(start, len, styleStart, pdoc);
		}

		performingStyle = false;
	}
}

int LexInterface::LineEndTypesSupported() const noexcept {
	if (instance) {
		return instance->LineEndTypesSupported();
	}
	return 0;
}

void ActionDuration::AddSample(Sci::Line numberActions, double durationOfActions) noexcept {
	// Only adjust for multiple actions to avoid instability
#if ActionDuration_MeasureTimeByBytes
	if (numberActions < ActionDuration_MeasureTimeByBytes) {
		return;
	}
#else
	if (numberActions < 8) {
		return;
	}
#endif

	// Alpha value for exponential smoothing.
	// Most recent value contributes 25% to smoothed value.
	constexpr double alpha = 0.25;

#if ActionDuration_MeasureTimeByBytes
	const double durationOne = (ActionDuration_MeasureTimeByBytes * durationOfActions) / numberActions;
#else
	const double durationOne = durationOfActions / numberActions;
#endif
	const double duration_ = alpha * durationOne + (1.0 - alpha) * duration;
	//duration = Clamp(duration_, minDuration, maxDuration);
	duration = std::max(duration_, minDuration);
	//printf("%s actions=%.9f / %zd, one=%.9f, value=%.9f, [%.9f, %f, %f]\n", __func__,
	//	durationOfActions, numberActions, durationOne, duration_, duration, minDuration, maxDuration);
}

double ActionDuration::Duration() const noexcept {
	return duration;
}

Sci::Line ActionDuration::ActionsInAllowedTime(double secondsAllowed) const noexcept {
	const Sci::Line actions = std::clamp<Sci::Line>(static_cast<Sci::Line>(secondsAllowed / duration), 8, 0x10000);
#if ActionDuration_MeasureTimeByBytes
	return actions * ActionDuration_MeasureTimeByBytes;
#else
	return actions;
#endif
}

Document::Document(int options) :
	cb((options & SC_DOCUMENTOPTION_STYLES_NONE) == 0, (options & SC_DOCUMENTOPTION_TEXT_LARGE) != 0) {
	refCount = 0;
#ifdef _WIN32
	eolMode = SC_EOL_CRLF;
#else
	eolMode = SC_EOL_LF;
#endif
	dbcsCodePage = SC_CP_UTF8;
	dbcsCharClass = nullptr;
	lineEndBitSet = SC_LINE_END_TYPE_DEFAULT;
	endStyled = 0;
	styleClock = 0;
	enteredModification = 0;
	enteredStyling = 0;
	enteredReadOnlyCount = 0;
	insertionSet = false;
	tabInChars = 8;
	indentInChars = 0;
	actualIndentInChars = 8;
	useTabs = true;
	tabIndents = true;
	backspaceUnindents = false;

	matchesValid = false;

	perLineData[ldMarkers] = std::make_unique<LineMarkers>();
	perLineData[ldLevels] = std::make_unique<LineLevels>();
	perLineData[ldState] = std::make_unique<LineState>();
	perLineData[ldMargin] = std::make_unique<LineAnnotation>();
	perLineData[ldAnnotation] = std::make_unique<LineAnnotation>();
	perLineData[ldEOLAnnotation] = std::make_unique<LineAnnotation>();

	decorations = DecorationListCreate(IsLarge());

	cb.SetPerLine(this);
	cb.SetUTF8Substance(SC_CP_UTF8 == dbcsCodePage);
}

Document::~Document() {
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifyDeleted(this, watcher.userData);
	}
}

// Increase reference count and return its previous value.
int Document::AddRef() noexcept {
	return refCount++;
}

// Decrease reference count and return its previous value.
// Delete the document if reference count reaches zero.
int SCI_METHOD Document::Release() noexcept {
	const int curRefCount = --refCount;
	if (curRefCount == 0)
		delete this;
	return curRefCount;
}

void Document::Init() {
	for (const auto &pl : perLineData) {
		if (pl)
			pl->Init();
	}
}

bool Document::IsActive() const noexcept {
	return std::any_of(std::begin(perLineData), std::end(perLineData),
	[](const auto &pl) noexcept {
		return pl->IsActive();
	});
}

void Document::InsertLine(Sci::Line line) {
	for (const auto &pl : perLineData) {
		if (pl)
			pl->InsertLine(line);
	}
}

void Document::InsertLines(Sci::Line line, Sci::Line lines) {
	for (const auto &pl : perLineData) {
		if (pl)
			pl->InsertLines(line, lines);
	}
}

void Document::RemoveLine(Sci::Line line) {
	for (const auto &pl : perLineData) {
		if (pl)
			pl->RemoveLine(line);
	}
}

LineMarkers *Document::Markers() const noexcept {
	return down_cast<LineMarkers *>(perLineData[ldMarkers].get());
}

LineLevels *Document::Levels() const noexcept {
	return down_cast<LineLevels *>(perLineData[ldLevels].get());
}

LineState *Document::States() const noexcept {
	return down_cast<LineState *>(perLineData[ldState].get());
}

LineAnnotation *Document::Margins() const noexcept {
	return down_cast<LineAnnotation *>(perLineData[ldMargin].get());
}

LineAnnotation *Document::Annotations() const noexcept {
	return down_cast<LineAnnotation *>(perLineData[ldAnnotation].get());
}

LineAnnotation *Document::EOLAnnotations() const noexcept {
	return down_cast<LineAnnotation *>(perLineData[ldEOLAnnotation].get());
}

int Document::LineEndTypesSupported() const noexcept {
	if ((SC_CP_UTF8 == dbcsCodePage) && pli)
		return pli->LineEndTypesSupported();
	else
		return 0;
}

bool Document::SetDBCSCodePage(int dbcsCodePage_) {
	if (dbcsCodePage != dbcsCodePage_) {
		dbcsCodePage = dbcsCodePage_;
		pcf.reset();
		cb.SetLineEndTypes(lineEndBitSet & LineEndTypesSupported());
		cb.SetUTF8Substance(SC_CP_UTF8 == dbcsCodePage);
		dbcsCharClass = DBCSCharClassify::Get(dbcsCodePage_);
		ModifiedAt(0);	// Need to restyle whole document
		return true;
	} else {
		return false;
	}
}

bool Document::SetLineEndTypesAllowed(int lineEndBitSet_) {
	if (lineEndBitSet != lineEndBitSet_) {
		lineEndBitSet = lineEndBitSet_;
		const int lineEndBitSetActive = lineEndBitSet & LineEndTypesSupported();
		if (lineEndBitSetActive != cb.GetLineEndTypes()) {
			ModifiedAt(0);
			cb.SetLineEndTypes(lineEndBitSetActive);
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

void Document::SetSavePoint() noexcept {
	cb.SetSavePoint();
	NotifySavePoint(true);
}

void Document::TentativeUndo(bool pendingUpdate) {
	if (!TentativeActive())
		return;
	CheckReadOnly();
	if (enteredModification == 0) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			const bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			const int steps = cb.TentativeSteps();
			//Platform::DebugPrintf("Steps=%d\n", steps);
			for (int step = 0; step < steps; step++) {
				const Sci::Line prevLinesTotal = LinesTotal();
				const Action &action = cb.GetUndoStep();
				if (action.at == ActionType::remove) {
					NotifyModified(DocModification(
						SC_MOD_BEFOREINSERT | SC_PERFORMED_UNDO, action));
				} else if (action.at == ActionType::container) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_UNDO);
					dm.token = action.position;
					NotifyModified(dm);
				} else {
					NotifyModified(DocModification(
						SC_MOD_BEFOREDELETE | SC_PERFORMED_UNDO, action));
				}
				cb.PerformUndoStep();
				if (action.at != ActionType::container) {
					ModifiedAt(action.position);
				}

				int modFlags = SC_PERFORMED_UNDO;
				// With undo, an insertion action becomes a deletion notification
				if (action.at == ActionType::remove) {
					modFlags |= SC_MOD_INSERTTEXT;
				} else if (action.at == ActionType::insert) {
					modFlags |= SC_MOD_DELETETEXT;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Line linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(DocModification(modFlags, action.position, action.lenData,
					linesAdded, action.data.get()));
			}

			const bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint && !pendingUpdate) {
				NotifySavePoint(endSavePoint);
			}

			cb.TentativeCommit();
		}
		enteredModification--;
	}
}

MarkerMask Document::GetMark(Sci::Line line) const noexcept {
	return Markers()->MarkValue(line);
}

Sci::Line Document::MarkerNext(Sci::Line lineStart, MarkerMask mask) const noexcept {
	return Markers()->MarkerNext(lineStart, mask);
}

int Document::AddMark(Sci::Line line, int markerNum) {
	if (line >= 0 && line <= LinesTotal()) {
		const int prev = Markers()->AddMark(line, markerNum, LinesTotal());
		const DocModification mh(SC_MOD_CHANGEMARKER, LineStart(line), 0, 0, nullptr, line);
		NotifyModified(mh);
		return prev;
	} else {
		return -1;
	}
}

void Document::AddMarkSet(Sci::Line line, MarkerMask valueSet) {
	if (line < 0 || line > LinesTotal()) {
		return;
	}
	MarkerMask m = valueSet;
	for (int i = 0; m; i++, m >>= 1) {
		if (m & 1)
			Markers()->AddMark(line, i, LinesTotal());
	}
	const DocModification mh(SC_MOD_CHANGEMARKER, LineStart(line), 0, 0, nullptr, line);
	NotifyModified(mh);
}

void Document::DeleteMark(Sci::Line line, int markerNum) {
	Markers()->DeleteMark(line, markerNum, false);
	const DocModification mh(SC_MOD_CHANGEMARKER, LineStart(line), 0, 0, nullptr, line);
	NotifyModified(mh);
}

void Document::DeleteMarkFromHandle(int markerHandle) {
	Markers()->DeleteMarkFromHandle(markerHandle);
	DocModification mh(SC_MOD_CHANGEMARKER);
	mh.line = -1;
	NotifyModified(mh);
}

void Document::DeleteAllMarks(int markerNum) {
	bool someChanges = false;
	for (Sci::Line line = 0; line < LinesTotal(); line++) {
		if (Markers()->DeleteMark(line, markerNum, true))
			someChanges = true;
	}
	if (someChanges) {
		DocModification mh(SC_MOD_CHANGEMARKER);
		mh.line = -1;
		NotifyModified(mh);
	}
}

Sci::Line Document::LineFromHandle(int markerHandle) const noexcept {
	return Markers()->LineFromHandle(markerHandle);
}

int Document::MarkerNumberFromLine(Sci::Line line, int which) const noexcept {
	return Markers()->NumberFromLine(line, which);
}

int Document::MarkerHandleFromLine(Sci::Line line, int which) const noexcept {
	return Markers()->HandleFromLine(line, which);
}

Sci_Position SCI_METHOD Document::LineStart(Sci_Line line) const noexcept {
	return cb.LineStart(line);
}

bool Document::IsLineStartPosition(Sci::Position position) const noexcept {
	return LineStart(LineFromPosition(position)) == position;
}

Sci_Position SCI_METHOD Document::LineEnd(Sci_Line line) const noexcept {
	Sci::Position position = LineStart(line + 1);
	if (line < LinesTotal() - 1) {
		if (cb.GetLineEndTypes()) {
			const unsigned char bytes[] = {
				cb.UCharAt(position - 3),
				cb.UCharAt(position - 2),
				cb.UCharAt(position - 1),
			};
			if (UTF8IsSeparator(bytes)) {
				return position - UTF8SeparatorLength;
			}
			if (UTF8IsNEL(bytes + 1)) {
				return position - UTF8NELLength;
			}
		}
		position--; // Back over CR or LF
		// When line terminator is CR+LF, may need to go back one more
		if ((position > LineStart(line)) && (cb.CharAt(position - 1) == '\r')) {
			position--;
		}
	}
	return position;
}

void SCI_METHOD Document::SetErrorStatus(int status) noexcept {
	// Tell the watchers an error has occurred.
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifyErrorOccurred(this, watcher.userData, status);
	}
}

Sci_Line SCI_METHOD Document::LineFromPosition(Sci_Position pos) const noexcept {
	return cb.LineFromPosition(pos);
}

Sci::Line Document::SciLineFromPosition(Sci::Position pos) const noexcept {
	// Avoids casting in callers for this very common function
	return cb.LineFromPosition(pos);
}

Sci::Position Document::LineEndPosition(Sci::Position position) const noexcept {
	return LineEnd(LineFromPosition(position));
}

bool Document::IsLineEndPosition(Sci::Position position) const noexcept {
	return LineEnd(LineFromPosition(position)) == position;
}

bool Document::IsPositionInLineEnd(Sci::Position position) const noexcept {
	return position >= LineEnd(LineFromPosition(position));
}

Sci::Position Document::VCHomePosition(Sci::Position position) const noexcept {
	const Sci::Line line = SciLineFromPosition(position);
	const Sci::Position startPosition = LineStart(line);
	const Sci::Position endLine = LineEnd(line);
	Sci::Position startText = startPosition;
	while (startText < endLine && (cb.CharAt(startText) == ' ' || cb.CharAt(startText) == '\t')) {
		startText++;
	}
	if (position == startText)
		return startPosition;
	else
		return startText;
}

Sci::Position Document::IndexLineStart(Sci::Line line, int lineCharacterIndex) const noexcept {
	return cb.IndexLineStart(line, lineCharacterIndex);
}

Sci::Line Document::LineFromPositionIndex(Sci::Position pos, int lineCharacterIndex) const noexcept {
	return cb.LineFromPositionIndex(pos, lineCharacterIndex);
}

#if ActionDuration_MeasureTimeByBytes
Sci::Line Document::LineFromPositionAfter(Sci::Line line, Sci::Position length) const noexcept {
	const Sci::Position pos = LineStart(line) + length;
	if (pos >= Length()) {
		return LinesTotal();
	}
	const Sci::Line lineLast = SciLineFromPosition(pos);
	return lineLast + (!!(line == lineLast));
}
#endif

int SCI_METHOD Document::SetLevel(Sci_Line line, int level) {
	const int prev = Levels()->SetLevel(line, level, LinesTotal());
	if (prev != level) {
		DocModification mh(SC_MOD_CHANGEFOLD | SC_MOD_CHANGEMARKER,
			LineStart(line), 0, 0, nullptr, line);
		mh.foldLevelNow = level;
		mh.foldLevelPrev = prev;
		NotifyModified(mh);
	}
	return prev;
}

int SCI_METHOD Document::GetLevel(Sci_Line line) const noexcept {
	return Levels()->GetLevel(line);
}

void Document::ClearLevels() {
	Levels()->ClearLevels();
}

static bool IsSubordinate(int levelStart, int levelTry) noexcept {
	if (levelTry & SC_FOLDLEVELWHITEFLAG)
		return true;
	else
		return LevelNumber(levelStart) < LevelNumber(levelTry);
}

Sci::Line Document::GetLastChild(Sci::Line lineParent, int level, Sci::Line lastLine) {
	if (level == -1)
		level = LevelNumber(GetLevel(lineParent));
	const Sci::Line maxLine = LinesTotal();
	const Sci::Line lookLastLine = (lastLine != -1) ? std::min(LinesTotal() - 1, lastLine) : -1;
	Sci::Line lineMaxSubord = lineParent;
	while (lineMaxSubord < maxLine - 1) {
		EnsureStyledTo(LineStart(lineMaxSubord + 2));
		if (!IsSubordinate(level, GetLevel(lineMaxSubord + 1)))
			break;
		if ((lookLastLine != -1) && (lineMaxSubord >= lookLastLine) && !(GetLevel(lineMaxSubord) & SC_FOLDLEVELWHITEFLAG))
			break;
		lineMaxSubord++;
	}
	if (lineMaxSubord > lineParent) {
		if (level > LevelNumber(GetLevel(lineMaxSubord + 1))) {
			// Have chewed up some whitespace that belongs to a parent so seek back
			if (GetLevel(lineMaxSubord) & SC_FOLDLEVELWHITEFLAG) {
				lineMaxSubord--;
			}
		}
	}
	return lineMaxSubord;
}

Sci::Line Document::GetFoldParent(Sci::Line line) const noexcept {
	const int level = LevelNumber(GetLevel(line));
	Sci::Line lineLook = line - 1;
	while ((lineLook > 0) && (
		(!(GetLevel(lineLook) & SC_FOLDLEVELHEADERFLAG)) ||
		(LevelNumber(GetLevel(lineLook)) >= level))
		) {
		lineLook--;
	}
	if ((GetLevel(lineLook) & SC_FOLDLEVELHEADERFLAG) &&
		(LevelNumber(GetLevel(lineLook)) < level)) {
		return lineLook;
	} else {
		return -1;
	}
}

void Document::GetHighlightDelimiters(HighlightDelimiter &highlightDelimiter, Sci::Line line, Sci::Line lastLine) {
	const int level = GetLevel(line);
	const Sci::Line lookLastLine = std::max(line, lastLine) + 1;

	Sci::Line lookLine = line;
	int lookLineLevel = level;
	int lookLineLevelNum = LevelNumber(lookLineLevel);
	while ((lookLine > 0) && ((lookLineLevel & SC_FOLDLEVELWHITEFLAG) ||
		((lookLineLevel & SC_FOLDLEVELHEADERFLAG) && (lookLineLevelNum >= LevelNumber(GetLevel(lookLine + 1)))))) {
		lookLineLevel = GetLevel(--lookLine);
		lookLineLevelNum = LevelNumber(lookLineLevel);
	}

	Sci::Line beginFoldBlock = (lookLineLevel & SC_FOLDLEVELHEADERFLAG) ? lookLine : GetFoldParent(lookLine);
	if (beginFoldBlock == -1) {
		highlightDelimiter.Clear();
		return;
	}

	Sci::Line endFoldBlock = GetLastChild(beginFoldBlock, -1, lookLastLine);
	Sci::Line firstChangeableLineBefore = -1;
	if (endFoldBlock < line) {
		lookLine = beginFoldBlock - 1;
		lookLineLevel = GetLevel(lookLine);
		lookLineLevelNum = LevelNumber(lookLineLevel);
		while ((lookLine >= 0) && (lookLineLevelNum >= SC_FOLDLEVELBASE)) {
			if (lookLineLevel & SC_FOLDLEVELHEADERFLAG) {
				if (GetLastChild(lookLine, -1, lookLastLine) == line) {
					beginFoldBlock = lookLine;
					endFoldBlock = line;
					firstChangeableLineBefore = line - 1;
				}
			}
			if ((lookLine > 0) && (lookLineLevelNum == SC_FOLDLEVELBASE) && (LevelNumber(GetLevel(lookLine - 1)) > lookLineLevelNum))
				break;
			lookLineLevel = GetLevel(--lookLine);
			lookLineLevelNum = LevelNumber(lookLineLevel);
		}
	}
	if (firstChangeableLineBefore == -1) {
		for (lookLine = line - 1, lookLineLevel = GetLevel(lookLine), lookLineLevelNum = LevelNumber(lookLineLevel);
			lookLine >= beginFoldBlock;
			lookLineLevel = GetLevel(--lookLine), lookLineLevelNum = LevelNumber(lookLineLevel)) {
			if ((lookLineLevel & SC_FOLDLEVELWHITEFLAG) || (lookLineLevelNum > LevelNumber(level))) {
				firstChangeableLineBefore = lookLine;
				break;
			}
		}
	}
	if (firstChangeableLineBefore == -1)
		firstChangeableLineBefore = beginFoldBlock - 1;

	Sci::Line firstChangeableLineAfter = -1;
	for (lookLine = line + 1, lookLineLevel = GetLevel(lookLine), lookLineLevelNum = LevelNumber(lookLineLevel);
		lookLine <= endFoldBlock;
		lookLineLevel = GetLevel(++lookLine), lookLineLevelNum = LevelNumber(lookLineLevel)) {
		if ((lookLineLevel & SC_FOLDLEVELHEADERFLAG) && (lookLineLevelNum < LevelNumber(GetLevel(lookLine + 1)))) {
			firstChangeableLineAfter = lookLine;
			break;
		}
	}
	if (firstChangeableLineAfter == -1)
		firstChangeableLineAfter = endFoldBlock + 1;

	highlightDelimiter.beginFoldBlock = beginFoldBlock;
	highlightDelimiter.endFoldBlock = endFoldBlock;
	highlightDelimiter.firstChangeableLineBefore = firstChangeableLineBefore;
	highlightDelimiter.firstChangeableLineAfter = firstChangeableLineAfter;
}

Sci::Position Document::ClampPositionIntoDocument(Sci::Position pos) const noexcept {
	return std::clamp<Sci::Position>(pos, 0, Length());
}

bool Document::IsCrLf(Sci::Position pos) const noexcept {
	if (pos < 0)
		return false;
	if (pos >= (Length() - 1))
		return false;
	return (cb.CharAt(pos) == '\r') && (cb.CharAt(pos + 1) == '\n');
}

int Document::LenChar(Sci::Position pos, bool *invalid) noexcept {
	if (pos < 0 || pos >= Length()) {
		return 1;
	} else if (IsCrLf(pos)) {
		return 2;
	}

	const unsigned char leadByte = cb.UCharAt(pos);
	if (!dbcsCodePage || UTF8IsAscii(leadByte)) {
		// Common case: ASCII character
		return 1;
	}
	if (SC_CP_UTF8 == dbcsCodePage) {
		const int widthCharBytes = UTF8BytesOfLead(leadByte);
		unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
		for (int b = 1; b < widthCharBytes; b++) {
			charBytes[b] = cb.UCharAt(pos + b);
		}
		const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
		if (utf8status & UTF8MaskInvalid) {
			// Treat as invalid and use up just one byte
			if (invalid) {
				*invalid = true;
			}
			return 1;
		} else {
			return utf8status & UTF8MaskWidth;
		}
	} else {
		const bool lead = IsDBCSLeadByteNoExcept(leadByte);
		if (lead && ((pos + 1) < Length())) {
			return 2;
		} else {
			if (invalid) {
				*invalid = lead;
			}
			return 1;
		}
	}
}

bool Document::InGoodUTF8(Sci::Position pos, Sci::Position &start, Sci::Position &end) const noexcept {
	Sci::Position trail = pos;
	while ((trail > 0) && (pos - trail < UTF8MaxBytes) && UTF8IsTrailByte(cb.CharAt(trail - 1))) {
		trail--;
	}
	start = (trail > 0) ? trail - 1 : trail;

	const unsigned char leadByte = cb.UCharAt(start);
	const int widthCharBytes = UTF8BytesOfLead(leadByte);
	if (widthCharBytes == 1) {
		return false;
	} else {
		const int trailBytes = widthCharBytes - 1;
		const Sci::Position len = pos - start;
		if (len > trailBytes)
			// pos too far from lead
			return false;
		unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
		for (Sci::Position b = 1; b < widthCharBytes && ((start + b) < cb.Length()); b++) {
			charBytes[b] = cb.CharAt(start + b);
		}
		const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
		if (utf8status & UTF8MaskInvalid)
			return false;
		end = start + widthCharBytes;
		return true;
	}
}

// Normalise a position so that it is not halfway through a two byte character.
// This can occur in two situations -
// When lines are terminated with \r\n pairs which should be treated as one character.
// When displaying DBCS text such as Japanese.
// If moving, move the position in the indicated direction.
Sci::Position Document::MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir, bool checkLineEnd) const noexcept {
	//Platform::DebugPrintf("NoCRLF %d %d\n", pos, moveDir);
	// If out of range, just return minimum/maximum value.
	if (pos <= 0)
		return 0;
	if (pos >= Length())
		return Length();

	// PLATFORM_ASSERT(pos > 0 && pos < Length());
	if (checkLineEnd && IsCrLf(pos - 1)) {
		if (moveDir > 0)
			return pos + 1;
		else
			return pos - 1;
	}

	if (dbcsCodePage) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			const unsigned char ch = cb.UCharAt(pos);
			// If ch is not a trail byte then pos is valid intercharacter position
			if (UTF8IsTrailByte(ch)) {
				Sci::Position startUTF = pos;
				Sci::Position endUTF = pos;
				if (InGoodUTF8(pos, startUTF, endUTF)) {
					// ch is a trail byte within a UTF-8 character
					if (moveDir > 0)
						pos = endUTF;
					else
						pos = startUTF;
				}
				// Else invalid UTF-8 so return position of isolated trail byte
			}
		} else {
			// Anchor DBCS calculations at start of line because start of line can
			// not be a DBCS trail byte.
			const Sci::Position posStartLine = LineStart(LineFromPosition(pos));
			if (pos == posStartLine)
				return pos;

			// Step back until a non-lead-byte is found.
			Sci::Position posCheck = pos;
			while ((posCheck > posStartLine) && IsDBCSLeadByteNoExcept(cb.CharAt(posCheck - 1))) {
				posCheck--;
			}

			// Check from known start of character.
			while (posCheck < pos) {
				const int mbsize = IsDBCSLeadByteNoExcept(cb.CharAt(posCheck)) ? 2 : 1;
				if (posCheck + mbsize == pos) {
					return pos;
				} else if (posCheck + mbsize > pos) {
					if (moveDir > 0) {
						return posCheck + mbsize;
					} else {
						return posCheck;
					}
				}
				posCheck += mbsize;
			}
		}
	}

	return pos;
}

// NextPosition moves between valid positions - it can not handle a position in the middle of a
// multi-byte character. It is used to iterate through text more efficiently than MovePositionOutsideChar.
// A \r\n pair is treated as two characters.
Sci::Position Document::NextPosition(Sci::Position pos, int moveDir) const noexcept {
	// If out of range, just return minimum/maximum value.
	const int increment = (moveDir > 0) ? 1 : -1;
	if (pos + increment <= 0)
		return 0;
	if (pos + increment >= cb.Length())
		return cb.Length();

	if (dbcsCodePage) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			if (increment == 1) {
				// Simple forward movement case so can avoid some checks
				const unsigned char leadByte = cb.UCharAt(pos);
				if (UTF8IsAscii(leadByte)) {
					// Single byte character or invalid
					pos++;
				} else {
					const int widthCharBytes = UTF8BytesOfLead(leadByte);
					unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0 , 0, 0 };
					for (int b = 1; b < widthCharBytes; b++) {
						charBytes[b] = cb.CharAt(pos + b);
					}
					const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
					if (utf8status & UTF8MaskInvalid)
						pos++;
					else
						pos += utf8status & UTF8MaskWidth;
				}
			} else {
				// Examine byte before position
				pos--;
				const unsigned char ch = cb.UCharAt(pos);
				// If ch is not a trail byte then pos is valid intercharacter position
				if (UTF8IsTrailByte(ch)) {
					// If ch is a trail byte in a valid UTF-8 character then return start of character
					Sci::Position startUTF = pos;
					Sci::Position endUTF = pos;
					if (InGoodUTF8(pos, startUTF, endUTF)) {
						pos = startUTF;
					}
					// Else invalid UTF-8 so return position of isolated trail byte
				}
			}
		} else {
			if (moveDir > 0) {
				const int mbsize = IsDBCSLeadByteNoExcept(cb.CharAt(pos)) ? 2 : 1;
				pos += mbsize;
				if (pos > cb.Length())
					pos = cb.Length();
			} else {
				// Anchor DBCS calculations at start of line because start of line can
				// not be a DBCS trail byte.
				const Sci::Position posStartLine = cb.LineStart(cb.LineFromPosition(pos));
				// How to Go Backward in a DBCS String
				// https://msdn.microsoft.com/en-us/library/cc194792.aspx
				// DBCS-Enabled Programs vs. Non-DBCS-Enabled Programs
				// https://msdn.microsoft.com/en-us/library/cc194790.aspx
				if ((pos - 1) <= posStartLine) {
					return pos - 1;
				} else if (IsDBCSLeadByteNoExcept(cb.CharAt(pos - 1))) {
					// Must actually be trail byte
					return pos - 2;
				} else {
					// Otherwise, step back until a non-lead-byte is found.
					Sci::Position posTemp = pos - 1;
					while (posStartLine <= --posTemp && IsDBCSLeadByteNoExcept(cb.CharAt(posTemp))) {
					}
					// Now posTemp+1 must point to the beginning of a character,
					// so figure out whether we went back an even or an odd
					// number of bytes and go back 1 or 2 bytes, respectively.
					return (pos - 1 - ((pos - posTemp) & 1));
				}
			}
		}
	} else {
		pos += increment;
	}

	return pos;
}

bool Document::NextCharacter(Sci::Position &pos, int moveDir) const noexcept {
	// Returns true if pos changed
	Sci::Position posNext = NextPosition(pos, moveDir);
	if (posNext == pos) {
		return false;
	} else {
		pos = posNext;
		return true;
	}
}

Document::CharacterExtracted Document::CharacterAfter(Sci::Position position) const noexcept {
	if (position >= Length()) {
		return CharacterExtracted(unicodeReplacementChar, 0);
	}
	const unsigned char leadByte = cb.UCharAt(position);
	if (!dbcsCodePage || UTF8IsAscii(leadByte)) {
		// Common case: ASCII character
		return CharacterExtracted(leadByte, 1);
	}
	if (SC_CP_UTF8 == dbcsCodePage) {
		const int widthCharBytes = UTF8BytesOfLead(leadByte);
		unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
		for (int b = 1; b < widthCharBytes; b++) {
			charBytes[b] = cb.UCharAt(position + b);
		}
		const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
		if (utf8status & UTF8MaskInvalid) {
			// Treat as invalid and use up just one byte
			return CharacterExtracted(unicodeReplacementChar, 1);
		} else {
			return CharacterExtracted(UnicodeFromUTF8(charBytes), utf8status & UTF8MaskWidth);
		}
	} else {
		if (IsDBCSLeadByteNoExcept(leadByte) && ((position + 1) < Length())) {
			return CharacterExtracted::DBCS(leadByte, cb.UCharAt(position + 1));
		} else {
			return CharacterExtracted(leadByte, 1);
		}
	}
}

Document::CharacterExtracted Document::CharacterBefore(Sci::Position position) const noexcept {
	if (position <= 0) {
		return CharacterExtracted(unicodeReplacementChar, 0);
	}
	const unsigned char previousByte = cb.UCharAt(position - 1);
	if (0 == dbcsCodePage) {
		return CharacterExtracted(previousByte, 1);
	}
	if (SC_CP_UTF8 == dbcsCodePage) {
		if (UTF8IsAscii(previousByte)) {
			return CharacterExtracted(previousByte, 1);
		}
		position--;
		// If previousByte is not a trail byte then its invalid
		if (UTF8IsTrailByte(previousByte)) {
			// If previousByte is a trail byte in a valid UTF-8 character then find start of character
			Sci::Position startUTF = position;
			Sci::Position endUTF = position;
			if (InGoodUTF8(position, startUTF, endUTF)) {
				const Sci::Position widthCharBytes = endUTF - startUTF;
				unsigned char charBytes[UTF8MaxBytes] = { 0, 0, 0, 0 };
				for (Sci::Position b = 0; b < widthCharBytes; b++) {
					charBytes[b] = cb.UCharAt(startUTF + b);
				}
				const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
				if (utf8status & UTF8MaskInvalid) {
					// Treat as invalid and use up just one byte
					return CharacterExtracted(unicodeReplacementChar, 1);
				} else {
					return CharacterExtracted(UnicodeFromUTF8(charBytes), utf8status & UTF8MaskWidth);
				}
			}
			// Else invalid UTF-8 so return position of isolated trail byte
		}
		return CharacterExtracted(unicodeReplacementChar, 1);
	} else {
		// Moving backwards in DBCS is complex so use NextPosition
		const Sci::Position posStartCharacter = NextPosition(position, -1);
		return CharacterAfter(posStartCharacter);
	}
}

// Return -1  on out-of-bounds
Sci_Position SCI_METHOD Document::GetRelativePosition(Sci_Position positionStart, Sci_Position characterOffset) const noexcept {
	Sci::Position pos = positionStart;
	if (dbcsCodePage) {
		const int increment = (characterOffset > 0) ? 1 : -1;
		while (characterOffset != 0) {
			const Sci::Position posNext = NextPosition(pos, increment);
			if (posNext == pos)
				return INVALID_POSITION;
			pos = posNext;
			characterOffset -= increment;
		}
	} else {
		pos = positionStart + characterOffset;
		if ((pos < 0) || (pos > Length()))
			return INVALID_POSITION;
	}
	return pos;
}

Sci::Position Document::GetRelativePositionUTF16(Sci::Position positionStart, Sci::Position characterOffset) const noexcept {
	Sci::Position pos = positionStart;
	if (dbcsCodePage) {
		const int increment = (characterOffset > 0) ? 1 : -1;
		while (characterOffset != 0) {
			const Sci::Position posNext = NextPosition(pos, increment);
			if (posNext == pos)
				return INVALID_POSITION;
			if (std::abs(pos - posNext) > 3)	// 4 byte character = 2*UTF16.
				characterOffset -= increment;
			pos = posNext;
			characterOffset -= increment;
		}
	} else {
		pos = positionStart + characterOffset;
		if ((pos < 0) || (pos > Length()))
			return INVALID_POSITION;
	}
	return pos;
}

int SCI_METHOD Document::GetCharacterAndWidth(Sci_Position position, Sci_Position *pWidth) const noexcept {
	int character;
	int bytesInCharacter = 1;
	const unsigned char leadByte = cb.UCharAt(position);
	if (dbcsCodePage) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			if (UTF8IsAscii(leadByte)) {
				// Single byte character or invalid
				character = leadByte;
			} else {
				const int widthCharBytes = UTF8BytesOfLead(leadByte);
				unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
				for (int b = 1; b < widthCharBytes; b++) {
					charBytes[b] = cb.UCharAt(position + b);
				}
				const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
				if (utf8status & UTF8MaskInvalid) {
					// Report as singleton surrogate values which are invalid Unicode
					character = 0xDC80 + leadByte;
				} else {
					bytesInCharacter = utf8status & UTF8MaskWidth;
					character = UnicodeFromUTF8(charBytes);
				}
			}
		} else {
			if (IsDBCSLeadByteNoExcept(leadByte)) {
				bytesInCharacter = 2;
				character = (leadByte << 8) | cb.UCharAt(position + 1);
			} else {
				character = leadByte;
			}
		}
	} else {
		character = leadByte;
	}
	if (pWidth) {
		*pWidth = bytesInCharacter;
	}
	return character;
}

int SCI_METHOD Document::CodePage() const noexcept {
	return dbcsCodePage;
}

bool SCI_METHOD Document::IsDBCSLeadByte(unsigned char ch) const noexcept {
	// Used by lexers so must match IDocument method exactly
	return dbcsCharClass && dbcsCharClass->IsLeadByte(ch);
}

bool Document::IsDBCSLeadByteNoExcept(unsigned char ch) const noexcept {
	return dbcsCharClass->IsLeadByte(ch);
}

bool Document::IsDBCSLeadByteInvalid(unsigned char ch) const noexcept {
	return dbcsCharClass->IsLeadByteInvalid(ch);
}

bool Document::IsDBCSTrailByteInvalid(unsigned char ch) const noexcept {
	return dbcsCharClass->IsTrailByteInvalid(ch);
}

int Document::DBCSDrawBytes(std::string_view text) const noexcept {
	if (text.length() <= 1) {
		return static_cast<int>(text.length());
	}
	if (IsDBCSLeadByteNoExcept(text[0])) {
		return IsDBCSTrailByteInvalid(text[1]) ? 1 : 2;
	} else {
		return 1;
	}
}

static constexpr bool IsSpaceOrTab(int ch) noexcept {
	return ch == ' ' || ch == '\t';
}

// Need to break text into segments near lengthSegment but taking into
// account the encoding to not break inside a UTF-8 or DBCS character
// and also trying to avoid breaking inside a pair of combining characters.
// The segment length must always be long enough (more than 4 bytes)
// so that there will be at least one whole character to make a segment.
// For UTF-8, text must consist only of valid whole characters.
// In preference order from best to worst:
//   1) Break after space
//   2) Break before punctuation
//   3) Break after whole character

int Document::SafeSegment(const char *text, int length, int lengthSegment) const noexcept {
	if (length <= lengthSegment)
		return length;
	int lastSpaceBreak = -1;
	int lastPunctuationBreak = -1;
	int lastEncodingAllowedBreak = 0;
	for (int j = 0; j < lengthSegment;) {
		const unsigned char ch = text[j];
		if (j > 0) {
			if (IsSpaceOrTab(text[j - 1]) && !IsSpaceOrTab(text[j])) {
				lastSpaceBreak = j;
			}
		}

		lastEncodingAllowedBreak = j;
		if (!dbcsCodePage || UTF8IsAscii(ch)) {
			if (j > 0 && charClass.GetClass(ch) == CharacterClass::punctuation) {
				lastPunctuationBreak = j;
			}
			j++;
		} else if (dbcsCodePage == SC_CP_UTF8) {
			j += UTF8BytesOfLead(ch);
		} else {
			j += IsDBCSLeadByteNoExcept(ch) ? 2 : 1;
		}
	}
	if (lastSpaceBreak >= 0) {
		return lastSpaceBreak;
	} else if (lastPunctuationBreak >= 0) {
		return lastPunctuationBreak;
	}
	return lastEncodingAllowedBreak;
}

EncodingFamily Document::CodePageFamily() const noexcept {
	if (SC_CP_UTF8 == dbcsCodePage)
		return EncodingFamily::unicode;
	else if (dbcsCodePage)
		return EncodingFamily::dbcs;
	else
		return EncodingFamily::eightBit;
}

void Document::ModifiedAt(Sci::Position pos) noexcept {
	if (endStyled > pos)
		endStyled = pos;
}

void Document::CheckReadOnly() noexcept {
	if (cb.IsReadOnly() && enteredReadOnlyCount == 0) {
		enteredReadOnlyCount++;
		NotifyModifyAttempt();
		enteredReadOnlyCount--;
	}
}

// Document only modified by gateways DeleteChars, InsertString, Undo, Redo, and SetStyleAt.
// SetStyleAt does not change the persistent state of a document

bool Document::DeleteChars(Sci::Position pos, Sci::Position len) {
	if (pos < 0)
		return false;
	if (len <= 0)
		return false;
	if ((pos + len) > Length())
		return false;
	CheckReadOnly();
	if (enteredModification != 0) {
		return false;
	} else {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			NotifyModified(
				DocModification(
					SC_MOD_BEFOREDELETE | SC_PERFORMED_USER,
					pos, len,
					0, nullptr));
			const Sci::Line prevLinesTotal = LinesTotal();
			const bool startSavePoint = cb.IsSavePoint();
			bool startSequence = false;
			const char *text = cb.DeleteChars(pos, len, startSequence);
			if (startSavePoint && cb.IsCollectingUndo())
				NotifySavePoint(false);
			if ((pos < Length()) || (pos == 0))
				ModifiedAt(pos);
			else
				ModifiedAt(pos - 1);
			NotifyModified(
				DocModification(
					SC_MOD_DELETETEXT | SC_PERFORMED_USER | (startSequence ? SC_STARTACTION : 0),
					pos, len,
					LinesTotal() - prevLinesTotal, text));
		}
		enteredModification--;
	}
	return !cb.IsReadOnly();
}

namespace {

struct WithoutPerLine {
	CellBuffer *cb;
	PerLine *pl;
	constexpr WithoutPerLine(CellBuffer *cb_, PerLine *pl_) noexcept : cb(cb_), pl(pl_) {}
	const char *InsertString(Sci::Position position, const char *s, Sci::Position insertLength, bool &startSequence) const {
		cb->SetPerLine(nullptr);
		return cb->InsertString(position, s, insertLength, startSequence);
	}
	~WithoutPerLine() {
		cb->SetPerLine(pl);
	}
};

}

/**
 * Insert a string with a length.
 */
Sci::Position Document::InsertString(Sci::Position position, const char *s, Sci::Position insertLength) {
	if (insertLength <= 0) {
		return 0;
	}
	CheckReadOnly();	// Application may change read only state here
	if (cb.IsReadOnly()) {
		return 0;
	}
	if (enteredModification != 0) {
		return 0;
	}
	enteredModification++;
	insertionSet = false;
	insertion.clear();
	NotifyModified(
		DocModification(
			SC_MOD_INSERTCHECK,
			position, insertLength,
			0, s));
	if (insertionSet) {
		s = insertion.c_str();
		insertLength = insertion.length();
	}
	NotifyModified(
		DocModification(
			SC_MOD_BEFOREINSERT | SC_PERFORMED_USER,
			position, insertLength,
			0, s));
	const Sci::Line prevLinesTotal = LinesTotal();
	const bool startSavePoint = cb.IsSavePoint();
	bool startSequence = false;
#if InsertString_WithoutPerLine
	const char *text = nullptr;
	if (insertLength > InsertString_WithoutPerLine && !IsActive()) {
		// avoid calling InsertLine() or RemoveLine()
		text = WithoutPerLine(&cb, this).InsertString(position, s, insertLength, startSequence);
	} else {
		text = cb.InsertString(position, s, insertLength, startSequence);
	}
#else
	const char *text = cb.InsertString(position, s, insertLength, startSequence);
#endif
	if (startSavePoint && cb.IsCollectingUndo())
		NotifySavePoint(false);
	ModifiedAt(position);
	NotifyModified(
		DocModification(
			SC_MOD_INSERTTEXT | SC_PERFORMED_USER | (startSequence ? SC_STARTACTION : 0),
			position, insertLength,
			LinesTotal() - prevLinesTotal, text));
	if (insertionSet) {	// Free memory as could be large
		std::string().swap(insertion);
	}
	enteredModification--;
	return insertLength;
}

void Document::ChangeInsertion(const char *s, Sci::Position length) {
	insertionSet = true;
	insertion.assign(s, length);
}

int SCI_METHOD Document::AddData(const char *data, Sci_Position length) {
	try {
		const Sci::Position position = Length();
		InsertString(position, data, length);
	} catch (std::bad_alloc &) {
		return SC_STATUS_BADALLOC;
	} catch (...) {
		return SC_STATUS_FAILURE;
	}
	return 0;
}

void * SCI_METHOD Document::ConvertToDocument() noexcept {
	return this;
}

Sci::Position Document::Undo() {
	Sci::Position newPos = -1;
	CheckReadOnly();
	if ((enteredModification == 0) && (cb.IsCollectingUndo())) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			const bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			const int steps = cb.StartUndo();
			//Platform::DebugPrintf("Steps=%d\n", steps);
			Sci::Position coalescedRemovePos = -1;
			Sci::Position coalescedRemoveLen = 0;
			Sci::Position prevRemoveActionPos = -1;
			Sci::Position prevRemoveActionLen = 0;
			for (int step = 0; step < steps; step++) {
				const Sci::Line prevLinesTotal = LinesTotal();
				const Action &action = cb.GetUndoStep();
				if (action.at == ActionType::remove) {
					NotifyModified(DocModification(
						SC_MOD_BEFOREINSERT | SC_PERFORMED_UNDO, action));
				} else if (action.at == ActionType::container) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_UNDO);
					dm.token = action.position;
					NotifyModified(dm);
					if (!action.mayCoalesce) {
						coalescedRemovePos = -1;
						coalescedRemoveLen = 0;
						prevRemoveActionPos = -1;
						prevRemoveActionLen = 0;
					}
				} else {
					NotifyModified(DocModification(
						SC_MOD_BEFOREDELETE | SC_PERFORMED_UNDO, action));
				}
				cb.PerformUndoStep();
				if (action.at != ActionType::container) {
					ModifiedAt(action.position);
					newPos = action.position;
				}

				int modFlags = SC_PERFORMED_UNDO;
				// With undo, an insertion action becomes a deletion notification
				if (action.at == ActionType::remove) {
					newPos += action.lenData;
					modFlags |= SC_MOD_INSERTTEXT;
					if ((coalescedRemoveLen > 0) &&
						(action.position == prevRemoveActionPos || action.position == (prevRemoveActionPos + prevRemoveActionLen))) {
						coalescedRemoveLen += action.lenData;
						newPos = coalescedRemovePos + coalescedRemoveLen;
					} else {
						coalescedRemovePos = action.position;
						coalescedRemoveLen = action.lenData;
					}
					prevRemoveActionPos = action.position;
					prevRemoveActionLen = action.lenData;
				} else if (action.at == ActionType::insert) {
					modFlags |= SC_MOD_DELETETEXT;
					coalescedRemovePos = -1;
					coalescedRemoveLen = 0;
					prevRemoveActionPos = -1;
					prevRemoveActionLen = 0;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Line linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(DocModification(modFlags, action.position, action.lenData,
					linesAdded, action.data.get()));
			}

			const bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint)
				NotifySavePoint(endSavePoint);
		}
		enteredModification--;
	}
	return newPos;
}

Sci::Position Document::Redo() {
	Sci::Position newPos = -1;
	CheckReadOnly();
	if ((enteredModification == 0) && (cb.IsCollectingUndo())) {
		enteredModification++;
		if (!cb.IsReadOnly()) {
			const bool startSavePoint = cb.IsSavePoint();
			bool multiLine = false;
			const int steps = cb.StartRedo();
			for (int step = 0; step < steps; step++) {
				const Sci::Line prevLinesTotal = LinesTotal();
				const Action &action = cb.GetRedoStep();
				if (action.at == ActionType::insert) {
					NotifyModified(DocModification(
						SC_MOD_BEFOREINSERT | SC_PERFORMED_REDO, action));
				} else if (action.at == ActionType::container) {
					DocModification dm(SC_MOD_CONTAINER | SC_PERFORMED_REDO);
					dm.token = action.position;
					NotifyModified(dm);
				} else {
					NotifyModified(DocModification(
						SC_MOD_BEFOREDELETE | SC_PERFORMED_REDO, action));
				}
				cb.PerformRedoStep();
				if (action.at != ActionType::container) {
					ModifiedAt(action.position);
					newPos = action.position;
				}

				int modFlags = SC_PERFORMED_REDO;
				if (action.at == ActionType::insert) {
					newPos += action.lenData;
					modFlags |= SC_MOD_INSERTTEXT;
				} else if (action.at == ActionType::remove) {
					modFlags |= SC_MOD_DELETETEXT;
				}
				if (steps > 1)
					modFlags |= SC_MULTISTEPUNDOREDO;
				const Sci::Line linesAdded = LinesTotal() - prevLinesTotal;
				if (linesAdded != 0)
					multiLine = true;
				if (step == steps - 1) {
					modFlags |= SC_LASTSTEPINUNDOREDO;
					if (multiLine)
						modFlags |= SC_MULTILINEUNDOREDO;
				}
				NotifyModified(
					DocModification(modFlags, action.position, action.lenData,
						linesAdded, action.data.get()));
			}

			const bool endSavePoint = cb.IsSavePoint();
			if (startSavePoint != endSavePoint)
				NotifySavePoint(endSavePoint);
		}
		enteredModification--;
	}
	return newPos;
}

void Document::DelChar(Sci::Position pos) {
	DeleteChars(pos, LenChar(pos));
}

void Document::DelCharBack(Sci::Position pos) {
	if (pos <= 0) {
		return;
	} else if (IsCrLf(pos - 2)) {
		DeleteChars(pos - 2, 2);
	} else if (dbcsCodePage) {
		const Sci::Position startChar = NextPosition(pos, -1);
		DeleteChars(startChar, pos - startChar);
	} else {
		DeleteChars(pos - 1, 1);
	}
}

static constexpr Sci::Position NextTab(Sci::Position pos, Sci::Position tabSize) noexcept {
	return ((pos / tabSize) + 1) * tabSize;
}

static std::string CreateIndentation(Sci::Position indent, int tabSize, bool insertSpaces) {
	std::string indentation;
	if (!insertSpaces) {
		while (indent >= tabSize) {
			indentation += '\t';
			indent -= tabSize;
		}
	}
	while (indent > 0) {
		indentation += ' ';
		indent--;
	}
	return indentation;
}

int SCI_METHOD Document::GetLineIndentation(Sci_Line line) const noexcept {
	int indent = 0;
	if ((line >= 0) && (line < LinesTotal())) {
		const Sci::Position lineStart = LineStart(line);
		const Sci::Position length = Length();
		for (Sci::Position i = lineStart; i < length; i++) {
			const char ch = cb.CharAt(i);
			if (ch == ' ')
				indent++;
			else if (ch == '\t')
				indent = static_cast<int>(NextTab(indent, tabInChars));
			else
				return indent;
		}
	}
	return indent;
}

Sci::Position Document::SetLineIndentation(Sci::Line line, Sci::Position indent) {
	const int indentOfLine = GetLineIndentation(line);
	indent = std::max<Sci::Position>(indent, 0);
	if (indent != indentOfLine) {
		std::string linebuf = CreateIndentation(indent, tabInChars, !useTabs);
		const Sci::Position thisLineStart = LineStart(line);
		const Sci::Position indentPos = GetLineIndentPosition(line);
		UndoGroup ug(this);
		DeleteChars(thisLineStart, indentPos - thisLineStart);
		return thisLineStart + InsertString(thisLineStart, linebuf.c_str(),
			linebuf.length());
	} else {
		return GetLineIndentPosition(line);
	}
}

Sci::Position Document::GetLineIndentPosition(Sci::Line line) const noexcept {
	if (line < 0)
		return 0;
	Sci::Position pos = LineStart(line);
	const Sci::Position length = Length();
	while ((pos < length) && IsSpaceOrTab(cb.CharAt(pos))) {
		pos++;
	}
	return pos;
}

Sci::Position Document::GetColumn(Sci::Position pos) noexcept {
	Sci::Position column = 0;
	const Sci::Line line = SciLineFromPosition(pos);
	if ((line >= 0) && (line < LinesTotal())) {
		for (Sci::Position i = LineStart(line); i < pos;) {
			const char ch = cb.CharAt(i);
			if (ch == '\t') {
				column = NextTab(column, tabInChars);
				i++;
			} else if (ch == '\r') {
				return column;
			} else if (ch == '\n') {
				return column;
			} else if (i >= Length()) {
				return column;
			} else {
				column++;
				i = NextPosition(i, 1);
			}
		}
	}
	return column;
}

Sci::Position Document::CountCharacters(Sci::Position startPos, Sci::Position endPos) const noexcept {
	startPos = MovePositionOutsideChar(startPos, 1, false);
	endPos = MovePositionOutsideChar(endPos, -1, false);
	Sci::Position count = 0;
	Sci::Position i = startPos;
	while (i < endPos) {
		count++;
		i = NextPosition(i, 1);
	}
	return count;
}

void Document::CountCharactersAndColumns(Sci_TextToFind *ft) const noexcept {
	const Sci::Position startPos = ft->chrg.cpMin;
	const Sci::Position endPos = ft->chrg.cpMax;
	Sci::Position count = ft->chrgText.cpMin;
	Sci::Position column = ft->chrgText.cpMax;

	Sci::Position i = startPos;
	while (i < endPos) {
		const unsigned char ch = cb.UCharAt(i);
		if (ch == '\t') {
			column = NextTab(column, tabInChars);
			i++;
		} else if (UTF8IsAscii(ch)) {
			column++;
			i++;
		} else {
			column++;
			i = NextPosition(i, 1);
		}
		count++;
	}

	ft->chrgText.cpMin = count;
	ft->chrgText.cpMax = column;
}

Sci::Position Document::CountUTF16(Sci::Position startPos, Sci::Position endPos) const noexcept {
	startPos = MovePositionOutsideChar(startPos, 1, false);
	endPos = MovePositionOutsideChar(endPos, -1, false);
	Sci::Position count = 0;
	Sci::Position i = startPos;
	while (i < endPos) {
		count++;
		const Sci::Position next = NextPosition(i, 1);
		if ((next - i) > 3)
			count++;
		i = next;
	}
	return count;
}

Sci::Position Document::FindColumn(Sci::Line line, Sci::Position column) noexcept {
	Sci::Position position = LineStart(line);
	if ((line >= 0) && (line < LinesTotal())) {
		Sci::Position columnCurrent = 0;
		while ((columnCurrent < column) && (position < Length())) {
			const char ch = cb.CharAt(position);
			if (ch == '\t') {
				columnCurrent = NextTab(columnCurrent, tabInChars);
				if (columnCurrent > column)
					return position;
				position++;
			} else if (ch == '\r') {
				return position;
			} else if (ch == '\n') {
				return position;
			} else {
				columnCurrent++;
				position = NextPosition(position, 1);
			}
		}
	}
	return position;
}

void Document::Indent(bool forwards, Sci::Line lineBottom, Sci::Line lineTop) {
	// Dedent - suck white space off the front of the line to dedent by equivalent of a tab
	for (Sci::Line line = lineBottom; line >= lineTop; line--) {
		const Sci::Position indentOfLine = GetLineIndentation(line);
		if (forwards) {
			if (LineStart(line) < LineEnd(line)) {
				SetLineIndentation(line, indentOfLine + IndentSize());
			}
		} else {
			SetLineIndentation(line, indentOfLine - IndentSize());
		}
	}
}

// Convert line endings for a piece of text to a particular mode.
// Stop at len or when a NUL is found.
std::string Document::TransformLineEnds(const char *s, size_t len, int eolModeWanted) {
	std::string dest;
	for (size_t i = 0; (i < len) && (s[i]); i++) {
		if (IsEOLChar(s[i])) {
			if (eolModeWanted == SC_EOL_CR) {
				dest.push_back('\r');
			} else if (eolModeWanted == SC_EOL_LF) {
				dest.push_back('\n');
			} else { // eolModeWanted == SC_EOL_CRLF
				dest.push_back('\r');
				dest.push_back('\n');
			}
			if ((s[i] == '\r') && (i + 1 < len) && (s[i + 1] == '\n')) {
				i++;
			}
		} else {
			dest.push_back(s[i]);
		}
	}
	return dest;
}

void Document::ConvertLineEnds(int eolModeSet) {
	UndoGroup ug(this);

	for (Sci::Position pos = 0; pos < Length(); pos++) {
		if (cb.CharAt(pos) == '\r') {
			if (cb.CharAt(pos + 1) == '\n') {
				// CRLF
				if (eolModeSet == SC_EOL_CR) {
					DeleteChars(pos + 1, 1); // Delete the LF
				} else if (eolModeSet == SC_EOL_LF) {
					DeleteChars(pos, 1); // Delete the CR
				} else {
					pos++;
				}
			} else {
				// CR
				if (eolModeSet == SC_EOL_CRLF) {
					pos += InsertString(pos + 1, "\n", 1); // Insert LF
				} else if (eolModeSet == SC_EOL_LF) {
					pos += InsertString(pos, "\n", 1); // Insert LF
					DeleteChars(pos, 1); // Delete CR
					pos--;
				}
			}
		} else if (cb.CharAt(pos) == '\n') {
			// LF
			if (eolModeSet == SC_EOL_CRLF) {
				pos += InsertString(pos, "\r", 1); // Insert CR
			} else if (eolModeSet == SC_EOL_CR) {
				pos += InsertString(pos, "\r", 1); // Insert CR
				DeleteChars(pos, 1); // Delete LF
				pos--;
			}
		}
	}

}

int Document::Options() const noexcept {
	return (IsLarge() ? SC_DOCUMENTOPTION_TEXT_LARGE : 0) |
		(cb.HasStyles() ? 0 : SC_DOCUMENTOPTION_STYLES_NONE);
}

bool Document::IsWhiteLine(Sci::Line line) const noexcept {
	Sci::Position currentChar = LineStart(line);
	const Sci::Position endLine = LineEnd(line);
	while (currentChar < endLine) {
		if (!IsSpaceOrTab(cb.CharAt(currentChar))) {
			return false;
		}
		++currentChar;
	}
	return true;
}

Sci::Position Document::ParaUp(Sci::Position pos) const noexcept {
	Sci::Line line = SciLineFromPosition(pos);
	line--;
	while (line >= 0 && IsWhiteLine(line)) { // skip empty lines
		line--;
	}
	while (line >= 0 && !IsWhiteLine(line)) { // skip non-empty lines
		line--;
	}
	line++;
	return LineStart(line);
}

Sci::Position Document::ParaDown(Sci::Position pos) const noexcept {
	Sci::Line line = SciLineFromPosition(pos);
	while (line < LinesTotal() && !IsWhiteLine(line)) { // skip non-empty lines
		line++;
	}
	while (line < LinesTotal() && IsWhiteLine(line)) { // skip empty lines
		line++;
	}
	if (line < LinesTotal())
		return LineStart(line);
	else // end of a document
		return LineEnd(line - 1);
}

CharacterClass Document::WordCharacterClass(unsigned int ch) const noexcept {
	if (dbcsCodePage && !IsASCIICharacter(ch)) {
		if (SC_CP_UTF8 == dbcsCodePage) {
			return CharClassify::ClassifyCharacter(ch);
		} else {
			return dbcsCharClass->ClassifyCharacter(ch);
		}
	}
	return charClass.GetClass(static_cast<unsigned char>(ch));
}

/**
 * Used by commands that want to select whole words.
 * Finds the start of word at pos when delta < 0 or the end of the word when delta >= 0.
 */
Sci::Position Document::ExtendWordSelect(Sci::Position pos, int delta, bool onlyWordCharacters) const noexcept {
	CharacterClass ccStart = CharacterClass::word;
	if (delta < 0) {
		if (pos > 0) {
			const CharacterExtracted ce = CharacterBefore(pos);
			const CharacterClass ceStart = WordCharacterClass(ce.character);
			if (!onlyWordCharacters || ceStart == ccStart || ceStart == CharacterClass::cjkWord) {
				ccStart = ceStart;
				pos -= ce.widthBytes;
			} else {
				return MovePositionOutsideChar(pos, delta, true);
			}
		}
		//const int style = StyleAt(pos);
		while (pos > 0) {
			const CharacterExtracted ce = CharacterBefore(pos);
			if (/*StyleAt(pos - 1) != style || */WordCharacterClass(ce.character) != ccStart)
				break;
			pos -= ce.widthBytes;
		}
	} else {
		if (pos < Length()) {
			const CharacterExtracted ce = CharacterAfter(pos);
			const CharacterClass ceStart = WordCharacterClass(ce.character);
			if (!onlyWordCharacters || ceStart == ccStart || ceStart == CharacterClass::cjkWord) {
				ccStart = ceStart;
				pos += ce.widthBytes;
			} else {
				return MovePositionOutsideChar(pos, delta, true);
			}
		}
		//const int style = StyleAt(pos - 1);
		while (pos < Length()) {
			const CharacterExtracted ce = CharacterAfter(pos);
			if (/*StyleAt(pos) != style || */WordCharacterClass(ce.character) != ccStart)
				break;
			pos += ce.widthBytes;
		}
	}
	return MovePositionOutsideChar(pos, delta, true);
}

/**
 * Find the start of the next word in either a forward (delta >= 0) or backwards direction
 * (delta < 0).
 * This is looking for a transition between character classes although there is also some
 * additional movement to transit white space.
 * Used by cursor movement by word commands.
 */
Sci::Position Document::NextWordStart(Sci::Position pos, int delta) const noexcept {
	if (delta < 0) {
		while (pos > 0) {
			const CharacterExtracted ce = CharacterBefore(pos);
			if (WordCharacterClass(ce.character) != CharacterClass::space)
				break;
			pos -= ce.widthBytes;
		}
		if (pos > 0) {
			CharacterExtracted ce = CharacterBefore(pos);
			const CharacterClass ccStart = WordCharacterClass(ce.character);
			while (pos > 0) {
				ce = CharacterBefore(pos);
				if (WordCharacterClass(ce.character) != ccStart)
					break;
				pos -= ce.widthBytes;
			}
		}
	} else {
		CharacterExtracted ce = CharacterAfter(pos);
		const CharacterClass ccStart = WordCharacterClass(ce.character);
		while (pos < Length()) {
			ce = CharacterAfter(pos);
			if (WordCharacterClass(ce.character) != ccStart)
				break;
			pos += ce.widthBytes;
		}
		while (pos < Length()) {
			ce = CharacterAfter(pos);
			if (WordCharacterClass(ce.character) != CharacterClass::space)
				break;
			pos += ce.widthBytes;
		}
	}
	return pos;
}

/**
 * Find the end of the next word in either a forward (delta >= 0) or backwards direction
 * (delta < 0).
 * This is looking for a transition between character classes although there is also some
 * additional movement to transit white space.
 * Used by cursor movement by word commands.
 */
Sci::Position Document::NextWordEnd(Sci::Position pos, int delta) const noexcept {
	if (delta < 0) {
		if (pos > 0) {
			CharacterExtracted ce = CharacterBefore(pos);
			const CharacterClass ccStart = WordCharacterClass(ce.character);
			if (ccStart != CharacterClass::space) {
				while (pos > 0) {
					ce = CharacterBefore(pos);
					if (WordCharacterClass(ce.character) != ccStart)
						break;
					pos -= ce.widthBytes;
				}
			}
			while (pos > 0) {
				ce = CharacterBefore(pos);
				if (WordCharacterClass(ce.character) != CharacterClass::space)
					break;
				pos -= ce.widthBytes;
			}
		}
	} else {
		while (pos < Length()) {
			const CharacterExtracted ce = CharacterAfter(pos);
			if (WordCharacterClass(ce.character) != CharacterClass::space)
				break;
			pos += ce.widthBytes;
		}
		if (pos < Length()) {
			CharacterExtracted ce = CharacterAfter(pos);
			const CharacterClass ccStart = WordCharacterClass(ce.character);
			while (pos < Length()) {
				ce = CharacterAfter(pos);
				if (WordCharacterClass(ce.character) != ccStart)
					break;
				pos += ce.widthBytes;
			}
		}
	}
	return pos;
}

/**
 * Check that the character at the given position is a word or punctuation character and that
 * the previous character is of a different character class.
 */
bool Document::IsWordStartAt(Sci::Position pos) const noexcept {
	if (pos >= Length())
		return false;
	if (pos > 0) {
		const CharacterExtracted cePos = CharacterAfter(pos);
		const CharacterClass ccPos = WordCharacterClass(cePos.character);
		const CharacterExtracted cePrev = CharacterBefore(pos);
		const CharacterClass ccPrev = WordCharacterClass(cePrev.character);
		return (ccPos == CharacterClass::word || ccPos == CharacterClass::punctuation || ccPos == CharacterClass::cjkWord) &&
			(ccPos != ccPrev/* || StyleAt(pos - 1) != StyleAt(pos)*/);
	}
	return true;
}

/**
 * Check that the character at the given position is a word or punctuation character and that
 * the next character is of a different character class.
 */
bool Document::IsWordEndAt(Sci::Position pos) const noexcept {
	if (pos <= 0)
		return false;
	if (pos < Length()) {
		const CharacterExtracted cePos = CharacterAfter(pos);
		const CharacterClass ccPos = WordCharacterClass(cePos.character);
		const CharacterExtracted cePrev = CharacterBefore(pos);
		const CharacterClass ccPrev = WordCharacterClass(cePrev.character);
		return (ccPrev == CharacterClass::word || ccPrev == CharacterClass::punctuation || ccPrev == CharacterClass::cjkWord) &&
			(ccPrev != ccPos/* || StyleAt(pos - 1) != StyleAt(pos)*/);
	}
	return true;
}

/**
 * Check that the given range is has transitions between character classes at both
 * ends and where the characters on the inside are word or punctuation characters.
 */
bool Document::IsWordAt(Sci::Position start, Sci::Position end) const noexcept {
	return (start < end) && IsWordStartAt(start) && IsWordEndAt(end);
}

bool Document::MatchesWordOptions(bool word, bool wordStart, Sci::Position pos, Sci::Position length) const noexcept {
	return (!word && !wordStart) ||
		(word && IsWordAt(pos, pos + length)) ||
		(wordStart && IsWordStartAt(pos));
}

bool Document::HasCaseFolder() const noexcept {
	return pcf != nullptr;
}

void Document::SetCaseFolder(std::unique_ptr<CaseFolder> pcf_) noexcept {
	pcf = std::move(pcf_);
}

Document::CharacterExtracted Document::ExtractCharacter(Sci::Position position) const noexcept {
	const unsigned char leadByte = cb.UCharAt(position);
	if (UTF8IsAscii(leadByte)) {
		// Common case: ASCII character
		return CharacterExtracted(leadByte, 1);
	}
	const int widthCharBytes = UTF8BytesOfLead(leadByte);
	unsigned char charBytes[UTF8MaxBytes] = { leadByte, 0, 0, 0 };
	for (int b = 1; b < widthCharBytes; b++) {
		charBytes[b] = cb.UCharAt(position + b);
	}
	const int utf8status = UTF8ClassifyMulti(charBytes, widthCharBytes);
	if (utf8status & UTF8MaskInvalid) {
		// Treat as invalid and use up just one byte
		return CharacterExtracted(unicodeReplacementChar, 1);
	} else {
		return CharacterExtracted(UnicodeFromUTF8(charBytes), utf8status & UTF8MaskWidth);
	}
}

/**
 * Find text in document, supporting both forward and backward
 * searches (just pass minPos > maxPos to do a backward search)
 * Has not been tested with backwards DBCS searches yet.
 */
Sci::Position Document::FindText(Sci::Position minPos, Sci::Position maxPos, const char *search,
	int flags, Sci::Position *length) {
	if (*length <= 0) {
		return minPos;
	}
	const bool caseSensitive = (flags & SCFIND_MATCHCASE) != 0;
	if (flags & SCFIND_REGEXP) {
		if (!regex) {
			regex = std::unique_ptr<RegexSearchBase>(CreateRegexSearch(&charClass));
		}
		return regex->FindText(this, minPos, maxPos, search, caseSensitive, flags, length);
	} else {
		const bool word = (flags & SCFIND_WHOLEWORD) != 0;
		const bool wordStart = (flags & SCFIND_WORDSTART) != 0;

		const Sci::Position direction = maxPos - minPos;
		//const bool forward = direction >= 0;
		const int increment = (direction >= 0) ? 1 : -1;
		// table for the condition: forward ? (pos < endSearch) : (pos >= endSearch)
        //                   direction >= 0  direction < 0
        // pos >= endSearch: break           continue
        // pos < endSearch:  continue        break
		// i.e. continue search when direction and (pos - endSearch) have opposite signs,
		// which can be wrote as: (direction ^ (pos - endSearch)) < 0

		// Range endpoints should not be inside DBCS characters, but just in case, move them.
		const Sci::Position startPos = MovePositionOutsideChar(minPos, increment, false);
		const Sci::Position endPos = MovePositionOutsideChar(maxPos, increment, false);

		// Compute actual search ranges needed
		const Sci::Position lengthFind = *length;

		// character less than safeChar is encoded in single byte in the encoding.
		constexpr int safeCharASCII = 0x80;		// UTF-8 forward & backward search, DBCS forward search
		constexpr int safeCharSBCS = 256;		// all

		//Platform::DebugPrintf("Find %d %d %s %d\n", startPos, endPos, search, lengthFind);
		const Sci::Position limitPos = std::max(startPos, endPos);
		Sci::Position pos = startPos;
		if (direction < 0 && !caseSensitive) {
			// Back all of a character
			pos = NextPosition(pos, -1);
		}
		if (caseSensitive) {
			const Sci::Position endSearch = (startPos <= endPos) ? endPos - lengthFind + 1 : endPos;
			const unsigned char * const searchData = reinterpret_cast<const unsigned char *>(search);
			const unsigned char charStartSearch = searchData[0];
			const int safeChar = (0 == dbcsCodePage) ? safeCharSBCS : ((direction >= 0 || SC_CP_UTF8 == dbcsCodePage) ? safeCharASCII : dbcsCharClass->MinTrailByte());
			// Boyer-Moore-Horspool-Sunday Algorithm / Quick Search Algorithm
			// http://www-igm.univ-mlv.fr/~lecroq/string/index.html
			// http://www-igm.univ-mlv.fr/~lecroq/string/node19.html
			// https://www.inf.hs-flensburg.de/lang/algorithmen/pattern/sundayen.htm
			Sci::Position shiftTable[256];
			if (lengthFind != 1) {
				Sci::Position shift = lengthFind;
				std::fill_n(shiftTable, std::size(shiftTable), (shift + 1) * increment);
				if (direction >= 0) {
					const unsigned char *ptr = searchData;
					while (*ptr != 0) {
						shiftTable[*ptr++] = shift--;
					}
				} else {
					const unsigned char *ptr = searchData + shift - 1;
					shift = -shift;
					while (ptr >= searchData) {
						shiftTable[*ptr--] = shift++;
					}
				}
			}

			const Sci::Position skip = (direction >= 0) ? lengthFind : -1;
			if (direction < 0) {
				pos = MovePositionOutsideChar(pos - lengthFind, -1, false);
			}
			//while (forward ? (pos < endSearch) : (pos >= endSearch)) {
			while ((direction ^ (pos - endSearch)) < 0) {
				const unsigned char leadByte = UCharAt(pos);
				if (charStartSearch == leadByte) {
					bool found = (pos + lengthFind) <= limitPos;
					for (Sci::Position indexSearch = 1; (indexSearch < lengthFind) && found; indexSearch++) {
						const unsigned char ch = UCharAt(pos + indexSearch);
						found = ch == searchData[indexSearch];
					}
					if (found && MatchesWordOptions(word, wordStart, pos, lengthFind)) {
						return pos;
					}
				}

				if (lengthFind == 1) {
					if (leadByte < safeChar) {
						pos += increment;
					} else {
						if (!NextCharacter(pos, increment)) {
							break;
						}
					}
				} else {
					const unsigned char nextByte = UCharAt(pos + skip);
					pos += shiftTable[nextByte];
					if (nextByte >= safeChar) {
						pos = MovePositionOutsideChar(pos, increment, false);
					}
				}
			}
		} else if (SC_CP_UTF8 == dbcsCodePage) {
			constexpr size_t maxFoldingExpansion = 4;
			std::vector<char> searchThing((lengthFind + 1) * UTF8MaxBytes * maxFoldingExpansion + 1);
			const size_t lenSearch = pcf->Fold(searchThing.data(), searchThing.size(), search, lengthFind);
			const unsigned char * const searchData = reinterpret_cast<const unsigned char *>(searchThing.data());
			//while (forward ? (pos < endPos) : (pos >= endPos)) {
			while ((direction ^ (pos - endPos)) < 0) {
				int widthFirstCharacter = 0;
				Sci::Position posIndexDocument = pos;
				size_t indexSearch = 0;
				bool characterMatches = true;
				for (;;) {
					const unsigned char leadByte = cb.UCharAt(posIndexDocument);
					char bytes[UTF8MaxBytes + 1];
					int widthChar = 1;
					if (!UTF8IsAscii(leadByte)) {
						const int widthCharBytes = UTF8BytesOfLead(leadByte);
						bytes[0] = static_cast<char>(leadByte);
						for (int b = 1; b < widthCharBytes; b++) {
							bytes[b] = cb.CharAt(posIndexDocument + b);
						}
						widthChar = UTF8ClassifyMulti(reinterpret_cast<const unsigned char *>(bytes), widthCharBytes) & UTF8MaskWidth;
					}
					if (!widthFirstCharacter) {
						widthFirstCharacter = widthChar;
					}
					if ((posIndexDocument + widthChar) > limitPos) {
						break;
					}
					size_t lenFlat = 1;
					if (widthChar == 1) {
						characterMatches = searchData[indexSearch] == MakeLowerCase(leadByte);
					} else {
						char folded[UTF8MaxBytes * maxFoldingExpansion + 1];
						lenFlat = pcf->Fold(folded, sizeof(folded), bytes, widthChar);
						// memcmp may examine lenFlat bytes in both arguments so assert it doesn't read past end of searchThing
						assert((indexSearch + lenFlat) <= searchThing.size());
						// Does folded match the buffer
						characterMatches = 0 == memcmp(folded, searchData + indexSearch, lenFlat);
					}
					if (!characterMatches) {
						break;
					}
					posIndexDocument += widthChar;
					indexSearch += lenFlat;
					if (indexSearch >= lenSearch) {
						break;
					}
				}
				if (characterMatches && (indexSearch == lenSearch)) {
					if (MatchesWordOptions(word, wordStart, pos, posIndexDocument - pos)) {
						*length = posIndexDocument - pos;
						return pos;
					}
				}
				if (direction >= 0) {
					pos += widthFirstCharacter;
				} else {
					if (!NextCharacter(pos, increment)) {
						break;
					}
				}
			}
		} else if (dbcsCodePage) {
			constexpr size_t maxBytesCharacter = 2;
			constexpr size_t maxFoldingExpansion = 4;
			std::vector<char> searchThing((lengthFind + 1) * maxBytesCharacter * maxFoldingExpansion + 1);
			const size_t lenSearch = pcf->Fold(searchThing.data(), searchThing.size(), search, lengthFind);
			const unsigned char * const searchData = reinterpret_cast<const unsigned char *>(searchThing.data());
			//while (forward ? (pos < endPos) : (pos >= endPos)) {
			while ((direction ^ (pos - endPos)) < 0) {
				int widthFirstCharacter = 0;
				Sci::Position indexDocument = 0;
				size_t indexSearch = 0;
				bool characterMatches = true;
				for (;;) {
					const unsigned char leadByte = cb.UCharAt(pos + indexDocument);
					const int widthChar = IsDBCSLeadByteNoExcept(leadByte) ? 2 : 1;
					if (!widthFirstCharacter) {
						widthFirstCharacter = widthChar;
					}
					if ((pos + indexDocument + widthChar) > limitPos) {
						break;
					}
					size_t lenFlat = 1;
					if (widthChar == 1) {
						characterMatches = searchData[indexSearch] == MakeLowerCase(leadByte);
					} else {
						char bytes[maxBytesCharacter + 1];
						bytes[0] = static_cast<char>(leadByte);
						bytes[1] = cb.CharAt(pos + indexDocument + 1);
						char folded[maxBytesCharacter * maxFoldingExpansion + 1];
						lenFlat = pcf->Fold(folded, sizeof(folded), bytes, widthChar);
						// memcmp may examine lenFlat bytes in both arguments so assert it doesn't read past end of searchThing
						assert((indexSearch + lenFlat) <= searchThing.size());
						// Does folded match the buffer
						characterMatches = 0 == memcmp(folded, searchData + indexSearch, lenFlat);
					}
					if (!characterMatches) {
						break;
					}
					indexDocument += widthChar;
					indexSearch += lenFlat;
					if (indexSearch >= lenSearch) {
						break;
					}
				}
				if (characterMatches && (indexSearch == lenSearch)) {
					if (MatchesWordOptions(word, wordStart, pos, indexDocument)) {
						*length = indexDocument;
						return pos;
					}
				}
				if (direction >= 0) {
					pos += widthFirstCharacter;
				} else {
					if (!NextCharacter(pos, increment)) {
						break;
					}
				}
			}
		} else {
			const Sci::Position endSearch = (startPos <= endPos) ? endPos - lengthFind + 1 : endPos;
			std::vector<char> searchThing(lengthFind + 1);
			pcf->Fold(searchThing.data(), searchThing.size(), search, lengthFind);
			const char * const searchData = searchThing.data();
			//while (forward ? (pos < endSearch) : (pos >= endSearch)) {
			while ((direction ^ (pos - endSearch)) < 0) {
				bool found = (pos + lengthFind) <= limitPos;
				for (Sci::Position indexSearch = 0; (indexSearch < lengthFind) && found; indexSearch++) {
					const char ch = CharAt(pos + indexSearch);
					const char chTest = searchData[indexSearch];
					if (UTF8IsAscii(ch)) {
						found = chTest == MakeLowerCase(ch);
					} else {
						char folded[2];
						pcf->Fold(folded, sizeof(folded), &ch, 1);
						found = folded[0] == chTest;
					}
				}
				if (found && MatchesWordOptions(word, wordStart, pos, lengthFind)) {
					return pos;
				}
				pos += increment;
			}
		}
	}
	//Platform::DebugPrintf("Not found\n");
	return -1;
}

const char *Document::SubstituteByPosition(const char *text, Sci::Position *length) {
	if (regex)
		return regex->SubstituteByPosition(this, text, length);
	else
		return nullptr;
}

int Document::LineCharacterIndex() const noexcept {
	return cb.LineCharacterIndex();
}

void Document::AllocateLineCharacterIndex(int lineCharacterIndex) {
	return cb.AllocateLineCharacterIndex(lineCharacterIndex);
}

void Document::ReleaseLineCharacterIndex(int lineCharacterIndex) {
	return cb.ReleaseLineCharacterIndex(lineCharacterIndex);
}

Sci::Line Document::LinesTotal() const noexcept {
	return cb.Lines();
}

void Document::AllocateLines(Sci::Line lines) {
	cb.AllocateLines(lines);
}

void Document::SetDefaultCharClasses(bool includeWordClass) noexcept {
	charClass.SetDefaultCharClasses(includeWordClass);
	if (regex) {
		regex->ClearCache();
	}
}

void Document::SetCharClasses(const unsigned char *chars, CharacterClass newCharClass) noexcept {
	charClass.SetCharClasses(chars, newCharClass);
	if (regex) {
		regex->ClearCache();
	}
}

void Document::SetCharClassesEx(const unsigned char *chars, int length) noexcept {
	charClass.SetCharClassesEx(chars, length);
	if (regex) {
		regex->ClearCache();
	}
}

int Document::GetCharsOfClass(CharacterClass characterClass, unsigned char *buffer) const noexcept {
    return charClass.GetCharsOfClass(characterClass, buffer);
}

#if 0
void Document::SetCharacterCategoryOptimization(int countCharacters) {
	charMap.Optimize(countCharacters);
}

int Document::CharacterCategoryOptimization() const noexcept {
	return charMap.Size();
}
#endif

void SCI_METHOD Document::StartStyling(Sci_Position position) noexcept {
	endStyled = position;
}

bool SCI_METHOD Document::SetStyleFor(Sci_Position length, unsigned char style) {
	if (enteredStyling != 0) {
		return false;
	} else {
		enteredStyling++;
		const Sci::Position prevEndStyled = endStyled;
		if (cb.SetStyleFor(endStyled, length, style)) {
			const DocModification mh(SC_MOD_CHANGESTYLE | SC_PERFORMED_USER,
				prevEndStyled, length);
			NotifyModified(mh);
		}
		endStyled += length;
		enteredStyling--;
		return true;
	}
}

bool SCI_METHOD Document::SetStyles(Sci_Position length, const unsigned char *styles) {
	if (enteredStyling != 0) {
		return false;
	} else {
		enteredStyling++;
		bool didChange = false;
		Sci::Position startMod = 0;
		Sci::Position endMod = 0;
		for (int iPos = 0; iPos < length; iPos++, endStyled++) {
			PLATFORM_ASSERT(endStyled < Length());
			if (cb.SetStyleAt(endStyled, styles[iPos])) {
				if (!didChange) {
					startMod = endStyled;
				}
				didChange = true;
				endMod = endStyled;
			}
		}
		if (didChange) {
			const DocModification mh(SC_MOD_CHANGESTYLE | SC_PERFORMED_USER,
				startMod, endMod - startMod + 1);
			NotifyModified(mh);
		}
		enteredStyling--;
		return true;
	}
}

void Document::EnsureStyledTo(Sci::Position pos) {
	if ((enteredStyling == 0) && (pos > GetEndStyled())) {
		IncrementStyleClock();
		if (pli && !pli->UseContainerLexing()) {
			const Sci::Line lineEndStyled = SciLineFromPosition(GetEndStyled());
			const Sci::Position endStyledTo = LineStart(lineEndStyled);
			pli->Colourise(endStyledTo, pos);
		} else {
			// Ask the watchers to style, and stop as soon as one responds.
			for (auto it = watchers.begin();
				(pos > GetEndStyled()) && (it != watchers.end()); ++it) {
				it->watcher->NotifyStyleNeeded(this, it->userData, pos);
			}
		}
	}
}

void Document::StyleToAdjustingLineDuration(Sci::Position pos) {
	const Sci::Line lineFirst = SciLineFromPosition(GetEndStyled());
	ElapsedPeriod epStyling;
	EnsureStyledTo(pos);
	const Sci::Line lineLast = SciLineFromPosition(GetEndStyled());
#if ActionDuration_MeasureTimeByBytes
	const Sci::Line actions = LineStart(lineLast) - LineStart(lineFirst);
#else
	const Sci::Line actions = lineLast - lineFirst;
#endif
	durationStyleOneLine.AddSample(actions, epStyling.Duration());
}

void Document::LexerChanged(bool hasStyles_) {
	if (cb.EnsureStyleBuffer(hasStyles_)) {
		endStyled = 0;
	}
	// Tell the watchers the lexer has changed.
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifyLexerChanged(this, watcher.userData);
	}
}

LexInterface *Document::GetLexInterface() const noexcept {
	return pli.get();
}

void Document::SetLexInterface(std::unique_ptr<LexInterface> pLexInterface) noexcept {
	pli = std::move(pLexInterface);
}

int SCI_METHOD Document::SetLineState(Sci_Line line, int state) {
	const int statePrevious = States()->SetLineState(line, state, LinesTotal());
	if (state != statePrevious) {
		const DocModification mh(SC_MOD_CHANGELINESTATE, LineStart(line), 0, 0, nullptr, line);
		NotifyModified(mh);
	}
	return statePrevious;
}

int SCI_METHOD Document::GetLineState(Sci_Line line) const noexcept {
	return States()->GetLineState(line);
}

Sci::Line Document::GetMaxLineState() const noexcept {
	return States()->GetMaxLineState();
}

void SCI_METHOD Document::ChangeLexerState(Sci_Position start, Sci_Position end) {
	const DocModification mh(SC_MOD_LEXERSTATE, start,
		end - start, 0, nullptr, 0);
	NotifyModified(mh);
}

StyledText Document::MarginStyledText(Sci::Line line) const noexcept {
	const LineAnnotation *pla = Margins();
	return StyledText(pla->Length(line), pla->Text(line),
		pla->MultipleStyles(line), pla->Style(line), pla->Styles(line));
}

void Document::MarginSetText(Sci::Line line, const char *text) {
	Margins()->SetText(line, text);
	const DocModification mh(SC_MOD_CHANGEMARGIN, LineStart(line),
		0, 0, nullptr, line);
	NotifyModified(mh);
}

void Document::MarginSetStyle(Sci::Line line, int style) {
	Margins()->SetStyle(line, style);
	NotifyModified(DocModification(SC_MOD_CHANGEMARGIN, LineStart(line),
		0, 0, nullptr, line));
}

void Document::MarginSetStyles(Sci::Line line, const unsigned char *styles) {
	Margins()->SetStyles(line, styles);
	NotifyModified(DocModification(SC_MOD_CHANGEMARGIN, LineStart(line),
		0, 0, nullptr, line));
}

void Document::MarginClearAll() {
	const Sci::Line maxEditorLine = LinesTotal();
	for (Sci::Line l = 0; l < maxEditorLine; l++) {
		MarginSetText(l, nullptr);
	}
	// Free remaining data
	Margins()->ClearAll();
}

StyledText Document::AnnotationStyledText(Sci::Line line) const noexcept {
	const LineAnnotation *pla = Annotations();
	return StyledText(pla->Length(line), pla->Text(line),
		pla->MultipleStyles(line), pla->Style(line), pla->Styles(line));
}

void Document::AnnotationSetText(Sci::Line line, const char *text) {
	if (line >= 0 && line < LinesTotal()) {
		const Sci::Line linesBefore = AnnotationLines(line);
		Annotations()->SetText(line, text);
		const int linesAfter = AnnotationLines(line);
		DocModification mh(SC_MOD_CHANGEANNOTATION, LineStart(line),
			0, 0, nullptr, line);
		mh.annotationLinesAdded = linesAfter - linesBefore;
		NotifyModified(mh);
	}
}

void Document::AnnotationSetStyle(Sci::Line line, int style) {
	if (line >= 0 && line < LinesTotal()) {
		Annotations()->SetStyle(line, style);
		const DocModification mh(SC_MOD_CHANGEANNOTATION, LineStart(line),
			0, 0, nullptr, line);
		NotifyModified(mh);
	}
}

void Document::AnnotationSetStyles(Sci::Line line, const unsigned char *styles) {
	if (line >= 0 && line < LinesTotal()) {
		Annotations()->SetStyles(line, styles);
	}
}

int Document::AnnotationLines(Sci::Line line) const noexcept {
	return Annotations()->Lines(line);
}

void Document::AnnotationClearAll() {
	const Sci::Line maxEditorLine = LinesTotal();
	for (Sci::Line l = 0; l < maxEditorLine; l++) {
		AnnotationSetText(l, nullptr);
	}
	// Free remaining data
	Annotations()->ClearAll();
}

StyledText Document::EOLAnnotationStyledText(Sci::Line line) const noexcept {
	const LineAnnotation *pla = EOLAnnotations();
	return StyledText(pla->Length(line), pla->Text(line),
		pla->MultipleStyles(line), pla->Style(line), pla->Styles(line));
}

void Document::EOLAnnotationSetText(Sci::Line line, const char *text) {
	if (line >= 0 && line < LinesTotal()) {
		EOLAnnotations()->SetText(line, text);
		const DocModification mh(SC_MOD_CHANGEEOLANNOTATION, LineStart(line),
			0, 0, nullptr, line);
		NotifyModified(mh);
	}
}

void Document::EOLAnnotationSetStyle(Sci::Line line, int style) {
	if (line >= 0 && line < LinesTotal()) {
		EOLAnnotations()->SetStyle(line, style);
		const DocModification mh(SC_MOD_CHANGEEOLANNOTATION, LineStart(line),
			0, 0, nullptr, line);
		NotifyModified(mh);
	}
}

void Document::EOLAnnotationClearAll() {
	const Sci::Line maxEditorLine = LinesTotal();
	for (Sci::Line l = 0; l < maxEditorLine; l++)
		EOLAnnotationSetText(l, nullptr);
	// Free remaining data
	EOLAnnotations()->ClearAll();
}

void Document::IncrementStyleClock() noexcept {
	styleClock = (styleClock + 1) % 0x100000;
}

void SCI_METHOD Document::DecorationSetCurrentIndicator(int indicator) noexcept {
	decorations->SetCurrentIndicator(indicator);
}

void SCI_METHOD Document::DecorationFillRange(Sci_Position position, int value, Sci_Position fillLength) {
	const FillResult<Sci::Position> fr = decorations->FillRange(
		position, value, fillLength);
	if (fr.changed) {
		const DocModification mh(SC_MOD_CHANGEINDICATOR | SC_PERFORMED_USER,
			fr.position, fr.fillLength);
		NotifyModified(mh);
	}
}

bool Document::AddWatcher(DocWatcher *watcher, void *userData) {
	const WatcherWithUserData wwud(watcher, userData);
	const auto it = std::find(watchers.begin(), watchers.end(), wwud);
	if (it != watchers.end())
		return false;
	watchers.push_back(wwud);
	return true;
}

bool Document::RemoveWatcher(DocWatcher *watcher, void *userData) {
	auto it = std::find(watchers.begin(), watchers.end(), WatcherWithUserData(watcher, userData));
	if (it != watchers.end()) {
		watchers.erase(it);
		return true;
	}
	return false;
}

void Document::NotifyModifyAttempt() noexcept {
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifyModifyAttempt(this, watcher.userData);
	}
}

void Document::NotifySavePoint(bool atSavePoint) noexcept {
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifySavePoint(this, watcher.userData, atSavePoint);
	}
}

void Document::NotifyModified(DocModification mh) {
	if (mh.modificationType & SC_MOD_INSERTTEXT) {
		decorations->InsertSpace(mh.position, mh.length);
	} else if (mh.modificationType & SC_MOD_DELETETEXT) {
		decorations->DeleteRange(mh.position, mh.length);
	}
	for (const auto &watcher : watchers) {
		watcher.watcher->NotifyModified(this, mh, watcher.userData);
	}
}

// Used for word part navigation.
static constexpr bool IsASCIIPunctuationCharacter(unsigned int ch) noexcept {
	return IsPunctuation(ch);
}

bool Document::IsWordPartSeparator(unsigned int ch) const noexcept {
	const CharacterClass cc = WordCharacterClass(ch);
	return (cc == CharacterClass::word || cc == CharacterClass::cjkWord) && IsASCIIPunctuationCharacter(ch);
}

Sci::Position Document::WordPartLeft(Sci::Position pos) const noexcept {
	if (pos > 0) {
		pos -= CharacterBefore(pos).widthBytes;
		CharacterExtracted ceStart = CharacterAfter(pos);
		if (IsWordPartSeparator(ceStart.character)) {
			while (pos > 0 && IsWordPartSeparator(CharacterAfter(pos).character)) {
				pos -= CharacterBefore(pos).widthBytes;
			}
		}
		if (pos > 0) {
			ceStart = CharacterAfter(pos);
			pos -= CharacterBefore(pos).widthBytes;
			if (!IsASCIICharacter(ceStart.character)) {
				while (pos > 0 && !IsASCIICharacter(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				if (IsASCIICharacter(CharacterAfter(pos).character))
					pos += CharacterAfter(pos).widthBytes;
			} else if (IsLowerCase(ceStart.character)) {
				while (pos > 0 && IsLowerCase(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				ceStart = CharacterAfter(pos);
				if (!IsUpperCase(ceStart.character) && !IsLowerCase(ceStart.character))
					pos += CharacterAfter(pos).widthBytes;
			} else if (IsUpperCase(ceStart.character)) {
				while (pos > 0 && IsUpperCase(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				if (!IsUpperCase(CharacterAfter(pos).character))
					pos += CharacterAfter(pos).widthBytes;
			} else if (IsADigit(ceStart.character)) {
				while (pos > 0 && IsADigit(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				if (!IsADigit(CharacterAfter(pos).character))
					pos += CharacterAfter(pos).widthBytes;
			} else if (IsGraphic(ceStart.character)) {
				while (pos > 0 && IsASCIIPunctuationCharacter(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				if (!IsASCIIPunctuationCharacter(CharacterAfter(pos).character))
					pos += CharacterAfter(pos).widthBytes;
			} else if (isspacechar(ceStart.character)) {
				while (pos > 0 && isspacechar(CharacterAfter(pos).character)) {
					pos -= CharacterBefore(pos).widthBytes;
				}
				if (!isspacechar(CharacterAfter(pos).character))
					pos += CharacterAfter(pos).widthBytes;
			} else {
				pos += CharacterAfter(pos).widthBytes;
			}
		}
	}
	return pos;
}

Sci::Position Document::WordPartRight(Sci::Position pos) const noexcept {
	CharacterExtracted ceStart = CharacterAfter(pos);
	const Sci::Position length = Length();
	while (pos < length && IsWordPartSeparator(ceStart.character)) {
		pos += ceStart.widthBytes;
		ceStart = CharacterAfter(pos);
	}
	if (!IsASCIICharacter(ceStart.character)) {
		while (pos < length && !IsASCIICharacter(ceStart.character)) {
			pos += ceStart.widthBytes;
			ceStart = CharacterAfter(pos);
		}
	} else if (IsLowerCase(ceStart.character)) {
		while (pos < length && IsLowerCase(ceStart.character)) {
			pos += ceStart.widthBytes;
			ceStart = CharacterAfter(pos);
		}
	} else if (IsUpperCase(ceStart.character)) {
		CharacterExtracted cePos = CharacterAfter(pos + ceStart.widthBytes);
		if (IsLowerCase(cePos.character)) {
			pos += ceStart.widthBytes;
			ceStart = cePos;
			while (pos < length && IsLowerCase(ceStart.character)) {
				pos += ceStart.widthBytes;
				ceStart = CharacterAfter(pos);
			}
		} else {
			while (pos < length && IsUpperCase(ceStart.character)) {
				pos += ceStart.widthBytes;
				ceStart = CharacterAfter(pos);
			}
		}
		if (IsLowerCase(ceStart.character)) {
			cePos = CharacterBefore(pos);
			if (IsUpperCase(cePos.character)) {
				pos -= cePos.widthBytes;
			}
		}
	} else if (IsADigit(ceStart.character)) {
		while (pos < length && IsADigit(ceStart.character)) {
			pos += ceStart.widthBytes;
			ceStart = CharacterAfter(pos);
		}
	} else if (IsGraphic(ceStart.character)) {
		while (pos < length && IsASCIIPunctuationCharacter(ceStart.character)) {
			pos += ceStart.widthBytes;
			ceStart = CharacterAfter(pos);
		}
	} else if (isspacechar(ceStart.character)) {
		while (pos < length && isspacechar(ceStart.character)) {
			pos += ceStart.widthBytes;
			ceStart = CharacterAfter(pos);
		}
	} else {
		pos += ceStart.widthBytes;
	}
	return pos;
}

Sci::Position Document::ExtendStyleRange(Sci::Position pos, int delta, bool singleLine) noexcept {
	const char sStart = cb.StyleAt(pos);
	if (delta < 0) {
		while (pos > 0 && (cb.StyleAt(pos) == sStart) && (!singleLine || !IsEOLChar(cb.CharAt(pos))))
			pos--;
		pos++;
	} else {
		while (pos < (Length()) && (cb.StyleAt(pos) == sStart) && (!singleLine || !IsEOLChar(cb.CharAt(pos))))
			pos++;
	}
	return pos;
}

static constexpr char BraceOpposite(char ch) noexcept {
	switch (ch) {
	case '(':
		return ')';
	case ')':
		return '(';
	case '[':
		return ']';
	case ']':
		return '[';
	case '{':
		return '}';
	case '}':
		return '{';
	case '<':
		return '>';
	case '>':
		return '<';
	default:
		return '\0';
	}
}

// TODO: should be able to extend styled region to find matching brace
Sci::Position Document::BraceMatch(Sci::Position position, Sci::Position /*maxReStyle*/, Sci::Position startPos, bool useStartPos) const noexcept {
	const char chBrace = CharAt(position);
	const char chSeek = BraceOpposite(chBrace);
	if (chSeek == '\0')
		return -1;
	const int styBrace = StyleIndexAt(position);
	const int direction = (chBrace < chSeek) ? 1 : -1;
	int depth = 1;
	position = useStartPos ? startPos : NextPosition(position, direction);
	while ((position >= 0) && (position < Length())) {
		const char chAtPos = CharAt(position);
		const int styAtPos = StyleIndexAt(position);
		if ((position > GetEndStyled()) || (styAtPos == styBrace)) {
			if (chAtPos == chBrace)
				depth++;
			if (chAtPos == chSeek)
				depth--;
			if (depth == 0)
				return position;
		}
		const Sci::Position positionBeforeMove = position;
		position = NextPosition(position, direction);
		if (position == positionBeforeMove)
			break;
	}
	return -1;
}

/**
 * Implementation of RegexSearchBase for the default built-in regular expression engine
 */
class BuiltinRegex : public RegexSearchBase {
public:
	explicit BuiltinRegex(CharClassify *charClassTable) : search(charClassTable) {}
	BuiltinRegex(const BuiltinRegex &) = delete;
	BuiltinRegex(BuiltinRegex &&) = delete;
	BuiltinRegex &operator=(const BuiltinRegex &) = delete;
	BuiltinRegex &operator=(BuiltinRegex &&) = delete;
	~BuiltinRegex() override = default;

	Sci::Position FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
		bool caseSensitive, int flags, Sci::Position *length) override;

	const char *SubstituteByPosition(Document *doc, const char *text, Sci::Position *length) override;

	void ClearCache() noexcept override {
		search.ClearCache();
	}

private:
	RESearch search;
	std::string substituted;
};

namespace {

/**
* RESearchRange keeps track of search range.
*/
class RESearchRange {
public:
	const Document *doc;
	int increment;
	Sci::Position startPos;
	Sci::Position endPos;
	Sci::Line lineRangeStart;
	Sci::Line lineRangeEnd;
	Sci::Line lineRangeBreak;
	RESearchRange(const Document *doc_, Sci::Position minPos, Sci::Position maxPos) noexcept : doc(doc_) {
		increment = (minPos <= maxPos) ? 1 : -1;

		// Range endpoints should not be inside DBCS characters or between a CR and LF,
		// but just in case, move them.
		startPos = doc->MovePositionOutsideChar(minPos, 1, true);
		endPos = doc->MovePositionOutsideChar(maxPos, 1, true);

		lineRangeStart = doc->SciLineFromPosition(startPos);
		lineRangeEnd = doc->SciLineFromPosition(endPos);
		lineRangeBreak = lineRangeEnd + increment;
	}
	Range LineRange(Sci::Line line) const noexcept {
		Range range(doc->LineStart(line), doc->LineEnd(line));
		if (increment == 1) {
			if (line == lineRangeStart)
				range.start = startPos;
			if (line == lineRangeEnd)
				range.end = endPos;
		} else {
			if (line == lineRangeEnd)
				range.start = endPos;
			if (line == lineRangeStart)
				range.end = startPos;
		}
		return range;
	}
};

// Define a way for the Regular Expression code to access the document
class DocumentIndexer : public CharacterIndexer {
	Document *pdoc;
	Sci::Position end;
public:
	DocumentIndexer(Document *pdoc_, Sci::Position end_) noexcept :
		pdoc(pdoc_), end(end_) {}

	DocumentIndexer(const DocumentIndexer &) = delete;
	DocumentIndexer(DocumentIndexer &&) = delete;
	DocumentIndexer &operator=(const DocumentIndexer &) = delete;
	DocumentIndexer &operator=(DocumentIndexer &&) = delete;

	~DocumentIndexer() override = default;

	char CharAt(Sci::Position index) const noexcept override {
		if (index < 0 || index >= end)
			return '\0';
		else
			return pdoc->CharAt(index);
	}

	bool IsWordStartAt(Sci::Position pos) const noexcept override {
		return pdoc->IsWordStartAt(pos);
	}

	bool IsWordEndAt(Sci::Position pos) const noexcept override {
		return pdoc->IsWordEndAt(pos);
	}

	Sci::Position MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir) const noexcept override {
		return pdoc->MovePositionOutsideChar(pos, moveDir, true);
	}

	Sci::Position NextPosition(Sci::Position pos, int moveDir) const noexcept override {
		return pdoc->NextPosition(pos, moveDir);
	}

	Sci::Position ExtendWordSelect(Sci::Position pos, int delta) const noexcept override {
		return pdoc->ExtendWordSelect(pos, delta, true);
	}
};

#ifndef NO_CXX11_REGEX

class ByteIterator {
public:
	typedef std::bidirectional_iterator_tag iterator_category;
	typedef char value_type;
	typedef ptrdiff_t difference_type;
	typedef char* pointer;
	typedef char& reference;

	const Document *doc;
	Sci::Position position;
	ByteIterator(const Document *doc_ = nullptr, Sci::Position position_ = 0) noexcept :
		doc(doc_), position(position_) {}
	ByteIterator(const ByteIterator &other) noexcept {
		doc = other.doc;
		position = other.position;
	}
	ByteIterator(ByteIterator &&other) noexcept {
		doc = other.doc;
		position = other.position;
	}
	ByteIterator &operator=(const ByteIterator &other) noexcept {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
		}
		return *this;
	}
	ByteIterator &operator=(ByteIterator &&) noexcept = default;
	~ByteIterator() = default;
	char operator*() const noexcept {
		return doc->CharAt(position);
	}
	ByteIterator &operator++() noexcept {
		position++;
		return *this;
	}
	ByteIterator operator++(int) noexcept {
		ByteIterator retVal(*this);
		position++;
		return retVal;
	}
	ByteIterator &operator--() noexcept {
		position--;
		return *this;
	}
	bool operator==(const ByteIterator &other) const noexcept {
		return doc == other.doc && position == other.position;
	}
	bool operator!=(const ByteIterator &other) const noexcept {
		return doc != other.doc || position != other.position;
	}
	Sci::Position Pos() const noexcept {
		return position;
	}
	Sci::Position PosRoundUp() const noexcept {
		return position;
	}
};

// On Windows, wchar_t is 16 bits wide and on Unix it is 32 bits wide.
// Would be better to use sizeof(wchar_t) or similar to differentiate
// but easier for now to hard-code platforms.
// C++11 has char16_t and char32_t but neither Clang nor Visual C++
// appear to allow specializing basic_regex over these.

#ifdef _WIN32
#define WCHAR_T_IS_16 1
#else
#define WCHAR_T_IS_16 0
#endif

#if WCHAR_T_IS_16

// On Windows, report non-BMP characters as 2 separate surrogates as that
// matches wregex since it is based on wchar_t.
class UTF8Iterator {
	// These 3 fields determine the iterator position and are used for comparisons
	const Document *doc;
	Sci::Position position;
	size_t characterIndex;
	// Remaining fields are derived from the determining fields so are excluded in comparisons
	unsigned int lenBytes;
	size_t lenCharacters;
	wchar_t buffered[2];
public:
	typedef std::bidirectional_iterator_tag iterator_category;
	typedef wchar_t value_type;
	typedef ptrdiff_t difference_type;
	typedef wchar_t* pointer;
	typedef wchar_t& reference;

	UTF8Iterator(const Document *doc_ = nullptr, Sci::Position position_ = 0) noexcept :
		doc(doc_), position(position_), characterIndex(0), lenBytes(0), lenCharacters(0), buffered{} {
		buffered[0] = 0;
		buffered[1] = 0;
		if (doc) {
			ReadCharacter();
		}
	}
	UTF8Iterator(const UTF8Iterator &other) noexcept : buffered{} {
		doc = other.doc;
		position = other.position;
		characterIndex = other.characterIndex;
		lenBytes = other.lenBytes;
		lenCharacters = other.lenCharacters;
		buffered[0] = other.buffered[0];
		buffered[1] = other.buffered[1];
	}
	UTF8Iterator(UTF8Iterator &&other) noexcept = default;
	UTF8Iterator &operator=(const UTF8Iterator &other) noexcept {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
			characterIndex = other.characterIndex;
			lenBytes = other.lenBytes;
			lenCharacters = other.lenCharacters;
			buffered[0] = other.buffered[0];
			buffered[1] = other.buffered[1];
		}
		return *this;
	}
	UTF8Iterator &operator=(UTF8Iterator &&) noexcept = default;
	~UTF8Iterator() = default;
	wchar_t operator*() const noexcept {
		assert(lenCharacters != 0);
		return buffered[characterIndex];
	}
	UTF8Iterator &operator++() noexcept {
		if ((characterIndex + 1) < (lenCharacters)) {
			characterIndex++;
		} else {
			position += lenBytes;
			ReadCharacter();
			characterIndex = 0;
		}
		return *this;
	}
	UTF8Iterator operator++(int) noexcept {
		UTF8Iterator retVal(*this);
		if ((characterIndex + 1) < (lenCharacters)) {
			characterIndex++;
		} else {
			position += lenBytes;
			ReadCharacter();
			characterIndex = 0;
		}
		return retVal;
	}
	UTF8Iterator &operator--() noexcept {
		if (characterIndex) {
			characterIndex--;
		} else {
			position = doc->NextPosition(position, -1);
			ReadCharacter();
			characterIndex = lenCharacters - 1;
		}
		return *this;
	}
	bool operator==(const UTF8Iterator &other) const noexcept {
		// Only test the determining fields, not the character widths and values derived from this
		return doc == other.doc &&
			position == other.position &&
			characterIndex == other.characterIndex;
	}
	bool operator!=(const UTF8Iterator &other) const noexcept {
		// Only test the determining fields, not the character widths and values derived from this
		return doc != other.doc ||
			position != other.position ||
			characterIndex != other.characterIndex;
	}
	Sci::Position Pos() const noexcept {
		return position;
	}
	Sci::Position PosRoundUp() const noexcept {
		if (characterIndex)
			return position + lenBytes;	// Force to end of character
		else
			return position;
	}
private:
	void ReadCharacter() noexcept {
		const Document::CharacterExtracted charExtracted = doc->ExtractCharacter(position);
		lenBytes = charExtracted.widthBytes;
		if (charExtracted.character == unicodeReplacementChar) {
			lenCharacters = 1;
			buffered[0] = static_cast<wchar_t>(charExtracted.character);
		} else {
			lenCharacters = UTF16FromUTF32Character(charExtracted.character, buffered);
		}
	}
};

#else

// On Unix, report non-BMP characters as single characters

class UTF8Iterator {
	const Document *doc;
	Sci::Position position;
public:
	typedef std::bidirectional_iterator_tag iterator_category;
	typedef wchar_t value_type;
	typedef ptrdiff_t difference_type;
	typedef wchar_t* pointer;
	typedef wchar_t& reference;

	UTF8Iterator(const Document *doc_ = nullptr, Sci::Position position_ = 0) noexcept :
		doc(doc_), position(position_) {}
	UTF8Iterator(const UTF8Iterator &other) noexcept {
		doc = other.doc;
		position = other.position;
	}
	UTF8Iterator(UTF8Iterator &&other) noexcept = default;
	UTF8Iterator &operator=(const UTF8Iterator &other) noexcept {
		if (this != &other) {
			doc = other.doc;
			position = other.position;
		}
		return *this;
	}
	UTF8Iterator &operator=(UTF8Iterator &&) noexcept = default;
	~UTF8Iterator() = default;
	wchar_t operator*() const noexcept {
		const Document::CharacterExtracted charExtracted = doc->ExtractCharacter(position);
		return charExtracted.character;
	}
	UTF8Iterator &operator++() noexcept {
		position = doc->NextPosition(position, 1);
		return *this;
	}
	UTF8Iterator operator++(int) noexcept {
		UTF8Iterator retVal(*this);
		position = doc->NextPosition(position, 1);
		return retVal;
	}
	UTF8Iterator &operator--() noexcept {
		position = doc->NextPosition(position, -1);
		return *this;
	}
	bool operator==(const UTF8Iterator &other) const noexcept {
		return doc == other.doc && position == other.position;
	}
	bool operator!=(const UTF8Iterator &other) const noexcept {
		return doc != other.doc || position != other.position;
	}
	Sci::Position Pos() const noexcept {
		return position;
	}
	Sci::Position PosRoundUp() const noexcept {
		return position;
	}
};

#endif

std::regex_constants::match_flag_type MatchFlags(const Document *doc, Sci::Position startPos, Sci::Position endPos) {
	std::regex_constants::match_flag_type flagsMatch = std::regex_constants::match_default;
	if (!doc->IsLineStartPosition(startPos))
		flagsMatch |= std::regex_constants::match_not_bol;
	if (!doc->IsLineEndPosition(endPos))
		flagsMatch |= std::regex_constants::match_not_eol;
	return flagsMatch;
}

template<typename Iterator, typename Regex>
bool MatchOnLines(const Document *doc, const Regex &regexp, const RESearchRange &resr, RESearch &search) {
	std::match_results<Iterator> match;

	// MSVC and libc++ have problems with ^ and $ matching line ends inside a range.
	// CRLF line ends are also a problem as ^ and $ only treat LF as a line end.
	// The std::regex::multiline option was added to C++17 to improve behaviour but
	// has not been implemented by compiler runtimes with MSVC always in multiline
	// mode and libc++ and libstdc++ always in single-line mode.
	// If multiline regex worked well then the line by line iteration could be removed
	// for the forwards case and replaced with the following 4 lines:
#ifdef REGEX_MULTILINE
	Iterator itStart(doc, resr.startPos);
	Iterator itEnd(doc, resr.endPos);
	const std::regex_constants::match_flag_type flagsMatch = MatchFlags(doc, resr.startPos, resr.endPos);
	const bool matched = std::regex_search(itStart, itEnd, match, regexp, flagsMatch);
#else
	// Line by line.
	bool matched = false;
	for (Sci::Line line = resr.lineRangeStart; line != resr.lineRangeBreak; line += resr.increment) {
		const Range lineRange = resr.LineRange(line);
		Iterator itStart(doc, lineRange.start);
		Iterator itEnd(doc, lineRange.end);
		std::regex_constants::match_flag_type flagsMatch = MatchFlags(doc, lineRange.start, lineRange.end);
		matched = std::regex_search(itStart, itEnd, match, regexp, flagsMatch);
		// Check for the last match on this line.
		if (matched) {
			if (resr.increment == -1) {
				while (matched) {
					Iterator itNext(doc, match[0].second.PosRoundUp());
					flagsMatch = MatchFlags(doc, itNext.Pos(), lineRange.end);
					std::match_results<Iterator> matchNext;
					matched = std::regex_search(itNext, itEnd, matchNext, regexp, flagsMatch);
					if (matched) {
						if (match[0].first == match[0].second) {
							// Empty match means failure so exit
							return false;
						}
						match = matchNext;
					}
				}
				matched = true;
			}
			break;
		}
	}
#endif
	if (matched) {
		for (size_t co = 0; co < match.size(); co++) {
			search.bopat[co] = match[co].first.Pos();
			search.eopat[co] = match[co].second.PosRoundUp();
			const Sci::Position lenMatch = search.eopat[co] - search.bopat[co];
			search.pat[co].resize(lenMatch);
			for (Sci::Position iPos = 0; iPos < lenMatch; iPos++) {
				search.pat[co][iPos] = doc->CharAt(iPos + search.bopat[co]);
			}
		}
	}
	return matched;
}

Sci::Position Cxx11RegexFindText(const Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
	bool caseSensitive, Sci::Position *length, RESearch &search) {
	const RESearchRange resr(doc, minPos, maxPos);
	try {
		//ElapsedPeriod ep;
		std::regex::flag_type flagsRe = std::regex::ECMAScript;
		// Flags that appear to have no effect:
		// | std::regex::collate | std::regex::extended;
		if (!caseSensitive)
			flagsRe = flagsRe | std::regex::icase;

		// Clear the RESearch so can fill in matches
		search.Clear();

		bool matched = false;
		if (SC_CP_UTF8 == doc->dbcsCodePage) {
			const std::wstring ws = WStringFromUTF8(s);
			std::wregex regexp;
			regexp.assign(ws, flagsRe);
			matched = MatchOnLines<UTF8Iterator>(doc, regexp, resr, search);
		} else {
			std::regex regexp;
			regexp.assign(s, flagsRe);
			matched = MatchOnLines<ByteIterator>(doc, regexp, resr, search);
		}

		Sci::Position posMatch = -1;
		if (matched) {
			posMatch = search.bopat[0];
			*length = search.eopat[0] - search.bopat[0];
		}
		// Example - search in doc/ScintillaHistory.html for
		// [[:upper:]]eta[[:space:]]
		// On MacBook, normally around 1 second but with locale imbued -> 14 seconds.
		//const double durSearch = ep.Duration(true);
		//Platform::DebugPrintf("Search:%9.6g \n", durSearch);
		return posMatch;
	} catch (const std::regex_error &) {
		// Failed to create regular expression
		throw RegexError();
	} catch (...) {
		// Failed in some other way
		return -1;
	}
}

#endif

}

Sci::Position BuiltinRegex::FindText(Document *doc, Sci::Position minPos, Sci::Position maxPos, const char *s,
	bool caseSensitive, int flags, Sci::Position *length) {

#ifndef NO_CXX11_REGEX
	if (flags & SCFIND_CXX11REGEX) {
		return Cxx11RegexFindText(doc, minPos, maxPos, s, caseSensitive, length, search);
	}
#endif

	const RESearchRange resr(doc, minPos, maxPos);

	const char *errmsg = search.Compile(s, *length, caseSensitive, flags);
	if (errmsg) {
		return -1;
	}
	// Find a variable in a property file: \$(\([A-Za-z0-9_.]+\))
	// Replace first '.' with '-' in each property file variable reference:
	//     Search: \$(\([A-Za-z0-9_-]+\)\.\([A-Za-z0-9_.]+\))
	//     Replace: $(\1-\2)
	Sci::Position pos = -1;
	Sci::Position lenRet = 0;
	const bool searchforLineStart = s[0] == '^';
	const char searchEnd = s[*length - 1];
	const char searchEndPrev = (*length > 1) ? s[*length - 2] : '\0';
	const bool searchforLineEnd = (searchEnd == '$') && (searchEndPrev != '\\');
	for (Sci::Line line = resr.lineRangeStart; line != resr.lineRangeBreak; line += resr.increment) {
		Sci::Position startOfLine = doc->LineStart(line);
		Sci::Position endOfLine = doc->LineEnd(line);
		if (resr.increment == 1) {
			if (line == resr.lineRangeStart) {
				if ((resr.startPos != startOfLine) && searchforLineStart)
					continue;	// Can't match start of line if start position after start of line
				startOfLine = resr.startPos;
			}
			if (line == resr.lineRangeEnd) {
				if ((resr.endPos != endOfLine) && searchforLineEnd)
					continue;	// Can't match end of line if end position before end of line
				endOfLine = resr.endPos;
			}
		} else {
			if (line == resr.lineRangeEnd) {
				if ((resr.endPos != startOfLine) && searchforLineStart)
					continue;	// Can't match start of line if end position after start of line
				startOfLine = resr.endPos;
			}
			if (line == resr.lineRangeStart) {
				if ((resr.startPos != endOfLine) && searchforLineEnd)
					continue;	// Can't match end of line if start position before end of line
				endOfLine = resr.startPos;
			}
		}

		const DocumentIndexer di(doc, endOfLine);
		int success = search.Execute(di, startOfLine, endOfLine);
		if (success) {
			pos = search.bopat[0];
			// Ensure only whole characters selected
			search.eopat[0] = doc->MovePositionOutsideChar(search.eopat[0], 1, false);
			lenRet = search.eopat[0] - search.bopat[0];
			// There can be only one start of a line, so no need to look for last match in line
			if ((resr.increment == -1) && !searchforLineStart) {
				// Check for the last match on this line.
				int repetitions = 1000;	// Break out of infinite loop
				while (success && (search.eopat[0] <= endOfLine) && (repetitions--)) {
					success = search.Execute(di, pos + 1, endOfLine);
					if (success) {
						if (search.eopat[0] <= minPos) {
							pos = search.bopat[0];
							lenRet = search.eopat[0] - search.bopat[0];
						} else {
							success = 0;
						}
					}
				}
			}
			break;
		}
	}
	*length = lenRet;
	return pos;
}

const char *BuiltinRegex::SubstituteByPosition(Document *doc, const char *text, Sci::Position *length) {
	substituted.clear();
	const DocumentIndexer di(doc, doc->Length());
	search.GrabMatches(di);
	for (Sci::Position j = 0; j < *length; j++) {
		if (text[j] == '\\') {
			if (text[j + 1] >= '0' && text[j + 1] <= '9') {
				const unsigned int patNum = text[j + 1] - '0';
				const Sci::Position len = search.eopat[patNum] - search.bopat[patNum];
				if (!search.pat[patNum].empty())	// Will be null if try for a match that did not occur
					substituted.append(search.pat[patNum].c_str(), len);
				j++;
			} else {
				j++;
				switch (text[j]) {
				case 'a':
					substituted.push_back('\a');
					break;
				case 'b':
					substituted.push_back('\b');
					break;
				case 'f':
					substituted.push_back('\f');
					break;
				case 'n':
					substituted.push_back('\n');
					break;
				case 'r':
					substituted.push_back('\r');
					break;
				case 't':
					substituted.push_back('\t');
					break;
				case 'v':
					substituted.push_back('\v');
					break;
				case '\\':
					substituted.push_back('\\');
					break;
				default:
					substituted.push_back('\\');
					j--;
				}
			}
		} else {
			substituted.push_back(text[j]);
		}
	}
	*length = substituted.length();
	return substituted.c_str();
}

#ifndef SCI_OWNREGEX

RegexSearchBase *Scintilla::CreateRegexSearch(CharClassify *charClassTable) {
	return new BuiltinRegex(charClassTable);
}

#endif
