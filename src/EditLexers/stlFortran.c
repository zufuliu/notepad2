#include "EditLexer.h"
#include "EditStyleX.h"

static KEYWORDLIST Keywords_Fortran = {{
//++Autogenerated -- start of section automatically generated
"all allocate assign backspace call case change class close common concurrent contains cycle data deallocate default "
"else elseif elsewhere endfile entry enumerator equivalence error event exit fail file final flush form format "
"generic go goto image images implicit import include inquire is lock map memory namelist none nullify only open "
"pause post print procedure rank read record result return rewind stop structure sync then to union unlock use "
"wait while write "

, // 1 code folding
"associate block blockdata continue critical do "
"end endassociate endblock endblockdata endcritical enddo endenum endforall endfunction endif endinterface endmodule "
"endprocedure endprogram endselect endsubmodule endsubroutine endteam endtype endwhere enum "
"forall function if interface module program select selectcase selectrank selecttype submodule subroutine team type "
"where "

, // 2 type
"character complex double doubleprecision event_type integer lock_type logical precision real team_type typespec "

, // 3 attribute
"abstract access acquired_lock action advance allocatable assignment asynchronous automatic bind blank "
"codimension contiguous convert decimal deferred delim dimension direct "
"elemental encoding eor err errmsg exist extends external fmt formatted "
"id impure in inout intent intrinsic iolength iomsg iostat kind len local local_init mold "
"name named new_index newunit nextrec nml non_intrinsic non_overridable non_recursive nopass number "
"opened operator optional out pad parameter pass pending pointer pos position private protected public pure quiet "
"readwrite rec recl recursive round save sequence sequential shared sign size source stat static status stream "
"target team_number unformatted unit until_count value volatile "

, // 4 function
"abs( achar( acos( acosh( adjustl( adjustr( aimag( aint( all( allocated( anint( any( asin( asinh( associated( "
"atan( atan2( atanh( atomic_add( atomic_and( atomic_cas( atomic_define( "
"atomic_fetch_add( atomic_fetch_and( atomic_fetch_or( atomic_fetch_xor( atomic_or( atomic_ref( atomic_xor( "
"backspace( bessel_j0( bessel_j1( bessel_jn( bessel_y0( bessel_y1( bessel_yn( bge( bgt( bit_size( ble( blt( btest( "
"ceiling( char( class( close( cmplx( co_broadcast( co_max( co_min( co_reduce( co_sum( "
"command_argument_count( compiler_options( compiler_version( conjg( cos( cosh( coshape( count( cpu_time( cshift( "
"date_and_time( dble( digits( dim( dimension( dot_product( dprod( dshiftl( dshiftr( "
"endfile( eoshift( epsilon( erf( erfc( erfc_scaled( event_query( execute_command_line( exp( exponent( extends_type_of( "
"failed_images( findloc( floor( flush( format( fraction( "
"gamma( get_command( get_command_argument( get_environment_variable( get_team( huge( hypot( "
"iachar( iall( iand( iany( ibclr( ibits( ibset( ichar( ieor( image_index( image_status( index( inquire( int( intent( "
"ior( iparity( is_contiguous( is_iostat_end( is_iostat_eor( ishft( ishftc( "
"kind( lbound( lcobound( leadz( len( len_trim( lge( lgt( lle( llt( log( log10( log_gamma( logical( "
"maskl( maskr( matmul( max( maxexponent( maxloc( maxval( merge( merge_bits( min( minexponent( minloc( minval( "
"mod( modulo( move_alloc( mvbits( "
"nearest( new_line( nint( norm2( not( null( num_images( open( out_of_range( "
"pack( parity( popcnt( poppar( precision( present( product( "
"radix( random_init( random_number( random_seed( range( rank( read( real( reduce( repeat( reshape( rewind( rrspacing( "
"same_type_as( scale( scan( selected_char_kind( selected_int_kind( selected_real_kind( set_exponent( "
"shape( shifta( shiftl( shiftr( sign( sin( sinh( size( spacing( spread( sqrt( stopped_images( storage_size( sum( "
"system_clock( "
"tan( tanh( team_number( this_image( tiny( trailz( transfer( transpose( trim( type( ubound( ucobound( unpack( verify( "
"wait( write( "

, // 5 misc
"ABS( ABSTRACT ACCESS ACHAR( ACOS( ACOSH( ACQUIRED_LOCK ACTION ADJUSTL( ADJUSTR( ADVANCE AIMAG( AINT( "
"ALL ALL( ALLOCATABLE ALLOCATE ALLOCATED( ANINT( ANY( ASIN( ASINH( ASSIGN ASSIGNMENT ASSOCIATE ASSOCIATED( ASYNCHRONOUS "
"ATAN( ATAN2( ATANH( ATOMIC_ADD( ATOMIC_AND( ATOMIC_CAS( ATOMIC_DEFINE( "
"ATOMIC_FETCH_ADD( ATOMIC_FETCH_AND( ATOMIC_FETCH_OR( ATOMIC_FETCH_XOR( ATOMIC_OR( ATOMIC_REF( ATOMIC_XOR( AUTOMATIC "
"BACKSPACE BACKSPACE( BESSEL_J0( BESSEL_J1( BESSEL_JN( BESSEL_Y0( BESSEL_Y1( BESSEL_YN( BGE( BGT( BIND BIT_SIZE( "
"BLANK BLE( BLOCK BLOCKDATA BLT( BTEST( "
"CALL CASE CEILING( CHANGE CHAR( CHARACTER CLASS CLASS( CLOSE CLOSE( CMPLX( CODIMENSION "
"COMMAND_ARGUMENT_COUNT( COMMON COMPILER_OPTIONS( COMPILER_VERSION( COMPLEX "
"CONCURRENT CONJG( CONTAINS CONTIGUOUS CONTINUE CONVERT COS( COSH( COSHAPE( COUNT( "
"CO_BROADCAST( CO_MAX( CO_MIN( CO_REDUCE( CO_SUM( CPU_TIME( CRITICAL CSHIFT( CYCLE "
"DATA DATE_AND_TIME( DBLE( DEALLOCATE DECIMAL DEFAULT DEFERRED DELIM DIGITS( DIM( DIMENSION DIMENSION( DIRECT "
"DO DOT_PRODUCT( DOUBLE DOUBLEPRECISION DPROD( DSHIFTL( DSHIFTR( "
"ELEMENTAL ELSE ELSEIF ELSEWHERE ENCODING END ENDASSOCIATE ENDBLOCK ENDBLOCKDATA ENDCRITICAL ENDDO ENDENUM "
"ENDFILE ENDFILE( ENDFORALL ENDFUNCTION ENDIF ENDINTERFACE ENDMODULE ENDPROCEDURE ENDPROGRAM "
"ENDSELECT ENDSUBMODULE ENDSUBROUTINE ENDTEAM ENDTYPE ENDWHERE ENTRY ENUM ENUMERATOR EOR EOSHIFT( EPSILON( EQUIVALENCE "
"ERF( ERFC( ERFC_SCALED( ERR ERRMSG ERROR EVENT EVENT_QUERY( EVENT_TYPE "
"EXECUTE_COMMAND_LINE( EXIST EXIT EXP( EXPONENT( EXTENDS EXTENDS_TYPE_OF( EXTERNAL "
"FAIL FAILED_IMAGES( FILE FINAL FINDLOC( FLOOR( FLUSH FLUSH( FMT FORALL FORM FORMAT FORMAT( FORMATTED FRACTION( FUNCTION "
"GAMMA( GENERIC GET_COMMAND( GET_COMMAND_ARGUMENT( GET_ENVIRONMENT_VARIABLE( GET_TEAM( GO GOTO HUGE( HYPOT( "
"IACHAR( IALL( IAND( IANY( IBCLR( IBITS( IBSET( ICHAR( ID IEOR( IF "
"IMAGE IMAGES IMAGE_INDEX( IMAGE_STATUS( IMPLICIT IMPORT IMPURE "
"IN INCLUDE INDEX( INOUT INQUIRE INQUIRE( INT( INTEGER INTENT INTENT( INTERFACE INTRINSIC IOLENGTH IOMSG IOR( IOSTAT "
"IPARITY( IS ISHFT( ISHFTC( IS_CONTIGUOUS( IS_IOSTAT_END( IS_IOSTAT_EOR( "
"KIND KIND( "
"LBOUND( LCOBOUND( LEADZ( LEN LEN( LEN_TRIM( LGE( LGT( LLE( LLT( "
"LOCAL LOCAL_INIT LOCK LOCK_TYPE LOG( LOG10( LOGICAL LOGICAL( LOG_GAMMA( "
"MAP MASKL( MASKR( MATMUL( MAX( MAXEXPONENT( MAXLOC( MAXVAL( MEMORY MERGE( MERGE_BITS( MIN( MINEXPONENT( MINLOC( MINVAL( "
"MOD( MODULE MODULO( MOLD MOVE_ALLOC( MVBITS( "
"NAME NAMED NAMELIST NEAREST( NEWUNIT NEW_INDEX NEW_LINE( NEXTREC NINT( NML "
"NONE NON_INTRINSIC NON_OVERRIDABLE NON_RECURSIVE NOPASS NORM2( NOT( NULL( NULLIFY NUMBER NUM_IMAGES( "
"ONLY OPEN OPEN( OPENED OPERATOR OPTIONAL OUT OUT_OF_RANGE( "
"PACK( PAD PARAMETER PARITY( PASS PAUSE PENDING POINTER POPCNT( POPPAR( POS POSITION POST "
"PRECISION PRECISION( PRESENT( PRINT PRIVATE PROCEDURE PRODUCT( PROGRAM PROTECTED PUBLIC PURE "
"QUIET "
"RADIX( RANDOM_INIT( RANDOM_NUMBER( RANDOM_SEED( RANGE( RANK RANK( "
"READ READ( READWRITE REAL REAL( REC RECL RECORD RECURSIVE REDUCE( REPEAT( RESHAPE( RESULT RETURN REWIND REWIND( ROUND "
"RRSPACING( "
"SAME_TYPE_AS( SAVE SCALE( SCAN( "
"SELECT SELECTCASE SELECTED_CHAR_KIND( SELECTED_INT_KIND( SELECTED_REAL_KIND( SELECTRANK SELECTTYPE SEQUENCE SEQUENTIAL "
"SET_EXPONENT( SHAPE( SHARED SHIFTA( SHIFTL( SHIFTR( SIGN SIGN( SIN( SINH( SIZE SIZE( SOURCE SPACING( SPREAD( SQRT( "
"STAT STATIC STATUS STOP STOPPED_IMAGES( STORAGE_SIZE( STREAM STRUCTURE SUBMODULE SUBROUTINE SUM( SYNC SYSTEM_CLOCK( "
"TAN( TANH( TARGET TEAM TEAM_NUMBER TEAM_NUMBER( TEAM_TYPE THEN THIS_IMAGE( TINY( TO TRAILZ( TRANSFER( TRANSPOSE( TRIM( "
"TYPE TYPE( TYPESPEC "
"UBOUND( UCOBOUND( UNFORMATTED UNION UNIT UNLOCK UNPACK( UNTIL_COUNT USE VALUE VERIFY( VOLATILE "
"WAIT WAIT( WHERE WHILE WRITE WRITE( "

, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
//--Autogenerated -- end of section automatically generated
}};

