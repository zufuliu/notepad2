// Scintilla source code edit control
/** @file CellBuffer.h
 ** Manages the text of the document.
 **/
// Copyright 1998-2004 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

#define InsertString_WithoutPerLine		1023

namespace Scintilla::Internal {

// Interface to per-line data that wants to see each line insertion and deletion
class PerLine {
public:
	virtual ~PerLine() = default;
	virtual void Init() = 0;
	virtual bool IsActive() const noexcept = 0;
	virtual void InsertLine(Sci::Line line) = 0;
	virtual void InsertLines(Sci::Line line, Sci::Line lines) = 0;
	virtual void RemoveLine(Sci::Line line) = 0;
};

class ChangeHistory;
/**
 * The line vector contains information about each of the lines in a cell buffer.
 */
class ILineVector;

enum class ActionType : uint8_t { insert, remove, start, container };

/**
 * Actions are used to store all the information required to perform one undo/redo step.
 */
class Action {
	static constexpr Sci::Position smallSize = sizeof(size_t) - 2;
public:
	ActionType at = ActionType::insert;
	bool mayCoalesce = false;
	char smallData[smallSize]{};
	Sci::Position position = 0;
	Sci::Position lenData = 0;
	std::unique_ptr<char[]> data;

	Action() noexcept = default;
	void Create(ActionType at_, Sci::Position position_ = 0, const char *data_ = nullptr, Sci::Position lenData_ = 0, bool mayCoalesce_ = true);
	void Clear() noexcept;
	const char *Data() const noexcept {
		return (lenData <= smallSize) ? smallData : data.get();
	}
};

/**
 *
 */
class UndoHistory {
	std::vector<Action> actions;
	int maxAction;
	int currentAction;
	int undoSequenceDepth;
	int savePoint;
	int tentativePoint;
	std::optional<int> detach;

	void EnsureUndoRoom();

public:
	UndoHistory();

	const char *AppendAction(ActionType at, Sci::Position position, const char *data, Sci::Position lengthData, bool &startSequence, bool mayCoalesce = true);

	void BeginUndoAction();
	void EndUndoAction();
	void DropUndoSequence() noexcept;
	void DeleteUndoHistory();

	/// The save point is a marker in the undo stack where the container has stated that
	/// the buffer was saved. Undo and redo can move over the save point.
	void SetSavePoint() noexcept;
	bool IsSavePoint() const noexcept;
	bool BeforeSavePoint() const noexcept;
	bool BeforeReachableSavePoint() const noexcept;
	bool AfterSavePoint() const noexcept;
	bool AfterDetachPoint() const noexcept;

	// Tentative actions are used for input composition so that it can be undone cleanly
	void TentativeStart() noexcept;
	void TentativeCommit() noexcept;
	bool TentativeActive() const noexcept;
	int TentativeSteps() noexcept;

	/// To perform an undo, StartUndo is called to retrieve the number of steps, then UndoStep is
	/// called that many times. Similarly for redo.
	bool CanUndo() const noexcept;
	int StartUndo() noexcept;
	const Action &GetUndoStep() const noexcept;
	void CompletedUndoStep() noexcept;
	bool CanRedo() const noexcept;
	int StartRedo() noexcept;
	const Action &GetRedoStep() const noexcept;
	void CompletedRedoStep() noexcept;
};

struct SplitView {
	const char *segment1 = nullptr;
	size_t length1 = 0;
	const char *segment2 = nullptr;
	size_t length = 0;

	SplitView(const SplitVector<char> &instance) noexcept;

	char CharAt(size_t position) const noexcept {
		if (position < length1) {
			return segment1[position];
		}
		if (position < length) {
			return segment2[position];
		}
		return '\0';
	}
};

/**
 * Holder for an expandable array of characters that supports undo and line markers.
 * Based on article "Data Structures in a Bit-Mapped Text Editor"
 * by Wilfred J. Hansen, Byte January 1987, page 183.
 */
class CellBuffer {
private:
	bool hasStyles;
	const bool largeDocument;
	bool readOnly;
	bool utf8Substance;
	Scintilla::LineEndType utf8LineEnds;
	SplitVector<char> substance;
	SplitVector<char> style;

	bool collectingUndo;
	UndoHistory uh;

	std::unique_ptr<ChangeHistory> changeHistory;

	std::unique_ptr<ILineVector> plv;

	bool UTF8LineEndOverlaps(Sci::Position position) const noexcept;
	bool UTF8IsCharacterBoundary(Sci::Position position) const;
	void ResetLineEnds();
	void RecalculateIndexLineStarts(Sci::Line lineFirst, Sci::Line lineLast);
	bool MaintainingLineCharacterIndex() const noexcept;
	/// Actions without undo
	void BasicInsertString(Sci::Position position, const char *s, Sci::Position insertLength);
	void BasicDeleteChars(Sci::Position position, Sci::Position deleteLength);

public:
	CellBuffer(bool hasStyles_, bool largeDocument_);
	// Deleted so CellBuffer objects can not be copied.
	CellBuffer(const CellBuffer &) = delete;
	CellBuffer(CellBuffer &&) = delete;
	void operator=(const CellBuffer &) = delete;
	void operator=(CellBuffer &&) = delete;
	~CellBuffer() noexcept;

