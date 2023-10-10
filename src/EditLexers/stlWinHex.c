#include "EditLexer.h"
#include "EditStyleX.h"

static KEYWORDLIST Keywords_WinHex = {{
//++Autogenerated -- start of section automatically generated
"applies_to begin big-endian decimal description end endsection fixed_start hexadecimal little-endian multiple numbering "
"octal read-only read-write requires section sector-aligned template "

, // 1 type
"AppleDateTime binary boole16 boole32 boole8 boolean byte char char16 DOSDateTime double extended FileTime float GUID "
"hex int16 int24 int32 int64 int8 JavaDateTime longdouble OLEDateTime real single SQLDateTime string string16 time_t "
"uint16 uint24 uint32 uint48 uint8 uint_flex UNIXDateTime zstring zstring16 "

, // 2 commands
"AESDecrypt AESEncrypt Assign Block Block1 Block2 "
"CalcHash CalcHashEx Close CloseAll Convert Copy CopyFile CopyIntoNewFile Create CreateBackup CreateBackupEx "
"CurrentPos Cut "
"Debug Dec DeleteFile Else EndIf ExecuteScript Exit ExitIfNoFilesOpen ExitLoop Find ForAllObjDo "
"GetClusterAlloc GetClusterAllocEx GetClusterSize GetSize GetUserInput GetUserInputI Goto "
"IfEqual IfFound IfGreater Inc InitFreeSpace InitMFTRecords InitSlackSpace Insert InterpretImageAsDisk IntToStr JumpTo "
"Label MessageBox Move MoveFile NextObj Open Paste Read ReadLn Release Remove ReplaceAll "
"Save SaveAll SaveAs SetVarSize StrCat StrToInt Terminate Turbo unlimited UseLogFile Write Write2 WriteClipboard "

, // 3 misc
"ANSI Base64 Binary BlockOnly Down HexASCII IBM IntelHex LowerCase MatchCase MatchWord MotorolaS Off On RAM SaveAllPos "
"UUCode Unicode Up UpperCase Wildcards disk false file hiberfil true "

, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
//--Autogenerated -- end of section automatically generated
}};

static EDITSTYLE Styles_WinHex[] = {
	EDITSTYLE_DEFAULT,
	{ SCE_WINHEX_KEYWORD, NP2StyleX_Keyword, L"bold; fore:#FF8000" },
	{ SCE_WINHEX_TYPE, NP2StyleX_Type, L"fore:#0080FF" },
	{ SCE_WINHEX_COMMAND, NP2StyleX_Command, L"fore:#A46000" },
	{ SCE_WINHEX_COMMENTLINE, NP2StyleX_Comment, L"fore:#608060" },
	//{ SCE_WINHEX_ESCAPECHAR, NP2StyleX_EscapeSequence, L"fore:#0080C0" },
	{ SCE_WINHEX_STRING, NP2StyleX_String, L"fore:#008000" },
	{ SCE_WINHEX_NUMBER, NP2StyleX_Number, L"fore:#FF0000" },
	{ SCE_WINHEX_OPERATOR, NP2StyleX_Operator, L"fore:#B000B0" },
};

EDITLEXER lexWinHex = {
	SCLEX_WINHEX, NP2LEX_WINHEX,
//Settings++Autogenerated -- start of section automatically generated
		LexerAttr_NoBlockComment |
		LexerAttr_EscapePunctuation,
		TAB_WIDTH_4, INDENT_WIDTH_4,
		(1 << 0) | (1 << 1), // level1, level2
		0,
		'\\', SCE_WINHEX_ESCAPECHAR, 0,
		0,
		0, 0,
		SCE_WINHEX_OPERATOR, 0
		, KeywordAttr32(0, KeywordAttr_PreSorted) // keywords
		| KeywordAttr32(1, KeywordAttr_MakeLower | KeywordAttr_PreSorted) // type
		| KeywordAttr32(2, KeywordAttr_MakeLower | KeywordAttr_PreSorted) // commands
		| KeywordAttr32(3, KeywordAttr_NoLexer) // misc
		, SCE_WINHEX_COMMENTLINE,
		SCE_WINHEX_STRING, SCE_WINHEX_ESCAPECHAR,
//Settings--Autogenerated -- end of section automatically generated
	EDITLEXER_HOLE(L"WinHex Script", Styles_WinHex),
	L"whs",
	&Keywords_WinHex,
	Styles_WinHex
};
