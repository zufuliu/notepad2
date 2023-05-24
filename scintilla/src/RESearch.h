// Scintilla source code edit control
/** @file RESearch.h
 ** Interface to the regular expression search library.
 **/
// Written by Neil Hodgson <neilh@scintilla.org>
// Based on the work of Ozan S. Yigit.
// This file is in the public domain.
#pragma once

namespace Scintilla::Internal {

class CharacterIndexer {
public:
	virtual char CharAt(Sci::Position index) const noexcept = 0;
	virtual bool IsWordStartAt(Sci::Position pos) const noexcept = 0;
	virtual bool IsWordEndAt(Sci::Position pos) const noexcept = 0;
	virtual Sci::Position MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir) const noexcept = 0;
	virtual Sci::Position NextPosition(Sci::Position pos, int moveDir) const noexcept = 0;
	virtual Sci::Position ExtendWordSelect(Sci::Position pos, int delta) const noexcept = 0;
};

class RESearch {
public:
	explicit RESearch(const CharClassify *charClassTable);
	// No dynamic allocation so default copy constructor and assignment operator are OK.
	void Clear() noexcept;
	void ClearCache() noexcept;
	void GrabMatches(const CharacterIndexer &ci);
	const char *Compile(const char *pattern, Sci::Position length, bool caseSensitive, Scintilla::FindOption flags);
	int Execute(const CharacterIndexer &ci, Sci::Position lp, Sci::Position endp);

	static constexpr int MAXTAG = 10;
	static constexpr int NOTFOUND = -1;

	Sci::Position bopat[MAXTAG];
	Sci::Position eopat[MAXTAG];
	std::string pat[MAXTAG];

private:
	static constexpr int MAXNFA = 4096;
	// The following constants are not meant to be changeable.
	static constexpr int MAXCHR = 256;
	static constexpr int CHRBIT = 8;
	static constexpr int BITBLK = MAXCHR / CHRBIT;

	void ChSet(unsigned char c) noexcept;
	void ChSetWithCase(unsigned char c, bool caseSensitive) noexcept;
	int GetBackslashExpression(const char *pattern, int &incr) noexcept;

	const char *DoCompile(const char *pattern, Sci::Position length, bool caseSensitive, bool posix) noexcept;
	Sci::Position PMatch(const CharacterIndexer &ci, Sci::Position lp, Sci::Position endp, char *ap, int moveDir = 1, Sci::Position *offset = nullptr);

	Sci::Position bol;
	Sci::Position tagstk[MAXTAG];  /* subpat tag stack */
	char nfa[MAXNFA];    /* automaton */
	int sta;
	int failure;

	// cache for previous pattern with same address, length and flags
	const char *previousPattern;
	Sci::Position previousLength;
	Scintilla::FindOption previousFlags;
	std::string cachedPattern;

	unsigned char bittab[BITBLK]; /* bit table for CCL pre-set bits */
	const CharClassify *charClass;
	bool iswordc(unsigned char x) const noexcept {
		return charClass->IsWord(x);
	}
};

}