static EDITSTYLE Styles_Fortran[] = {
	EDITSTYLE_DEFAULT,
	{ MULTI_STYLE(SCE_F_WORD, SCE_F_FOLDING_WORD, 0, 0), NP2StyleX_Keyword, L"fore:#0000FF" },
	{ SCE_F_TYPE, NP2StyleX_TypeKeyword, L"fore:#007F7F" },
	{ SCE_F_PREPROCESSOR, NP2StyleX_Preprocessor, L"fore:#FF8000" },
	{ SCE_F_ATTRIBUTE, NP2StyleX_Attribute, L"fore:#FF8000" },
	{ SCE_F_INTRINSIC, NP2StyleX_BasicFunction, L"fore:#0080FF" },
	{ SCE_F_FUNCTION_DEFINITION, NP2StyleX_FunctionDefinition, L"bold; fore:#A46000" },
	{ SCE_F_FUNCTION, NP2StyleX_Function, L"fore:#A46000" },
	{ SCE_F_COMMENT, NP2StyleX_Comment, L"fore:#608060" },
	{ MULTI_STYLE(SCE_F_STRING_SQ, SCE_F_STRING_DQ, 0, 0), NP2StyleX_String, L"fore:#008000" },
	{ SCE_F_ESCAPECHAR, NP2StyleX_EscapeSequence, L"fore:#0080C0" },
	{ SCE_F_NUMBER, NP2StyleX_Number, L"fore:#FF0000" },
	{ MULTI_STYLE(SCE_F_OPERATOR, SCE_F_OPERATOR2, 0, 0), NP2StyleX_Operator, L"fore:#B000B0" },
};