	/// Retrieving positions outside the range of the buffer works and returns 0
	char CharAt(Sci::Position position) const noexcept;
	unsigned char UCharAt(Sci::Position position) const noexcept;
	void GetCharRange(char *buffer, Sci::Position position, Sci::Position lengthRetrieve) const noexcept;
	char StyleAt(Sci::Position position) const noexcept;
	void GetStyleRange(unsigned char *buffer, Sci::Position position, Sci::Position lengthRetrieve) const noexcept;
	const char *BufferPointer();
	const char *RangePointer(Sci::Position position, Sci::Position rangeLength) noexcept;
	const char *StyleRangePointer(Sci::Position position, Sci::Position rangeLength) noexcept;
	Sci::Position GapPosition() const noexcept;
	SplitView AllView() const noexcept;

	Sci::Position Length() const noexcept {
		return substance.Length();
	}
	void Allocate(Sci::Position newSize);
	bool EnsureStyleBuffer(bool hasStyles_);
	void SetUTF8Substance(bool utf8Substance_) noexcept {
		utf8Substance = utf8Substance_;
	}
	Scintilla::LineEndType GetLineEndTypes() const noexcept {
		return utf8LineEnds;
	}
	void SetLineEndTypes(Scintilla::LineEndType utf8LineEnds_);
	bool ContainsLineEnd(const char *s, Sci::Position length) const noexcept;
	void SetPerLine(PerLine *pl) noexcept;
	Scintilla::LineCharacterIndexType LineCharacterIndex() const noexcept;
	void AllocateLineCharacterIndex(Scintilla::LineCharacterIndexType lineCharacterIndex);
	void ReleaseLineCharacterIndex(Scintilla::LineCharacterIndexType lineCharacterIndex);
	Sci::Line Lines() const noexcept;
	void AllocateLines(Sci::Line lines);
	Sci::Position LineStart(Sci::Line line) const noexcept;
	Sci::Position IndexLineStart(Sci::Line line, Scintilla::LineCharacterIndexType lineCharacterIndex) const noexcept;
	Sci::Line LineFromPosition(Sci::Position pos) const noexcept;
	Sci::Line LineFromPositionIndex(Sci::Position pos, Scintilla::LineCharacterIndexType lineCharacterIndex) const noexcept;
	void InsertLine(Sci::Line line, Sci::Position position, bool lineStart);
	void RemoveLine(Sci::Line line);
	const char *InsertString(Sci::Position position, const char *s, Sci::Position insertLength, bool &startSequence);

	/// Setting styles for positions outside the range of the buffer is safe and has no effect.
	/// @return true if the style of a character is changed.
	bool SetStyleAt(Sci::Position position, char styleValue) noexcept;
	bool SetStyleFor(Sci::Position position, Sci::Position lengthStyle, char styleValue) noexcept;

	const char *DeleteChars(Sci::Position position, Sci::Position deleteLength, bool &startSequence);

	bool IsReadOnly() const noexcept {
		return readOnly;
	}
	void SetReadOnly(bool set) noexcept {
		readOnly = set;
	}
	bool IsLarge() const noexcept {
		return largeDocument;
	}
	bool HasStyles() const noexcept {
		return hasStyles;
	}

	/// The save point is a marker in the undo stack where the container has stated that
	/// the buffer was saved. Undo and redo can move over the save point.
	void SetSavePoint() noexcept;
	bool IsSavePoint() const noexcept;

	void TentativeStart() noexcept;
	void TentativeCommit() noexcept;
	bool TentativeActive() const noexcept;
	int TentativeSteps() noexcept;

	bool SetUndoCollection(bool collectUndo) noexcept;
	bool IsCollectingUndo() const noexcept {
		return collectingUndo;
	}
	void BeginUndoAction();
	void EndUndoAction();
	void AddUndoAction(Sci::Position token, bool mayCoalesce);
	void DeleteUndoHistory();

	/// To perform an undo, StartUndo is called to retrieve the number of steps, then UndoStep is
	/// called that many times. Similarly for redo.
	bool CanUndo() const noexcept;
	int StartUndo() noexcept;
	const Action &GetUndoStep() const noexcept;
	void PerformUndoStep();
	bool CanRedo() const noexcept;
	int StartRedo() noexcept;
	const Action &GetRedoStep() const noexcept;
	void PerformRedoStep();

	void ChangeHistorySet(bool enable);
	[[nodiscard]] int EditionAt(Sci::Position pos) const noexcept;
	[[nodiscard]] Sci::Position EditionEndRun(Sci::Position pos) const noexcept;
	[[nodiscard]] unsigned int EditionDeletesAt(Sci::Position pos) const noexcept;
	[[nodiscard]] Sci::Position EditionNextDelete(Sci::Position pos) const noexcept;
};

}