EDITLEXER lexFortran = {
	SCLEX_FORTRAN, NP2LEX_FORTRAN,
//Settings++Autogenerated -- start of section automatically generated
		LexerAttr_CppPreprocessor |
		LexerAttr_CharacterPrefix,
		TAB_WIDTH_4, INDENT_WIDTH_4,
		(1 << 0) | (1 << 1), // level1, level2
		SCE_F_FUNCTION_DEFINITION,
		'\\', SCE_F_ESCAPECHAR, 0,
		0,
		0, 0,
		SCE_F_OPERATOR, SCE_F_OPERATOR2
		, KeywordAttr32(0, KeywordAttr_PreSorted) // keywords
		| KeywordAttr32(1, KeywordAttr_PreSorted) // code folding
		| KeywordAttr32(2, KeywordAttr_PreSorted) // type
		| KeywordAttr32(3, KeywordAttr_PreSorted) // attribute
		| KeywordAttr32(4, KeywordAttr_PreSorted) // function
		| KeywordAttr32(5, KeywordAttr_NoLexer) // misc
		, SCE_F_COMMENT,
		SCE_F_STRING_SQ, SCE_F_ESCAPECHAR,
//Settings--Autogenerated -- end of section automatically generated
	EDITLEXER_HOLE(L"Fortran Source", Styles_Fortran),
	L"f; for; ftn; fpp; f90; f95; f03; f08; f2k; hf",
	&Keywords_Fortran,
	Styles_Fortran
};
